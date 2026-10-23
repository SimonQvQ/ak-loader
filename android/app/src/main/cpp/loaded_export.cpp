#include "loaded_export.h"
#include <elf.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <vector>

namespace {
struct Mapping {
    uintptr_t begin, end, offset;
    unsigned long inode;
    unsigned major, minor;
    bool executable, readable;
};
struct FileImage {
    int fd = -1;
    size_t size = 0;
    const unsigned char *bytes = nullptr;
    ~FileImage() {
        if (bytes)
            munmap(const_cast<unsigned char *>(bytes), size);
        if (fd >= 0)
            close(fd);
    }
    bool contains(uint64_t offset, uint64_t count) const {
        return offset <= size && count <= size - offset;
    }
    template <class T> bool read(uint64_t offset, T &value) const {
        if (!contains(offset, sizeof(T)))
            return false;
        std::memcpy(&value, bytes + offset, sizeof(T));
        return true;
    }
};
} // namespace

void *find_loaded_export(const char *path, const char *wanted, const char **reason) {
    auto fail = [reason](const char *message) -> void * {
        if (reason)
            *reason = message;
        return nullptr;
    };
    if (reason)
        *reason = "ELF export not found";
    if (!path || path[0] != '/' || !wanted || !*wanted)
        return nullptr;
    FILE *maps = std::fopen("/proc/self/maps", "r");
    if (!maps)
        return fail("cannot read self maps");
    std::vector<Mapping> rows;
    char line[4096];
    while (std::fgets(line, sizeof line, maps)) {
        unsigned long lo, hi, offset, inode;
        unsigned dev_major, dev_minor;
        char perms[5]{};
        int used = 0;
        if (std::sscanf(line, "%lx-%lx %4s %lx %x:%x %lu %n", &lo, &hi, perms, &offset, &dev_major,
                        &dev_minor, &inode, &used) != 7)
            continue;
        char *name = line + used;
        name[std::strcspn(name, "\n")] = '\0';
        if (std::strcmp(name, path) == 0)
            rows.push_back(
                {lo, hi, offset, inode, dev_major, dev_minor, perms[2] == 'x', perms[0] == 'r'});
    }
    std::fclose(maps);
    const Mapping *header = nullptr;
    for (const auto &row : rows)
        if (row.offset == 0) {
            header = &row;
            break;
        }
    if (!header)
        return fail("no exact offset-zero mapping");
    FileImage image;
    image.fd = open(path, O_RDONLY | O_CLOEXEC);
    struct stat statbuf{};
    if (image.fd < 0)
        return fail("cannot open mapped provider file");
    if (fstat(image.fd, &statbuf) != 0)
        return fail("cannot stat mapped provider file");
    if (statbuf.st_size < 64 || statbuf.st_size > 64 * 1024 * 1024)
        return fail("provider file size invalid");
    const bool same_identity = statbuf.st_ino == header->inode &&
                               major(statbuf.st_dev) == header->major &&
                               minor(statbuf.st_dev) == header->minor;
    image.size = static_cast<size_t>(statbuf.st_size);
    void *data = mmap(nullptr, image.size, PROT_READ, MAP_PRIVATE, image.fd, 0);
    if (data == MAP_FAILED)
        return fail("cannot map provider ELF");
    image.bytes = static_cast<const unsigned char *>(data);
    Elf64_Ehdr elf{};
    if (!image.read(0, elf) || std::memcmp(elf.e_ident, ELFMAG, SELFMAG) != 0 ||
        elf.e_ident[EI_CLASS] != ELFCLASS64 || elf.e_ident[EI_DATA] != ELFDATA2LSB ||
        elf.e_machine != EM_X86_64 || elf.e_type != ET_DYN ||
        elf.e_shentsize != sizeof(Elf64_Shdr) || elf.e_phentsize != sizeof(Elf64_Phdr) ||
        !image.contains(elf.e_shoff, uint64_t(elf.e_shnum) * sizeof(Elf64_Shdr)) ||
        !image.contains(elf.e_phoff, uint64_t(elf.e_phnum) * sizeof(Elf64_Phdr)))
        return fail("invalid host ELF header");
    uintptr_t bias = 0;
    bool found_bias = false;
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        return nullptr;
    for (unsigned i = 0; i < elf.e_phnum; ++i) {
        Elf64_Phdr ph{};
        if (!image.read(elf.e_phoff + uint64_t(i) * sizeof ph, ph))
            return nullptr;
        if (ph.p_type == PT_LOAD && ph.p_offset == 0 && ph.p_filesz >= sizeof elf) {
            uintptr_t vpage = ph.p_vaddr & ~(uintptr_t(page_size) - 1);
            if (header->begin < vpage)
                return nullptr;
            bias = header->begin - vpage;
            found_bias = true;
            break;
        }
    }
    if (!found_bias)
        return fail("no header load segment");
    if (!same_identity) {
        // APEX bind mounts can report different device IDs in maps and stat.
        // Accept that case only when a GNU build-id matches the mapped image.
        bool same_build_id = false;
        for (unsigned i = 0; i < elf.e_phnum; ++i) {
            Elf64_Phdr ph{};
            if (!image.read(elf.e_phoff + uint64_t(i) * sizeof ph, ph))
                return nullptr;
            if (ph.p_type != PT_NOTE || ph.p_filesz > 65536 ||
                !image.contains(ph.p_offset, ph.p_filesz) || ph.p_vaddr > UINTPTR_MAX - bias)
                continue;
            const uintptr_t address = bias + ph.p_vaddr;
            bool readable = false;
            for (const auto &row : rows)
                if (row.readable && address >= row.begin && address <= row.end &&
                    ph.p_filesz <= row.end - address)
                    readable = true;
            if (!readable)
                continue;
            uint64_t at = 0;
            while (at + sizeof(Elf64_Nhdr) <= ph.p_filesz) {
                Elf64_Nhdr note{};
                if (!image.read(ph.p_offset + at, note))
                    break;
                const uint64_t namesize = (uint64_t(note.n_namesz) + 3) & ~uint64_t(3);
                const uint64_t descsize = (uint64_t(note.n_descsz) + 3) & ~uint64_t(3);
                const uint64_t total = sizeof note + namesize + descsize;
                if (total > ph.p_filesz - at)
                    break;
                const auto *start = image.bytes + ph.p_offset + at;
                if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && note.n_descsz >= 16 &&
                    note.n_descsz <= 64 && std::memcmp(start + sizeof note, "GNU\0", 4) == 0 &&
                    std::memcmp(reinterpret_cast<const void *>(address + at), start, total) == 0)
                    same_build_id = true;
                at += total;
            }
        }
        if (!same_build_id)
            return fail("provider identity and build-id mismatch");
    }
    for (unsigned i = 0; i < elf.e_shnum; ++i) {
        Elf64_Shdr syms{}, strings{};
        if (!image.read(elf.e_shoff + uint64_t(i) * sizeof syms, syms))
            return nullptr;
        if (syms.sh_type != SHT_DYNSYM || syms.sh_entsize != sizeof(Elf64_Sym) ||
            syms.sh_link >= elf.e_shnum || !image.contains(syms.sh_offset, syms.sh_size))
            continue;
        if (!image.read(elf.e_shoff + uint64_t(syms.sh_link) * sizeof strings, strings) ||
            strings.sh_type != SHT_STRTAB || !image.contains(strings.sh_offset, strings.sh_size))
            return nullptr;
        for (uint64_t n = 0; n < syms.sh_size / sizeof(Elf64_Sym); ++n) {
            Elf64_Sym symbol{};
            if (!image.read(syms.sh_offset + n * sizeof symbol, symbol))
                return nullptr;
            if (symbol.st_name >= strings.sh_size || symbol.st_shndx == SHN_UNDEF ||
                symbol.st_shndx >= SHN_LORESERVE || ELF64_ST_TYPE(symbol.st_info) != STT_FUNC)
                continue;
            unsigned bind = ELF64_ST_BIND(symbol.st_info), visibility = symbol.st_other & 3u;
            if ((bind != STB_GLOBAL && bind != STB_WEAK) ||
                (visibility != STV_DEFAULT && visibility != STV_PROTECTED))
                continue;
            const char *name =
                reinterpret_cast<const char *>(image.bytes + strings.sh_offset + symbol.st_name);
            if (!std::memchr(name, '\0', strings.sh_size - symbol.st_name) ||
                std::strcmp(name, wanted))
                continue;
            if (symbol.st_value > std::numeric_limits<uintptr_t>::max() - bias)
                return nullptr;
            const uintptr_t address = bias + symbol.st_value;
            for (const auto &row : rows)
                if (row.executable && address >= row.begin && address < row.end)
                    return reinterpret_cast<void *>(address);
        }
    }
    return nullptr;
}

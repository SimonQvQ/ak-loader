"""Apply the ELF assembler spelling required by NDK clang to pinned Dobby."""
from pathlib import Path
import subprocess
import sys

root = Path(sys.argv[1])
name = "source/TrampolineBridge/ClosureTrampolineBridge/arm64/closure_bridge_arm64.asm"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
old = "adrp TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGE\nadd TMP_REG_0, TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGEOFF"
assert source.count(old) == 1
source = source.replace(old, """#if defined(__APPLE__)
adrp TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGE
add TMP_REG_0, TMP_REG_0, cdecl(common_closure_bridge_handler)@PAGEOFF
#else
adrp TMP_REG_0, :got:cdecl(common_closure_bridge_handler)
ldr TMP_REG_0, [TMP_REG_0, :got_lo12:cdecl(common_closure_bridge_handler)]
#endif""")
(root / name).write_text(source)

name = "source/Backend/UserMode/ExecMemory/code-patch-tool-posix.cc"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
source = source.replace('#include "core/arch/Cpu.h"', '#include "PlatformUnifiedInterface/ExecMemory/ClearCacheTool.h"')
(root / name).write_text(source)

# platform.h includes common.h before declaring OSMemory, producing a circular
# declaration in upstream's Android-only helper. Use the equivalent POSIX call.
name = "common/os_arch_features.h"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
source = source.replace('#include <stddef.h>', '#include <stddef.h>\n#include <unistd.h>\n#include <sys/mman.h>')
source = source.replace('OSMemory::PageSize()', 'getpagesize()')
source = source.replace('OSMemory::SetPermission(page, getpagesize(), kReadExecute)',
                        '(mprotect(page, getpagesize(), PROT_READ | PROT_EXEC) == 0)')
(root / name).write_text(source)

name = "source/Backend/UserMode/PlatformUtil/Linux/ProcessRuntime.cc"
source = subprocess.check_output(["git", "-C", str(root), "show", f"HEAD:{name}"], text=True)
source = source.replace('a.start < b.start', 'a.start() < b.start()')
source = source.replace('module.load_address', 'module.base')
source = source.replace('#define LINE_MAX 2048', '#undef LINE_MAX\n#define LINE_MAX 2048')
(root / name).write_text(source)

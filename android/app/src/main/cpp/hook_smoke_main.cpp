#include <cstdio>
bool akl_hook_smoke();
int main() {
    const bool passed = akl_hook_smoke();
    std::puts(passed ? "ADRP hook/original/unhook execution passed" : "ADRP hook execution FAILED");
    return passed ? 0 : 1;
}

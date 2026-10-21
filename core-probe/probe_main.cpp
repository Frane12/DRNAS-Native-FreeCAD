#include "freecadbase_probe.h"
#include <cstdio>
int main() {
    const int code = drnas_freecadbase_probe();
    if (code == 0) {
        std::puts("PASS: ORIGINAL FreeCADBase source executed Base64 text + binary roundtrips");
    } else {
        std::printf("FAIL: FreeCADBase source smoke failed at check %d\n", code);
    }
    return code;
}

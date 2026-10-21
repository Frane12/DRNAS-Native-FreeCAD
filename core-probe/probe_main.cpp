#include "freecadbase_probe.h"
#include <cstdio>
int main() {
    const int code = drnas_freecadbase_probe();
    if (code == 0) {
        std::puts("PASS: Real FreeCADBase Base64 + Vector3D source executed (text, binary, CAD vectors)");
    } else {
        std::printf("FAIL: FreeCADBase source test failed at check %d\n", code);
    }
    return code;
}

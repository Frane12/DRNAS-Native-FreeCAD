#include "freecadbase_probe.h"
#include <Base64.h>
#include <Vector3D.h>
#include <cmath>
#include <cstring>
#include <string>

// Directly links FreeCAD upstream Base64.cpp and Vector3D.cpp (NOT all FreeCADBase).
extern "C" int drnas_freecadbase_probe() {
    const std::string text = "DRNAS FreeCAD Android ARM64";
    const std::string encoded = Base::base64_encode(text.data(), text.size());
    if (encoded != "RFJOQVMgRnJlZUNBRCBBbmRyb2lkIEFSTTY0") return 1;
    if (Base::base64_decode(encoded) != text) return 2;

    const unsigned char payload[] = {0, 1, 2, 127, 128, 254, 255};
    const std::string binary64 = Base::base64_encode(payload, sizeof(payload));
    const std::string decoded = Base::base64_decode(binary64);
    if (decoded.size() != sizeof(payload) ||
        std::memcmp(decoded.data(), payload, sizeof(payload)) != 0) return 3;

    // Actual FreeCAD C++ vector implementation is linked into the Android ELF.
    const Base::Vector3d a(3.0, 4.0, 0.0);
    const Base::Vector3d b(0.0, 0.0, 2.0);
    if (std::abs(a.Length() - 5.0) > 1e-12) return 4;
    if (std::abs(a.Dot(b)) > 1e-12) return 5;
    const Base::Vector3d c = a.Cross(b);
    if (std::abs(c.x - 8.0) > 1e-12 ||
        std::abs(c.y + 6.0) > 1e-12 ||
        std::abs(c.z) > 1e-12) return 6;
    if (std::abs(a.Normalized().Length() - 1.0) > 1e-12) return 7;
    return 0;
}

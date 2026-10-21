#include "freecadbase_probe.h"
#include <Base64.h>
#include <cstring>
#include <string>

// This links directly to the ORIGINAL pinned upstream FreeCAD src/Base/Base64.cpp.
// Deliberately label it "FreeCADBase source slice", not FreeCADBase or FreeCADApp.
extern "C" int drnas_freecadbase_probe() {
    const std::string text = "DRNAS FreeCAD Android ARM64";
    const std::string encoded = Base::base64_encode(text.data(), text.size());
    if (encoded != "RFJOQVMgRnJlZUNBRCBBbmRyb2lkIEFSTTY0") return 1;
    const std::string decoded = Base::base64_decode(encoded);
    if (decoded != text) return 2;

    const unsigned char payload[] = {0, 1, 2, 127, 128, 254, 255};
    const std::string binary64 = Base::base64_encode(payload, sizeof(payload));
    const std::string roundtrip = Base::base64_decode(binary64);
    if (roundtrip.size() != sizeof(payload) ||
        std::memcmp(roundtrip.data(), payload, sizeof(payload)) != 0) return 3;
    return 0;
}

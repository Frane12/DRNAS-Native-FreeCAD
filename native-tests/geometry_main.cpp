#include "drnas_occt_geometry.h"
#include <cstdio>
int main() {
  double volume = 0.0;
  const int rc = drnas_occt_geometry_selftest(&volume);
  if (rc == 0) {
    std::printf("DRNAS OCCT ARM64 geometry PASS, box-minus-bore volume=%.3f mm3\n", volume);
    return 0;
  }
  std::fprintf(stderr, "DRNAS OCCT ARM64 geometry FAIL (code %d)\n", rc);
  return rc;
}

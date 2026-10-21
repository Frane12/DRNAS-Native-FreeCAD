#include "drnas_occt_geometry.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <GProp_GProps.hxx>
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <cmath>
extern "C" int drnas_occt_geometry_selftest(double* volume_mm3) {
  if (!volume_mm3) return -1;
  *volume_mm3 = 0.0;
  try {
    BRepPrimAPI_MakeBox box(10.0, 20.0, 30.0);
    if (!box.IsDone()) return 1;
    BRepPrimAPI_MakeCylinder bore(
        gp_Ax2(gp_Pnt(5.0, 10.0, 0.0), gp_Dir(0.0, 0.0, 1.0)),
        3.0, 30.0);
    if (!bore.IsDone()) return 2;
    BRepAlgoAPI_Cut cut(box.Shape(), bore.Shape());
    cut.Build();
    if (!cut.IsDone() || cut.Shape().IsNull()) return 3;
    if (!BRepCheck_Analyzer(cut.Shape()).IsValid()) return 4;
    GProp_GProps props;
    BRepGProp::VolumeProperties(cut.Shape(), props);
    const double volume = props.Mass();
    const double expected = 6000.0 - 270.0 * std::acos(-1.0);
    if (!std::isfinite(volume) || std::abs(volume - expected) > 0.1) return 5;
    BRepMesh_IncrementalMesh mesh(cut.Shape(), 0.35);
    if (!mesh.IsDone()) return 6;
    *volume_mm3 = volume;
    return 0;
  } catch (const Standard_Failure&) {
    return 7;
  } catch (...) {
    return 8;
  }
}

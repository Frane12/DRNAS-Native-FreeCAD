// DRNAS FreeCAD Render LAB: real OCCT B-Rep shapes -> native triangle mesh.
// JNI output: XYZ + flat normal, 6 floats per vertex, 3 vertices per triangle.
#include <jni.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shape.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <vector>
#include <utility>

namespace {
bool buildShape(int kind, TopoDS_Shape& solid) {
    if (kind == 0) {   // Box with true Boolean drilled hole
        BRepPrimAPI_MakeBox box(10.0, 20.0, 30.0);
        box.Build();
        if (!box.IsDone()) return false;
        BRepPrimAPI_MakeCylinder bore(
            gp_Ax2(gp_Pnt(5, 10, 0), gp_Dir(0, 0, 1)), 3.0, 30.0);
        bore.Build();
        if (!bore.IsDone()) return false;
        BRepAlgoAPI_Cut cut(box.Shape(), bore.Shape());
        cut.Build();
        if (!cut.IsDone() || cut.Shape().IsNull()) return false;
        solid = cut.Shape();
    } else if (kind == 1) {
        BRepPrimAPI_MakeBox box(20.0, 20.0, 20.0);
        box.Build();
        if (!box.IsDone()) return false;
        solid = box.Shape();
    } else if (kind == 2) {
        BRepPrimAPI_MakeCylinder cylinder(8.0, 24.0);
        cylinder.Build();
        if (!cylinder.IsDone()) return false;
        solid = cylinder.Shape();
    } else if (kind == 3) {
        BRepPrimAPI_MakeSphere sphere(10.0);
        sphere.Build();
        if (!sphere.IsDone()) return false;
        solid = sphere.Shape();
    } else return false;
    return !solid.IsNull();
}
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_org_drnas_freecad_smoke_NativeCAD_createOcctMesh(JNIEnv* env, jclass, jint kind) {
    std::vector<float> vertices;
    try {
        TopoDS_Shape solid;
        if (!buildShape(kind, solid)) return nullptr;
        BRepMesh_IncrementalMesh triangulator(solid, 0.25, false, 0.28, false);
        if (!triangulator.IsDone()) return nullptr;

        constexpr size_t kMaxTriangles = 200000;
        for (TopExp_Explorer exp(solid, TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Face face = TopoDS::Face(exp.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, location);
            if (tri.IsNull()) continue;
            for (Standard_Integer i = 1; i <= tri->NbTriangles(); ++i) {
                if (vertices.size() / 18 >= kMaxTriangles) return nullptr;
                Standard_Integer a, b, c;
                tri->Triangle(i).Get(a, b, c);
                if (face.Orientation() == TopAbs_REVERSED) std::swap(b, c);
                const gp_Pnt p[3] = {
                    tri->Node(a).Transformed(location.Transformation()),
                    tri->Node(b).Transformed(location.Transformation()),
                    tri->Node(c).Transformed(location.Transformation())
                };
                gp_Vec normal = gp_Vec(p[0], p[1]).Crossed(gp_Vec(p[0], p[2]));
                if (normal.SquareMagnitude() < 1e-16) continue;
                normal.Normalize();
                for (int v = 0; v < 3; ++v) {
                    vertices.push_back(static_cast<float>(p[v].X()));
                    vertices.push_back(static_cast<float>(p[v].Y()));
                    vertices.push_back(static_cast<float>(p[v].Z()));
                    vertices.push_back(static_cast<float>(normal.X()));
                    vertices.push_back(static_cast<float>(normal.Y()));
                    vertices.push_back(static_cast<float>(normal.Z()));
                }
            }
        }
    } catch (const Standard_Failure&) {
        return nullptr;
    } catch (...) {
        return nullptr;
    }
    if (vertices.empty() || vertices.size() > static_cast<size_t>(200000) * 18)
        return nullptr;

    jfloatArray output = env->NewFloatArray(static_cast<jsize>(vertices.size()));
    if (!output) return nullptr;
    env->SetFloatArrayRegion(output, 0, static_cast<jsize>(vertices.size()), vertices.data());
    return output;
}

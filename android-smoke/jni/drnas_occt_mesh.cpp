// DRNAS Render LAB — turn real OCCT B-Rep faces into Android GLES triangles.
// No generated demonstration cube: the shape is OCCT's actual Boolean cut.
#include <jni.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <Standard_Failure.hxx>
#include <vector>
#include <cmath>

extern "C" JNIEXPORT jfloatArray JNICALL
Java_org_drnas_freecad_smoke_NativeCAD_createOcctMesh(JNIEnv* env, jclass) {
    std::vector<float> vertices;
    try {
        BRepPrimAPI_MakeBox block(10.0, 20.0, 30.0);
        block.Build();
        if (!block.IsDone()) return nullptr;
        BRepPrimAPI_MakeCylinder drill(
            gp_Ax2(gp_Pnt(5, 10, 0), gp_Dir(0, 0, 1)), 3.0, 30.0);
        drill.Build();
        if (!drill.IsDone()) return nullptr;
        BRepAlgoAPI_Cut booleanCut(block.Shape(), drill.Shape());
        booleanCut.Build();
        if (!booleanCut.IsDone() || booleanCut.Shape().IsNull()) return nullptr;
        const TopoDS_Shape& solid = booleanCut.Shape();
        BRepMesh_IncrementalMesh triangulator(solid, 0.30, false, 0.35, false);
        if (!triangulator.IsDone()) return nullptr;

        constexpr size_t kMaxTriangles = 200000;
        for (TopExp_Explorer exp(solid, TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Face face = TopoDS::Face(exp.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) tris = BRep_Tool::Triangulation(face, location);
            if (tris.IsNull()) continue;
            for (Standard_Integer i = 1; i <= tris->NbTriangles(); ++i) {
                if (vertices.size() / 18 >= kMaxTriangles) return nullptr;
                Standard_Integer a, b, c;
                tris->Triangle(i).Get(a, b, c);
                if (face.Orientation() == TopAbs_REVERSED) {
                    const auto temp = b; b = c; c = temp;
                }
                const gp_Pnt p[3] = {
                    tris->Node(a).Transformed(location.Transformation()),
                    tris->Node(b).Transformed(location.Transformation()),
                    tris->Node(c).Transformed(location.Transformation())
                };
                gp_Vec n = gp_Vec(p[0], p[1]).Crossed(gp_Vec(p[0], p[2]));
                if (n.SquareMagnitude() < 1e-16) continue;
                n.Normalize();
                for (int v = 0; v < 3; ++v) {
                    vertices.push_back(static_cast<float>(p[v].X()));
                    vertices.push_back(static_cast<float>(p[v].Y()));
                    vertices.push_back(static_cast<float>(p[v].Z()));
                    vertices.push_back(static_cast<float>(n.X()));
                    vertices.push_back(static_cast<float>(n.Y()));
                    vertices.push_back(static_cast<float>(n.Z()));
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
    jfloatArray result = env->NewFloatArray(static_cast<jsize>(vertices.size()));
    if (!result) return nullptr;
    env->SetFloatArrayRegion(result, 0, static_cast<jsize>(vertices.size()), vertices.data());
    return result;
}

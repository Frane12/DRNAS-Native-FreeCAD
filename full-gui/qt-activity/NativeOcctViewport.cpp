#include "NativeOcctViewport.h"
#include <QEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QtMath>
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
#include <Standard_Failure.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <cmath>

namespace {
bool makeSolid(int kind,TopoDS_Shape& shape) {
    if(kind==0) {
        BRepPrimAPI_MakeBox box(10,20,30);
        box.Build();
        BRepPrimAPI_MakeCylinder drill(
            gp_Ax2(gp_Pnt(5,10,0),gp_Dir(0,0,1)),3,30);
        drill.Build();
        if(!box.IsDone() || !drill.IsDone()) return false;
        BRepAlgoAPI_Cut cut(box.Shape(),drill.Shape());
        cut.Build();
        if(!cut.IsDone()) return false;
        shape=cut.Shape();
    } else if(kind==1) {
        BRepPrimAPI_MakeBox box(20,20,20);
        box.Build();
        if(!box.IsDone()) return false;
        shape=box.Shape();
    } else if(kind==2) {
        BRepPrimAPI_MakeCylinder cylinder(8,24);
        cylinder.Build();
        if(!cylinder.IsDone()) return false;
        shape=cylinder.Shape();
    } else if(kind==3) {
        BRepPrimAPI_MakeSphere sphere(10);
        sphere.Build();
        if(!sphere.IsDone()) return false;
        shape=sphere.Shape();
    } else return false;
    return !shape.IsNull();
}
constexpr char kVertex[] =
    "attribute vec3 aPos;\n"
    "attribute vec3 aNormal;\n"
    "uniform mat4 uMVP;\n"
    "uniform mat4 uModel;\n"
    "varying vec3 vNormal;\n"
    "void main(){vNormal=normalize(mat3(uModel)*aNormal);"
    "gl_Position=uMVP*vec4(aPos,1.0);}\n";
constexpr char kFragment[] =
    "precision mediump float;\n"
    "varying vec3 vNormal;\n"
    "uniform float uFlat;\n"
    "uniform vec3 uColor;\n"
    "void main(){if(uFlat>0.5){gl_FragColor=vec4(uColor,1.0);return;}"
    "vec3 n=normalize(vNormal);vec3 l=normalize(vec3(0.45,0.65,1.0));"
    "float diff=max(dot(n,l),0.0);float back=max(dot(n,-l),0.0)*0.16;"
    "gl_FragColor=vec4(vec3(0.13,0.39,0.70)*(0.4+0.65*diff+back)"
    "+vec3(0.045,0.07,0.13),1.0);}\n";
}

NativeOcctViewport::NativeOcctViewport(QWidget* parent):QOpenGLWidget(parent) {
    QSurfaceFormat f;
    f.setRenderableType(QSurfaceFormat::OpenGLES);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    setFormat(f);
    setAttribute(Qt::WA_AcceptTouchEvents,true);
    setMinimumSize(160,210);
    setShape(0);
}
NativeOcctViewport::~NativeOcctViewport() {
    if(context()) {
        makeCurrent();
        if(vbo_.isCreated())vbo_.destroy();
        if(edgeVbo_.isCreated())edgeVbo_.destroy();
        doneCurrent();
    }
}
int NativeOcctViewport::triangleCount() const {
    return static_cast<int>(triangles_.size()/18);
}
bool NativeOcctViewport::setShape(int kind) {
    std::vector<float> mesh;
    try {
        TopoDS_Shape shape;
        if(!makeSolid(kind,shape)) return false;
        BRepMesh_IncrementalMesh triangulator(shape,0.25,false,0.28,false);
        if(!triangulator.IsDone())return false;
        const std::size_t maxTriangles=200000;
        for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next()) {
            const TopoDS_Face face=TopoDS::Face(it.Current());
            TopLoc_Location location;
            const Handle(Poly_Triangulation) tri=BRep_Tool::Triangulation(face,location);
            if(tri.IsNull()) continue;
            for(Standard_Integer k=1;k<=tri->NbTriangles();++k) {
                if(mesh.size()/18>=maxTriangles)return false;
                Standard_Integer a,b,c;
                tri->Triangle(k).Get(a,b,c);
                if(face.Orientation()==TopAbs_REVERSED)std::swap(b,c);
                gp_Pnt points[3]={
                    tri->Node(a).Transformed(location.Transformation()),
                    tri->Node(b).Transformed(location.Transformation()),
                    tri->Node(c).Transformed(location.Transformation())
                };
                gp_Vec n=gp_Vec(points[0],points[1]).Crossed(
                    gp_Vec(points[0],points[2]));
                if(n.SquareMagnitude()<1e-16)continue;
                n.Normalize();
                for(const auto& pt:points) {
                    mesh.push_back(static_cast<float>(pt.X()));
                    mesh.push_back(static_cast<float>(pt.Y()));
                    mesh.push_back(static_cast<float>(pt.Z()));
                    mesh.push_back(static_cast<float>(n.X()));
                    mesh.push_back(static_cast<float>(n.Y()));
                    mesh.push_back(static_cast<float>(n.Z()));
                }
            }
        }
    } catch(const Standard_Failure&) {return false;}
      catch(...) {return false;}
    if(mesh.empty())return false;
    float lo[3]={INFINITY,INFINITY,INFINITY},
          hi[3]={-INFINITY,-INFINITY,-INFINITY};
    for(std::size_t i=0;i<mesh.size();i+=6)for(int axis=0;axis<3;axis++){
        lo[axis]=std::min(lo[axis],mesh[i+axis]);
        hi[axis]=std::max(hi[axis],mesh[i+axis]);
    }
    midpoint_=QVector3D((lo[0]+hi[0])*.5f,(lo[1]+hi[1])*.5f,
                         (lo[2]+hi[2])*.5f);
    const float side=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});
    normalization_=side>0.f?2.15f/side:1.f;
    triangles_.swap(mesh);
    edgeVertices_.clear();
    edgeVertices_.reserve(triangles_.size()*2);
    constexpr int edgeIndices[]={0,1,1,2,2,0};
    for(std::size_t i=0;i<triangles_.size();i+=18) {
        for(int vertex:edgeIndices)
            edgeVertices_.insert(edgeVertices_.end(),
                triangles_.begin()+i+vertex*6,
                triangles_.begin()+i+vertex*6+6);
    }
    gpuDirty_=true;
    update();
    return true;
}
void NativeOcctViewport::initializeGL() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    bool ok=shader_.addShaderFromSourceCode(QOpenGLShader::Vertex,kVertex)
         && shader_.addShaderFromSourceCode(QOpenGLShader::Fragment,kFragment);
    if(ok) {
        shader_.bindAttributeLocation("aPos",0);
        shader_.bindAttributeLocation("aNormal",1);
        ok=shader_.link();
    }
    if(!ok) qWarning("DRNAS native GPU shader error: %s",
                      qPrintable(shader_.log()));
    initialized_=ok;
    vbo_.create();
    edgeVbo_.create();
    gpuDirty_=true;
}
void NativeOcctViewport::resizeGL(int w,int h) {
    glViewport(0,0,std::max(1,w),std::max(1,h));
    updateProjections();
}
void NativeOcctViewport::updateProjections() {
    proj_.setToIdentity();
    const float aspect=float(std::max(1,width()))/std::max(1,height());
    proj_.perspective(45.f,aspect,.1f,100.f);
}
void NativeOcctViewport::drawMesh(bool wire) {
    auto& buffer=wire?edgeVbo_:vbo_;
    const auto& vertices=wire?edgeVertices_:triangles_;
    if(vertices.empty() || !buffer.isCreated())return;
    buffer.bind();
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),
                          reinterpret_cast<const void*>(3*sizeof(float)));
    glDrawArrays(wire?GL_LINES:GL_TRIANGLES,0,
                 static_cast<GLsizei>(vertices.size()/6));
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
    buffer.release();
}
void NativeOcctViewport::paintGL() {
    glClearColor(.035f,.063f,.11f,1.f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if(!initialized_ || triangles_.empty()) return;
    if(gpuDirty_) {
        vbo_.bind();
        vbo_.allocate(triangles_.data(),int(triangles_.size()*sizeof(float)));
        vbo_.release();
        edgeVbo_.bind();
        edgeVbo_.allocate(edgeVertices_.data(),
                          int(edgeVertices_.size()*sizeof(float)));
        edgeVbo_.release();
        gpuDirty_=false;
    }
    QMatrix4x4 camera;
    camera.lookAt(QVector3D(0,0,distance_),QVector3D(0,0,0),
                  QVector3D(0,1,0));
    QMatrix4x4 object;
    object.translate(panX_,panY_,0.f);
    object.rotate(pitch_,1,0,0);
    object.rotate(yaw_,0,1,0);
    object.scale(normalization_);
    object.translate(-midpoint_);
    shader_.bind();
    shader_.setUniformValue("uMVP",proj_*camera*object);
    shader_.setUniformValue("uModel",object);
    shader_.setUniformValue("uFlat",0.f);
    if(wire_) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.f,1.f);
    }
    drawMesh(false);
    if(wire_) {
        glDisable(GL_POLYGON_OFFSET_FILL);
        shader_.setUniformValue("uFlat",1.f);
        shader_.setUniformValue("uColor",QVector3D(.64f,.84f,1.f));
        glDepthMask(GL_FALSE);
        drawMesh(true);
        glDepthMask(GL_TRUE);
    }
    if(grid_) {
        std::vector<float> lines;
        // Construction grid is offset below the current CAD object.
        const float z=midpoint_.z()-1.2f/normalization_;
        const float step=2.f/normalization_;
        for(int i=-8;i<=8;i++) {
            const float v=i*step, e=8.f*step;
            for(const QVector3D& pt: {QVector3D(midpoint_.x()+v,midpoint_.y()-e,z),
                QVector3D(midpoint_.x()+v,midpoint_.y()+e,z),
                QVector3D(midpoint_.x()-e,midpoint_.y()+v,z),
                QVector3D(midpoint_.x()+e,midpoint_.y()+v,z)}) {
                lines.insert(lines.end(),{pt.x(),pt.y(),pt.z(),0.f,0.f,1.f});
            }
        }
        QOpenGLBuffer gridBuffer(QOpenGLBuffer::VertexBuffer);
        if(gridBuffer.create()) {
            gridBuffer.bind();
            gridBuffer.allocate(lines.data(),int(lines.size()*sizeof(float)));
            shader_.setUniformValue("uFlat",1.f);
            shader_.setUniformValue("uColor",QVector3D(.14f,.23f,.32f));
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr);
            glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),
                                  reinterpret_cast<const void*>(3*sizeof(float)));
            glDepthMask(GL_FALSE);
            glDrawArrays(GL_LINES,0,GLsizei(lines.size()/6));
            glDepthMask(GL_TRUE);
            glDisableVertexAttribArray(0);
            glDisableVertexAttribArray(1);
            gridBuffer.release();
            gridBuffer.destroy();
        }
    }
    shader_.release();
}
void NativeOcctViewport::setView(int preset) {
    if(preset==1){pitch_=0.f;yaw_=0.f;}
    else if(preset==2){pitch_=-90.f;yaw_=0.f;}
    else if(preset==3){pitch_=0.f;yaw_=-90.f;}
    else {pitch_=-25.f;yaw_=-32.f;}
    panX_=panY_=0.f;
    update();
}
void NativeOcctViewport::setGrid(bool show){grid_=show;update();}
void NativeOcctViewport::setWire(bool show){wire_=show;update();}
void NativeOcctViewport::fit(){distance_=4.5f;panX_=panY_=0.f;update();}
void NativeOcctViewport::mousePressEvent(QMouseEvent* e) {
    lastPointer_=e->position();
    e->accept();
}
void NativeOcctViewport::mouseMoveEvent(QMouseEvent* e) {
    QPointF delta=e->position()-lastPointer_;
    lastPointer_=e->position();
    if(e->buttons() & Qt::LeftButton) {
        pitch_+=float(delta.y())*.55f;
        yaw_+=float(delta.x())*.55f;
        update();
    }
    e->accept();
}
void NativeOcctViewport::wheelEvent(QWheelEvent* e) {
    const float steps=e->angleDelta().y()/120.f;
    distance_=std::clamp(distance_*std::pow(.86f,steps),2.0f,20.f);
    update();e->accept();
}
bool NativeOcctViewport::event(QEvent* e) {
    if(e->type()==QEvent::TouchBegin || e->type()==QEvent::TouchUpdate ||
       e->type()==QEvent::TouchEnd) {
        auto* touch=static_cast<QTouchEvent*>(e);
        const auto& points=touch->points();
        if(points.size()>=2) {
            const QPointF mid=(points[0].position()+points[1].position())*.5;
            const float span=float(QLineF(points[0].position(),
                                           points[1].position()).length());
            if(e->type()==QEvent::TouchBegin || lastPinch_<=0.f) {
                lastPointer_=mid;lastPinch_=span;
            } else {
                const QPointF d=mid-lastPointer_;
                panX_+=float(d.x())*2.7f/std::max(1,height());
                panY_-=float(d.y())*2.7f/std::max(1,height());
                distance_=std::clamp(distance_*lastPinch_/std::max(span,1.f),
                                      2.0f,20.f);
                lastPointer_=mid;lastPinch_=span;
                update();
            }
        } else if(points.size()==1) {
            const QPointF current=points[0].position();
            if(e->type()==QEvent::TouchBegin || lastPinch_>0.f) {
                lastPointer_=current;lastPinch_=0.f;
            } else {
                const QPointF d=current-lastPointer_;
                pitch_+=float(d.y())*.55f;
                yaw_+=float(d.x())*.55f;
                lastPointer_=current;update();
            }
        }
        if(e->type()==QEvent::TouchEnd)lastPinch_=0.f;
        e->accept();return true;
    }
    return QOpenGLWidget::event(e);
}

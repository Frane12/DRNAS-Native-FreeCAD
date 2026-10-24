#pragma once
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QMatrix4x4>
#include <QPointF>
#include <vector>
#include <memory>
class TopoDS_Shape;

// Real Android OpenCASCADE B-Rep solids -> tessellation -> OpenGL ES.
// Full upstream FreeCADGui ViewProvider/Coin3D is not connected yet.
class NativeOcctViewport final : public QOpenGLWidget, protected QOpenGLFunctions {
public:
    explicit NativeOcctViewport(QWidget* parent = nullptr, bool demo = true);
    ~NativeOcctViewport() override;
    bool setShape(int kind);
    bool loadBrep(const QString& path);
    void clearShape();
    void setView(int preset);
    void setGrid(bool show);
    void setWire(bool show);
    void fit();
    int triangleCount() const;
protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    bool event(QEvent* e) override;
private:
    bool setShapeData(const TopoDS_Shape& shape);
    void updateProjections();
    void drawMesh(bool wire);
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    QOpenGLBuffer vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer edgeVbo_{QOpenGLBuffer::VertexBuffer};
    std::vector<float> triangles_, edgeVertices_;
    QVector3D midpoint_{0.f, 0.f, 0.f};
    float normalization_{1.f}, pitch_{-25.f}, yaw_{-32.f};
    float distance_{4.5f}, panX_{0.f}, panY_{0.f};
    QPointF lastPointer_;
    float lastPinch_{0.f};
    bool grid_{true}, wire_{false}, gpuDirty_{true}, initialized_{false};
    QMatrix4x4 proj_;
};

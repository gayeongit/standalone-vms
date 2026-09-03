#ifndef VIDEO_RENDER_WIDGET_H
#define VIDEO_RENDER_WIDGET_H

#include <QImage>
#include <QElapsedTimer>
#include <QMutex>
#include <QOpenGLWidget>
#include <QSharedPointer>
#include <QString>
#include <QtGlobal>

class QShowEvent;
class QHideEvent;

class VideoRenderWidget final : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit VideoRenderWidget(QWidget *parent = nullptr);

public slots:
    void updateFrame(const QImage &frame);
    void updateFrameShared(const QSharedPointer<const QImage> &frame);
    void clearFrame();

protected:
    void paintGL() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QSharedPointer<const QImage> m_frame;
    mutable QMutex m_frameMutex;
    QElapsedTimer m_fpsTimer;
    int m_renderedFramesInWindow = 0;
};

namespace RenderPerfMetrics {
void beginFirstFrameAfterTransition(const QString &context);
quint64 currentFirstFrameGeneration();
void beginFirstLiveFrameAfterTransition(const QString &context);
quint64 currentFirstLiveGeneration();
void clearFirstFrameAfterTransition();
void clearFirstLiveFrameAfterTransition();
void maybeLogFirstLiveFrameAfterTransition(quint64 expectedGeneration);
}

#endif // VIDEO_RENDER_WIDGET_H

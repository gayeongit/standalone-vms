#include "video_render_widget.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QHideEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QShowEvent>

namespace {
struct FirstFrameAfterTransitionState
{
    bool pending = false;
    quint64 generation = 0;
    QString context;
    QElapsedTimer timer;
};

FirstFrameAfterTransitionState &firstFrameAfterTransitionState()
{
    static FirstFrameAfterTransitionState state;
    return state;
}

struct FirstLiveFrameAfterTransitionState
{
    bool pending = false;
    quint64 generation = 0;
    QString context;
    QElapsedTimer timer;
};

FirstLiveFrameAfterTransitionState &firstLiveFrameAfterTransitionState()
{
    static FirstLiveFrameAfterTransitionState state;
    return state;
}

void maybeLogFirstFrameAfterTransition(VideoRenderWidget *widget, quint64 expectedGeneration)
{
    if (!widget || !widget->isVisible() || widget->width() <= 0 || widget->height() <= 0) {
        return;
    }

    FirstFrameAfterTransitionState &state = firstFrameAfterTransitionState();
    if (state.generation != expectedGeneration) {
        return;
    }
    if (!state.pending || !state.timer.isValid() || state.context.isEmpty()) {
        return;
    }

    state.pending = false;
    qInfo().noquote()
        << QString("perf metric=first_frame_after_transition_ms context=%1 value=%2 unit=ms")
              .arg(state.context,
                   QString::number(state.timer.elapsed()));
}
} // namespace

namespace RenderPerfMetrics {
void beginFirstFrameAfterTransition(const QString &context)
{
    FirstFrameAfterTransitionState &state = firstFrameAfterTransitionState();
    ++state.generation;
    state.pending = !context.isEmpty();
    state.context = context;
    if (state.pending) {
        state.timer.start();
    } else {
        state.timer.invalidate();
    }
}

void beginFirstLiveFrameAfterTransition(const QString &context)
{
    FirstLiveFrameAfterTransitionState &state = firstLiveFrameAfterTransitionState();
    ++state.generation;
    state.pending = !context.isEmpty();
    state.context = context;
    if (state.pending) {
        state.timer.start();
    } else {
        state.timer.invalidate();
    }
}

quint64 currentFirstFrameGeneration()
{
    return firstFrameAfterTransitionState().generation;
}

quint64 currentFirstLiveGeneration()
{
    return firstLiveFrameAfterTransitionState().generation;
}

void clearFirstFrameAfterTransition()
{
    FirstFrameAfterTransitionState &state = firstFrameAfterTransitionState();
    ++state.generation;
    state.pending = false;
    state.context.clear();
    state.timer.invalidate();
}

void clearFirstLiveFrameAfterTransition()
{
    FirstLiveFrameAfterTransitionState &state = firstLiveFrameAfterTransitionState();
    ++state.generation;
    state.pending = false;
    state.context.clear();
    state.timer.invalidate();
}

void maybeLogFirstLiveFrameAfterTransition(quint64 expectedGeneration)
{
    FirstLiveFrameAfterTransitionState &state = firstLiveFrameAfterTransitionState();
    if (state.generation != expectedGeneration) {
        return;
    }
    if (!state.pending || !state.timer.isValid() || state.context.isEmpty()) {
        return;
    }
    state.pending = false;
    qInfo().noquote()
        << QString("perf metric=first_live_frame_after_transition_ms context=%1 value=%2 unit=ms")
              .arg(state.context,
                   QString::number(state.timer.elapsed()));
}
} // namespace RenderPerfMetrics

VideoRenderWidget::VideoRenderWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setAutoFillBackground(false);
}

void VideoRenderWidget::updateFrame(const QImage &frame)
{
    QMutexLocker locker(&m_frameMutex);
    m_frame = QSharedPointer<const QImage>::create(frame);
    update();
}

void VideoRenderWidget::updateFrameShared(const QSharedPointer<const QImage> &frame)
{
    bool shouldUpdate = false;
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = frame;
        shouldUpdate = isVisible() && width() > 0 && height() > 0;
    }
    if (shouldUpdate) {
        update();
    }
}

void VideoRenderWidget::clearFrame()
{
    QMutexLocker locker(&m_frameMutex);
    m_frame.reset();
    update();
}

void VideoRenderWidget::paintGL()
{
    QSharedPointer<const QImage> currentFrame;
    {
        QMutexLocker locker(&m_frameMutex);
        currentFrame = m_frame;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), Qt::black);

    if (currentFrame.isNull() || currentFrame->isNull()) {
        return;
    }

    const QSize target = currentFrame->size().scaled(size(), Qt::KeepAspectRatio);
    const QRect drawRect((width() - target.width()) / 2,
                         (height() - target.height()) / 2,
                         target.width(),
                         target.height());
    painter.drawImage(drawRect, *currentFrame);

    maybeLogFirstFrameAfterTransition(this, RenderPerfMetrics::currentFirstFrameGeneration());

    if (!isVisible() || width() <= 0 || height() <= 0) {
        m_fpsTimer.invalidate();
        m_renderedFramesInWindow = 0;
        return;
    }

    if (!m_fpsTimer.isValid()) {
        m_fpsTimer.start();
        m_renderedFramesInWindow = 0;
    }
    ++m_renderedFramesInWindow;

    const qint64 elapsedMs = m_fpsTimer.elapsed();
    if (elapsedMs >= 1000) {
        const double fps = (static_cast<double>(m_renderedFramesInWindow) * 1000.0)
                           / static_cast<double>(elapsedMs);
        qInfo().noquote()
            << QString("perf metric=render_fps context=%1 value=%2 unit=fps")
                  .arg(QString::number(reinterpret_cast<quintptr>(this), 16),
                       QString::number(fps, 'f', 1));
        m_fpsTimer.restart();
        m_renderedFramesInWindow = 0;
    }
}

void VideoRenderWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    m_fpsTimer.invalidate();
    m_renderedFramesInWindow = 0;
    bool hasFrame = false;
    {
        QMutexLocker locker(&m_frameMutex);
        hasFrame = !m_frame.isNull() && !m_frame->isNull();
    }
    if (hasFrame && width() > 0 && height() > 0) {
        update();
    }
}

void VideoRenderWidget::hideEvent(QHideEvent *event)
{
    QOpenGLWidget::hideEvent(event);
    m_fpsTimer.invalidate();
    m_renderedFramesInWindow = 0;
}

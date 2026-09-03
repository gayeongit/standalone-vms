#include "stream_player.h"
#include "video_render_widget.h"

#include <QEvent>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QMetaObject>
#include <QMutexLocker>
#include <QSharedPointer>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace {

constexpr int kReconnectMs = 3000;
constexpr qint64 kHiddenLatestFrameRefreshMs = 100;
const char kRenderOwnerProp[] = "_vms_render_owner";
const char kRenderRetiringProp[] = "_vms_render_retiring";
const char kHostOwnerProp[] = "_vms_host_owner";

qulonglong playerOwnerId(const StreamPlayer *player)
{
    return static_cast<qulonglong>(reinterpret_cast<quintptr>(player));
}

qulonglong ownerIdForObject(const QObject *obj, const char *propName)
{
    if (!obj) {
        return 0;
    }
    bool ok = false;
    const qulonglong value = obj->property(propName).toULongLong(&ok);
    return ok ? value : 0;
}

bool isRenderRetiring(const QObject *obj)
{
    return obj && obj->property(kRenderRetiringProp).toBool();
}

bool shouldDispatchToTarget(const VideoRenderWidget *target)
{
    return target
        && target->isVisible()
        && target->width() > 0
        && target->height() > 0;
}

QString rawCapsForProfile(StreamQualityProfile profile)
{
    QString rawCaps = QStringLiteral("video/x-raw,format=BGRA");
    if (profile == StreamQualityProfile::QuadGrid) {
        rawCaps += QStringLiteral(",width=854,height=480,framerate=10/1");
    } else if (profile == StreamQualityProfile::DenseGrid) {
        rawCaps += QStringLiteral(",width=480,height=270,framerate=6/1");
    }
    return rawCaps;
}

QString profileName(StreamQualityProfile profile)
{
    switch (profile) {
    case StreamQualityProfile::Normal:
        return QStringLiteral("Normal");
    case StreamQualityProfile::QuadGrid:
        return QStringLiteral("QuadGrid");
    case StreamQualityProfile::DenseGrid:
        return QStringLiteral("DenseGrid");
    }
    return QStringLiteral("Unknown");
}

QString normalizedCodecName(const QString &codec)
{
    const QString normalized = codec.trimmed().toUpper();
    if (normalized.contains(QStringLiteral("265")) || normalized.contains(QStringLiteral("HEVC"))) {
        return QStringLiteral("H265");
    }
    if (normalized.contains(QStringLiteral("264")) || normalized.contains(QStringLiteral("AVC"))) {
        return QStringLiteral("H264");
    }
    return {};
}

} // namespace

StreamPlayer::StreamPlayer(QObject *parent)
    : QObject(parent)
{
    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(100);
    connect(m_busTimer, &QTimer::timeout, this, &StreamPlayer::pollBus);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(kReconnectMs);
    connect(m_reconnectTimer, &QTimer::timeout, this, &StreamPlayer::restartAfterFailure);
}

StreamPlayer::~StreamPlayer()
{
    stop();
    clearRenderWidgets();
}

void StreamPlayer::setRenderWidget(QWidget *widget)
{
    clearRenderWidgets();
    bindRenderWidget(widget);
}

void StreamPlayer::bindRenderWidget(QWidget *widget)
{
    if (!widget) {
        return;
    }

    const qulonglong selfOwner = playerOwnerId(this);
    const qulonglong hostOwner = ownerIdForObject(widget, kHostOwnerProp);
    if (hostOwner != 0 && hostOwner != selfOwner) {
        emit errorOccurred("Render host is already owned by another stream session.");
        return;
    }

    QMutexLocker locker(&m_renderBindingsMutex);
    for (const auto &binding : m_renderBindings) {
        if (binding.host == widget) {
            return;
        }
    }

    VideoRenderWidget *target = ensureRenderTargetForHost(widget);
    if (!target) {
        return;
    }

    widget->installEventFilter(this);
    widget->setProperty(kHostOwnerProp, QVariant::fromValue(selfOwner));
    syncRenderTargetGeometry(widget);
    target->show();
    target->lower();

    m_renderBindings.push_back(RenderBinding{widget, target});
}

void StreamPlayer::unbindRenderWidget(QWidget *widget)
{
    if (!widget) {
        return;
    }

    const qulonglong selfOwner = playerOwnerId(this);
    VideoRenderWidget *widgetAsRender = qobject_cast<VideoRenderWidget *>(widget);
    QMutexLocker locker(&m_renderBindingsMutex);
    for (qsizetype i = m_renderBindings.size() - 1; i >= 0; --i) {
        const RenderBinding binding = m_renderBindings[static_cast<int>(i)];
        if (binding.host != widget) {
            continue;
        }
        clearBindingView(binding);
        widget->removeEventFilter(this);
        if (!binding.view.isNull() && ownerIdForObject(binding.view, kRenderOwnerProp) == selfOwner) {
            binding.view->setProperty(kRenderOwnerProp, QVariant());
            if (binding.view == widgetAsRender) {
                // Direct-host render widgets stay alive; only clear ownership.
                binding.view->setProperty(kRenderRetiringProp, false);
            } else {
                // Mark as retiring so rapid rebind paths don't reuse a pending-delete view.
                binding.view->setProperty(kRenderRetiringProp, true);
                binding.view->deleteLater();
            }
        }
        m_renderBindings.removeAt(static_cast<int>(i));
    }

    bool stillBound = false;
    for (const auto &binding : m_renderBindings) {
        if (binding.host == widget) {
            stillBound = true;
            break;
        }
    }
    if (!stillBound && ownerIdForObject(widget, kHostOwnerProp) == selfOwner) {
        widget->setProperty(kHostOwnerProp, QVariant());
    }
}

void StreamPlayer::clearRenderWidgets()
{
    const qulonglong selfOwner = playerOwnerId(this);
    QMutexLocker locker(&m_renderBindingsMutex);
    for (const auto &binding : m_renderBindings) {
        clearBindingView(binding);
        VideoRenderWidget *hostAsRender = qobject_cast<VideoRenderWidget *>(binding.host.data());
        if (!binding.host.isNull()) {
            binding.host->removeEventFilter(this);
            if (ownerIdForObject(binding.host, kHostOwnerProp) == selfOwner) {
                binding.host->setProperty(kHostOwnerProp, QVariant());
            }
        }
        if (!binding.view.isNull() && ownerIdForObject(binding.view, kRenderOwnerProp) == selfOwner) {
            binding.view->setProperty(kRenderOwnerProp, QVariant());
            if (binding.view == hostAsRender) {
                // Direct-host render widgets stay alive; only clear ownership.
                binding.view->setProperty(kRenderRetiringProp, false);
            } else {
                // Mark as retiring so rapid rebind paths don't reuse a pending-delete view.
                binding.view->setProperty(kRenderRetiringProp, true);
                binding.view->deleteLater();
            }
        }
    }
    m_renderBindings.clear();
    {
        QMutexLocker dispatchLocker(&m_renderDispatchMutex);
        m_renderDispatchPending.clear();
    }
}

void StreamPlayer::setSource(const QString &url)
{
    // source URL 변경은 단순 값 대입이 아니라 "파이프라인 재구성 필요 여부"를 결정한다.
    // 새 URL을 저장하고, 현재 재생 중이라면 start()를 다시 타서 pipeline을 새 source 기준으로 재구성한다.
    // 현재 재생 중/연결 중이면 start()를 다시 타게 해 새 source와 codec/profile 힌트를
    // 반영한 파이프라인을 재생성한다.
    if (m_sourceUrl == url) {
        return;
    }
    m_sourceUrl = url;
    {
        QMutexLocker locker(&m_negotiatedCapsLogMutex);
        m_lastLoggedNegotiatedCaps.clear();
    }
    if (m_status == StreamStatus::Playing || m_status == StreamStatus::Connecting) {
        start();
    }
}

void StreamPlayer::setVideoCodecHint(const QString &codec)
{
    const QString normalized = normalizedCodecName(codec);
    if (m_videoCodecHint == normalized) {
        return;
    }
    m_videoCodecHint = normalized;
    if (m_status == StreamStatus::Playing || m_status == StreamStatus::Connecting) {
        start();
    }
}

void StreamPlayer::setQualityProfile(StreamQualityProfile profile)
{
    if (m_qualityProfile == profile) {
        return;
    }

    const bool canTryDynamicCaps = (m_status == StreamStatus::Playing || m_status == StreamStatus::Connecting)
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
        && m_pipeline
        && m_qualityCapsFilter
#endif
        ;
    if (canTryDynamicCaps && applyQualityProfileCaps(profile)) {
        m_qualityProfile = profile;
        qInfo().noquote()
            << QString("perf metric=quality_profile_caps_update context=source:%1 value=1 unit=apply profile=%2")
                  .arg(m_sourceUrl.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)
                           ? QStringLiteral("rtsp")
                           : QStringLiteral("playback"),
                       profileName(profile));
        return;
    }

    m_qualityProfile = profile;
    if (m_status == StreamStatus::Playing || m_status == StreamStatus::Connecting) {
        start();
    }
}

void StreamPlayer::start()
{
    // StreamPlayer의 start()는 player 상태를 PLAYING으로 바꾸는 얇은 래퍼가 아니다.
    // source/profile/codec 힌트에 맞는 GStreamer pipeline을 새로 만들고 appsink까지 연결해 재생을 시작한다.
    // 실제로는:
    // - 기존 GStreamer pipeline 파기
    // - source/profile/codec에 맞는 pipeline string 생성
    // - appsink/capsfilter 확보
    // - PLAYING 전이와 reconnect 경로 설정
    // 까지 모두 담당하는 파이프라인 부트스트랩 함수다.
    m_manualStop = false;
    m_paused = false;
    m_reconnectTimer->stop();

    if (m_sourceUrl.isEmpty()) {
        setStatus(StreamStatus::Error);
        emit errorOccurred("RTSP URL is empty.");
        return;
    }

#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    destroyPipeline();

    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    setStatus(StreamStatus::Connecting);

    QString pipelineDesc;
    const QString rawCaps = rawCapsForProfile(m_qualityProfile);
    const QString rtspVideoChain = QString(
        "videoconvert ! videoscale ! videorate ! capsfilter name=qualitycaps caps=\"%1\" ! "
        "appsink name=appsink emit-signals=false sync=false max-buffers=1 drop=true")
                                       .arg(rawCaps);
    const QString playbackVideoChain = QString(
        "videoconvert ! videoscale ! videorate ! capsfilter name=qualitycaps caps=\"%1\" ! "
        "appsink name=appsink emit-signals=false sync=true max-buffers=1 drop=true")
                                           .arg(rawCaps);
    if (m_sourceUrl.startsWith("rtsp://", Qt::CaseInsensitive)) {
        if (m_videoCodecHint == QStringLiteral("H265")) {
            pipelineDesc = QString(
                               "rtspsrc location=\"%1\" latency=120 protocols=tcp ! "
                               "rtph265depay ! h265parse ! avdec_h265 ! %2")
                               .arg(m_sourceUrl, rtspVideoChain);
        } else if (m_videoCodecHint == QStringLiteral("H264")) {
            pipelineDesc = QString(
                               "rtspsrc location=\"%1\" latency=120 protocols=tcp ! "
                               "rtph264depay ! h264parse ! avdec_h264 ! %2")
                               .arg(m_sourceUrl, rtspVideoChain);
        } else {
            pipelineDesc = QString(
                               "uridecodebin uri=\"%1\" source::latency=120 source::protocols=tcp ! %2")
                               .arg(m_sourceUrl, rtspVideoChain);
        }
    } else {
        QString uri = m_sourceUrl;
        if (uri.startsWith("http://", Qt::CaseInsensitive) || uri.startsWith("https://", Qt::CaseInsensitive)) {
            // Playback URLs returned by the server are consumed as-is via uridecodebin.
        } else {
            QString filePath = m_sourceUrl;
            if (filePath.startsWith("file://", Qt::CaseInsensitive)) {
                filePath = QUrl(filePath).toLocalFile();
            }
            if (!QFileInfo::exists(filePath)) {
                setStatus(StreamStatus::Error);
                emit errorOccurred(QString("Playback file not found: %1").arg(filePath));
                return;
            }
            uri = QUrl::fromLocalFile(filePath).toString();
        }
        pipelineDesc = QString(
                           "uridecodebin uri=\"%1\" ! %2")
                           .arg(uri, playbackVideoChain);
    }

    GError *error = nullptr;
    m_pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &error);
    if (!m_pipeline) {
        const QString msg = error ? QString::fromUtf8(error->message) : QString("Failed to create pipeline.");
        if (error) {
            g_error_free(error);
        }
        setStatus(StreamStatus::Error);
        emit errorOccurred(msg);
        m_reconnectTimer->start();
        return;
    }
    if (error) {
        g_error_free(error);
    }

    m_appSink = gst_bin_get_by_name(GST_BIN(m_pipeline), "appsink");
    if (!m_appSink) {
        setStatus(StreamStatus::Error);
        emit errorOccurred("Failed to find appsink from pipeline.");
        destroyPipeline();
        m_reconnectTimer->start();
        return;
    }

    m_qualityCapsFilter = gst_bin_get_by_name(GST_BIN(m_pipeline), "qualitycaps");
    if (!m_qualityCapsFilter) {
        setStatus(StreamStatus::Error);
        emit errorOccurred("Failed to find quality caps filter from pipeline.");
        destroyPipeline();
        m_reconnectTimer->start();
        return;
    }

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &StreamPlayer::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(m_appSink), &callbacks, this, nullptr);

    const GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        setStatus(StreamStatus::Error);
        emit errorOccurred("Failed to set pipeline to PLAYING.");
        destroyPipeline();
        m_reconnectTimer->start();
        return;
    }

    m_busTimer->start();
#else
    setStatus(StreamStatus::Error);
    emit errorOccurred("GStreamer is not enabled in this build.");
#endif
}

void StreamPlayer::stop()
{
    // stop()은 렌더 정지뿐 아니라 "이 player가 들고 있는 마지막 프레임 상태"도 비운다.
    // 파이프라인을 파괴하고, 렌더 타깃을 clear하고, 마지막 frame/dispatch cache까지 정리한다.
    // 그래야 화면 전환/로그아웃 뒤에 이전 채널의 잔상이 남지 않고,
    // 다음 start()도 깨끗한 Idle 상태에서 시작할 수 있다.
    m_manualStop = true;
    m_reconnectTimer->stop();
    m_busTimer->stop();
    destroyPipeline();

    const auto targets = snapshotRenderTargets();
    for (const auto &target : targets) {
        if (!target.isNull()) {
            target->clearFrame();
        }
    }
    {
        QMutexLocker locker(&m_latestFrameMutex);
        m_latestFrame.reset();
    }
    {
        QMutexLocker locker(&m_negotiatedCapsLogMutex);
        m_lastLoggedNegotiatedCaps.clear();
    }
    m_lastHiddenLatestFrameUpdateMs = 0;
    {
        QMutexLocker dispatchLocker(&m_renderDispatchMutex);
        m_renderDispatchPending.clear();
    }
    setStatus(StreamStatus::Idle);
}

void StreamPlayer::setPaused(bool paused)
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (paused == m_paused) {
        return;
    }

    m_paused = paused;
    if (!m_pipeline) {
        return;
    }
    gst_element_set_state(m_pipeline, paused ? GST_STATE_PAUSED : GST_STATE_PLAYING);
#else
    Q_UNUSED(paused);
#endif
}

bool StreamPlayer::isPaused() const
{
    return m_paused;
}

void StreamPlayer::seekToMs(qint64 positionMs)
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return;
    }
    const gint64 pos = static_cast<gint64>(std::max<qint64>(0, positionMs)) * GST_MSECOND;
    gst_element_seek_simple(
        m_pipeline,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
        pos);
#else
    Q_UNUSED(positionMs);
#endif
}

void StreamPlayer::setPlaybackRate(double rate)
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return;
    }
    if (rate <= 0.0) {
        rate = 1.0;
    }
    const qint64 currentPosMs = positionMs();
    const gint64 pos = static_cast<gint64>(std::max<qint64>(0, currentPosMs)) * GST_MSECOND;
    gst_element_seek(
        m_pipeline,
        rate,
        GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
        GST_SEEK_TYPE_SET,
        pos,
        GST_SEEK_TYPE_NONE,
        GST_CLOCK_TIME_NONE);
#else
    Q_UNUSED(rate);
#endif
}

qint64 StreamPlayer::positionMs() const
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return 0;
    }
    gint64 pos = 0;
    if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos)) {
        return 0;
    }
    return static_cast<qint64>(pos / GST_MSECOND);
#else
    return 0;
#endif
}

qint64 StreamPlayer::durationMs() const
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return 0;
    }
    gint64 dur = 0;
    if (!gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur)) {
        return 0;
    }
    return static_cast<qint64>(dur / GST_MSECOND);
#else
    return 0;
#endif
}

bool StreamPlayer::canSeek() const
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return false;
    }
    GstQuery *query = gst_query_new_seeking(GST_FORMAT_TIME);
    if (!query) {
        return false;
    }

    gboolean seekable = FALSE;
    const gboolean ok = gst_element_query(m_pipeline, query);
    if (ok) {
        gst_query_parse_seeking(query, nullptr, &seekable, nullptr, nullptr);
    }
    gst_query_unref(query);
    return ok && seekable;
#else
    return false;
#endif
}

bool StreamPlayer::canChangePlaybackRate() const
{
    // Be conservative until playback protocol-specific rate support is verified.
    if (!canSeek()) {
        return false;
    }
    if (m_sourceUrl.startsWith("rtsp://", Qt::CaseInsensitive)
        || m_sourceUrl.startsWith("http://", Qt::CaseInsensitive)
        || m_sourceUrl.startsWith("https://", Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

StreamStatus StreamPlayer::status() const
{
    return m_status;
}

QSharedPointer<const QImage> StreamPlayer::latestFrameShared() const
{
    QMutexLocker locker(&m_latestFrameMutex);
    return m_latestFrame;
}

QImage StreamPlayer::latestFrameCopy() const
{
    const auto frame = latestFrameShared();
    if (frame.isNull()) {
        return {};
    }
    return *frame;
}

void StreamPlayer::pollBus()
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_pipeline) {
        return;
    }

    GstBus *bus = gst_element_get_bus(m_pipeline);
    if (!bus) {
        return;
    }

    while (true) {
        GstMessage *msg = gst_bus_pop_filtered(
            bus,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED));
        if (!msg) {
            break;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = nullptr;
            gchar *debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            const QString message = err ? QString::fromUtf8(err->message) : QString("Stream error");
            if (err) {
                g_error_free(err);
            }
            if (debug) {
                g_free(debug);
            }
            if (message.contains("Output window was closed", Qt::CaseInsensitive)) {
                destroyPipeline();
                break;
            }
            setStatus(StreamStatus::Error);
            emit errorOccurred(message);
            destroyPipeline();
            if (!m_manualStop) {
                m_reconnectTimer->start();
            }
            break;
        }
        case GST_MESSAGE_EOS:
            emit eosReached();
            if (m_sourceUrl.startsWith("rtsp://", Qt::CaseInsensitive)) {
                setStatus(StreamStatus::Error);
                emit errorOccurred("RTSP stream ended unexpectedly.");
                destroyPipeline();
                if (!m_manualStop) {
                    m_reconnectTimer->start();
                }
                break;
            }
            setStatus(StreamStatus::Idle);
            destroyPipeline();
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                GstState oldState = GST_STATE_NULL;
                GstState newState = GST_STATE_NULL;
                GstState pending = GST_STATE_NULL;
                gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
                Q_UNUSED(oldState);
                Q_UNUSED(pending);
                if (newState == GST_STATE_PLAYING) {
                    setStatus(StreamStatus::Playing);
                } else if (newState == GST_STATE_PAUSED || newState == GST_STATE_READY) {
                    setStatus(StreamStatus::Connecting);
                }
            }
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_object_unref(bus);
#endif
}

void StreamPlayer::restartAfterFailure()
{
    if (m_manualStop) {
        return;
    }
    start();
}

void StreamPlayer::setStatus(StreamStatus status)
{
    if (m_status == status) {
        return;
    }
    m_status = status;
    emit statusChanged(m_status);
}

void StreamPlayer::destroyPipeline()
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    {
        QMutexLocker locker(&m_negotiatedCapsLogMutex);
        m_lastLoggedNegotiatedCaps.clear();
    }
    if (m_appSink) {
        GstAppSinkCallbacks clearCallbacks = {};
        gst_app_sink_set_callbacks(GST_APP_SINK(m_appSink), &clearCallbacks, nullptr, nullptr);
        gst_object_unref(m_appSink);
        m_appSink = nullptr;
    }
    if (m_qualityCapsFilter) {
        gst_object_unref(m_qualityCapsFilter);
        m_qualityCapsFilter = nullptr;
    }
    if (!m_pipeline) {
        return;
    }
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    gst_object_unref(m_pipeline);
    m_pipeline = nullptr;
#endif
}

bool StreamPlayer::applyQualityProfileCaps(StreamQualityProfile profile)
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!m_qualityCapsFilter) {
        return false;
    }

    const QByteArray capsText = rawCapsForProfile(profile).toUtf8();
    GstCaps *caps = gst_caps_from_string(capsText.constData());
    if (!caps) {
        return false;
    }

    g_object_set(G_OBJECT(m_qualityCapsFilter), "caps", caps, nullptr);
    gst_caps_unref(caps);
    return true;
#else
    Q_UNUSED(profile);
    return false;
#endif
}

VideoRenderWidget *StreamPlayer::ensureRenderTargetForHost(QWidget *host)
{
    if (!host) {
        return nullptr;
    }

    const qulonglong selfOwner = playerOwnerId(this);

    if (auto *view = qobject_cast<VideoRenderWidget *>(host)) {
        if (isRenderRetiring(view)) {
            return nullptr;
        }
        const qulonglong owner = ownerIdForObject(view, kRenderOwnerProp);
        if (owner != 0 && owner != selfOwner) {
            emit errorOccurred("Render widget is already owned by another stream session.");
            return nullptr;
        }
        view->setProperty(kRenderOwnerProp, QVariant::fromValue(selfOwner));
        view->setProperty(kRenderRetiringProp, false);
        view->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        return view;
    }

    for (auto *child : host->findChildren<VideoRenderWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (isRenderRetiring(child)) {
            continue;
        }
        const qulonglong owner = ownerIdForObject(child, kRenderOwnerProp);
        if (owner == selfOwner) {
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            return child;
        }
    }

    for (auto *child : host->findChildren<VideoRenderWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (isRenderRetiring(child)) {
            continue;
        }
        const qulonglong owner = ownerIdForObject(child, kRenderOwnerProp);
        if (owner != 0 && owner != selfOwner) {
            emit errorOccurred("Render widget is already owned by another stream session.");
            return nullptr;
        }
    }

    for (auto *child : host->findChildren<VideoRenderWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (isRenderRetiring(child)) {
            continue;
        }
        const qulonglong owner = ownerIdForObject(child, kRenderOwnerProp);
        if (owner == 0) {
            child->setProperty(kRenderOwnerProp, QVariant::fromValue(selfOwner));
            child->setProperty(kRenderRetiringProp, false);
            child->setObjectName("videoRenderWidget");
            child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            return child;
        }
    }

    auto *view = new VideoRenderWidget(host);
    view->setObjectName("videoRenderWidget");
    view->setProperty(kRenderOwnerProp, QVariant::fromValue(selfOwner));
    view->setProperty(kRenderRetiringProp, false);
    view->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    view->setGeometry(host->rect());
    view->show();
    view->lower();
    return view;
}

void StreamPlayer::syncRenderTargetGeometry(QWidget *host)
{
    if (!host) {
        return;
    }

    for (const auto &binding : m_renderBindings) {
        if (binding.host != host || binding.view.isNull()) {
            continue;
        }
        binding.view->setGeometry(host->rect());
        binding.view->lower();
        return;
    }
}

void StreamPlayer::clearBindingView(const RenderBinding &binding)
{
    if (binding.view.isNull()) {
        return;
    }
    QPointer<VideoRenderWidget> target = binding.view;
    const qulonglong selfOwner = playerOwnerId(this);
    QMetaObject::invokeMethod(
        target,
        [target, selfOwner]() {
            if (!target.isNull() && ownerIdForObject(target, kRenderOwnerProp) == selfOwner) {
                target->clearFrame();
            }
        },
        Qt::QueuedConnection);
}

QVector<QPointer<VideoRenderWidget>> StreamPlayer::snapshotRenderTargets() const
{
    QMutexLocker locker(&m_renderBindingsMutex);
    QVector<QPointer<VideoRenderWidget>> out;
    out.reserve(m_renderBindings.size());
    for (const auto &binding : m_renderBindings) {
        if (!binding.view.isNull()) {
            out.push_back(binding.view);
        }
    }
    return out;
}

void StreamPlayer::publishSampleFrame(struct _GstSample *sample)
{
#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
    if (!sample) {
        return;
    }

    const auto targets = snapshotRenderTargets();
    if (targets.isEmpty()) {
        return;
    }
    bool hasDispatchableTarget = false;
    for (const auto &target : targets) {
        if (!target.isNull() && shouldDispatchToTarget(target)) {
            hasDispatchableTarget = true;
            break;
        }
    }
    const quint64 expectedFirstLiveGeneration = hasDispatchableTarget
                                                    ? RenderPerfMetrics::currentFirstLiveGeneration()
                                                    : 0;
    if (!hasDispatchableTarget) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        if (m_lastHiddenLatestFrameUpdateMs > 0
            && (nowMs - m_lastHiddenLatestFrameUpdateMs) < kHiddenLatestFrameRefreshMs) {
            return;
        }
        m_lastHiddenLatestFrameUpdateMs = nowMs;
    } else {
        m_lastHiddenLatestFrameUpdateMs = 0;
    }
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstCaps *caps = gst_sample_get_caps(sample);
    if (!buffer || !caps) {
        return;
    }

    GstStructure *s = gst_caps_get_structure(caps, 0);
    if (!s) {
        return;
    }

    int width = 0;
    int height = 0;
    if (!gst_structure_get_int(s, "width", &width) || !gst_structure_get_int(s, "height", &height)) {
        return;
    }
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        return;
    }

    int fpsNum = 0;
    int fpsDen = 1;
    const bool hasFramerate = gst_structure_get_fraction(s, "framerate", &fpsNum, &fpsDen);
    const QString fpsText = (hasFramerate && fpsNum > 0 && fpsDen > 0)
        ? QStringLiteral("%1/%2").arg(fpsNum).arg(fpsDen)
        : QStringLiteral("unknown");
    const QString negotiatedCapsKey = QStringLiteral("%1|%2|%3|%4")
        .arg(profileName(m_qualityProfile))
        .arg(width)
        .arg(height)
        .arg(fpsText);
    bool shouldLogNegotiatedCaps = false;
    {
        QMutexLocker locker(&m_negotiatedCapsLogMutex);
        if (m_lastLoggedNegotiatedCaps != negotiatedCapsKey) {
            m_lastLoggedNegotiatedCaps = negotiatedCapsKey;
            shouldLogNegotiatedCaps = true;
        }
    }
    if (shouldLogNegotiatedCaps) {
        qInfo().noquote()
            << QString("perf metric=negotiated_video_caps context=source:%1,profile:%2 value=%3x%4 unit=frame framerate=%5")
                  .arg(m_sourceUrl.startsWith(QStringLiteral("rtsp://"), Qt::CaseInsensitive)
                           ? QStringLiteral("rtsp")
                           : QStringLiteral("playback"),
                       profileName(m_qualityProfile))
                  .arg(width)
                  .arg(height)
                  .arg(fpsText);
    }

    GstMapInfo mapInfo;
    if (!gst_buffer_map(buffer, &mapInfo, GST_MAP_READ)) {
        return;
    }

    const int bytesPerLine = width * 4;
    if (bytesPerLine <= 0) {
        gst_buffer_unmap(buffer, &mapInfo);
        return;
    }
    const qint64 requiredSize = static_cast<qint64>(bytesPerLine) * static_cast<qint64>(height);
    if (requiredSize <= 0 || static_cast<qint64>(mapInfo.size) < requiredSize) {
        gst_buffer_unmap(buffer, &mapInfo);
        return;
    }
    QImage frame(reinterpret_cast<const uchar *>(mapInfo.data), width, height, bytesPerLine, QImage::Format_ARGB32);
    auto sharedFrame = QSharedPointer<const QImage>::create(frame.copy());
    gst_buffer_unmap(buffer, &mapInfo);
    {
        QMutexLocker locker(&m_latestFrameMutex);
        m_latestFrame = sharedFrame;
    }
    if (hasDispatchableTarget) {
        RenderPerfMetrics::maybeLogFirstLiveFrameAfterTransition(expectedFirstLiveGeneration);
    }

    for (const auto &target : targets) {
        if (target.isNull()) {
            continue;
        }
        QPointer<VideoRenderWidget> safeTarget = target;
        if (!shouldDispatchToTarget(safeTarget)) {
            continue;
        }
        const quintptr targetKey = reinterpret_cast<quintptr>(safeTarget.data());
        {
            QMutexLocker dispatchLocker(&m_renderDispatchMutex);
            if (m_renderDispatchPending.contains(targetKey)) {
                continue;
            }
            m_renderDispatchPending.insert(targetKey);
        }
        QPointer<StreamPlayer> self(this);
        const qulonglong selfOwner = playerOwnerId(this);
        const bool queued = QMetaObject::invokeMethod(
            safeTarget,
            [safeTarget, sharedFrame, self, targetKey, selfOwner]() {
                if (!safeTarget.isNull()
                    && ownerIdForObject(safeTarget, kRenderOwnerProp) == selfOwner
                    && shouldDispatchToTarget(safeTarget)) {
                    safeTarget->updateFrameShared(sharedFrame);
                }
                if (!self.isNull()) {
                    QMutexLocker dispatchLocker(&self->m_renderDispatchMutex);
                    self->m_renderDispatchPending.remove(targetKey);
                }
            },
            Qt::QueuedConnection);
        if (!queued) {
            QMutexLocker dispatchLocker(&m_renderDispatchMutex);
            m_renderDispatchPending.remove(targetKey);
        }
    }
#else
    Q_UNUSED(sample);
#endif
}

bool StreamPlayer::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *host = qobject_cast<QWidget *>(watched);
    if (host) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Move:
            syncRenderTargetGeometry(host);
            break;
        case QEvent::Destroy:
            unbindRenderWidget(host);
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

#if defined(VMS_WITH_GSTREAMER) && VMS_WITH_GSTREAMER
GstFlowReturn StreamPlayer::onNewSample(GstAppSink *sink, gpointer userData)
{
    auto *self = static_cast<StreamPlayer *>(userData);
    if (!self) {
        return GST_FLOW_ERROR;
    }

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_OK;
    }

    self->publishSampleFrame(sample);
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
#endif

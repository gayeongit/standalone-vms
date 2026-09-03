#include "playback_screen.h"

#include "common_widgets.h"
#include "common_ui.h"
#include "playback_screen_helpers.h"
#include "popup_manager.h"
#include "stream_player.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTime>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <QHash>

using namespace PlaybackScreenHelpers;

namespace {
constexpr qint64 kPlaybackDayStartMs = 0;
constexpr qint64 kPlaybackDayEndMs = 24LL * 60LL * 60LL * 1000LL;
constexpr qint64 kPlaybackMarkerClusterWindowMs = 3LL * 60LL * 1000LL;

// Timeline 마커가 너무 촘촘하면 UI가 무거워지고 클릭성도 떨어진다.
// Playback에서는 같은 채널/같은 이벤트 타입이 짧은 시간 안에 반복되는 경우가 많아서,
// 3분 이내 반복 마커는 첫 마커만 남겨 타임라인 가독성과 성능을 함께 확보한다.
QVector<PlaybackMarker> clusterPlaybackMarkers(const QVector<PlaybackMarker> &markers, const QString &date)
{
    // raw marker 목록을 시간순으로 정렬한 뒤, 같은 채널/같은 타입이 3분 안에 반복되면 하나로 압축한다.
    if (markers.size() <= 1) {
        return markers;
    }

    struct MarkerWithTime {
        PlaybackMarker marker;
        qint64 timestampMs = -1;
    };

    QVector<MarkerWithTime> timedMarkers;
    timedMarkers.reserve(markers.size());
    for (const auto &marker : markers) {
        timedMarkers.push_back(MarkerWithTime{marker, markerTimestampMs(date, marker.ts)});
    }

    std::stable_sort(timedMarkers.begin(), timedMarkers.end(), [](const MarkerWithTime &lhs, const MarkerWithTime &rhs) {
        if (lhs.timestampMs < 0 && rhs.timestampMs < 0) {
            return false;
        }
        if (lhs.timestampMs < 0) {
            return false;
        }
        if (rhs.timestampMs < 0) {
            return true;
        }
        return lhs.timestampMs < rhs.timestampMs;
    });

    QVector<PlaybackMarker> clustered;
    clustered.reserve(timedMarkers.size());
    QHash<QString, qint64> lastAcceptedByKey;
    for (const auto &entry : timedMarkers) {
        if (entry.timestampMs < 0) {
            clustered.push_back(entry.marker);
            continue;
        }

        const QString key = QStringLiteral("%1|%2")
                                .arg(entry.marker.channelId)
                                .arg(entry.marker.type.trimmed().toUpper());
        const auto it = lastAcceptedByKey.constFind(key);
        if (it != lastAcceptedByKey.cend() && (entry.timestampMs - *it) < kPlaybackMarkerClusterWindowMs) {
            continue;
        }

        lastAcceptedByKey.insert(key, entry.timestampMs);
        clustered.push_back(entry.marker);
    }

    return clustered;
}
}

void PlaybackScreen::startPlaybackForChannel(int channelId, const QString &channelName, const QString &date)
{
    loadTimelineForChannel(channelId, channelName, date, true);
}

void PlaybackScreen::loadTimelineForChannel(int channelId, const QString &channelName, const QString &date, bool autoStart)
{
    // Playback 진입의 첫 단계다.
    // 선택한 channel/date 조합의 timeline을 서버에 요청하고, 최신 요청만 UI에 반영하도록 generation으로 보호한다.
    // 이 함수는 "선택한 채널/날짜"에 대한 timeline만 요청하고,
    // 결과 적용은 generation guard를 거쳐 최신 요청만 반영한다.
    // 즉 사용자가 트리를 빠르게 바꿔도 이전 응답이 UI를 덮지 않게 한다.
    if (!m_playbackService) {
        if (!m_timelineServiceDisconnectPopupShown) {
            PopupManager::showInfo(this, "이전영상", "PlaybackService가 연결되지 않았습니다.");
            m_timelineServiceDisconnectPopupShown = true;
        } else {
            showActionStatus(m_actionStatusLabel, "PlaybackService 미연결", "error", 2200);
        }
        return;
    }
    m_timelineServiceDisconnectPopupShown = false;
    ++m_timelineLoadGeneration;
    const int generation = m_timelineLoadGeneration;
    showActionStatus(m_actionStatusLabel, "타임라인 조회 중...", "progress");
    m_playbackService->fetchTimeline(channelId, date, this, [this, generation, channelName, autoStart](const PlaybackTimelineResult &result) {
        if (generation != m_timelineLoadGeneration) {
            return;
        }
        applyTimelineResult(result, channelName, autoStart);
    });
}

void PlaybackScreen::applyTimelineResult(const PlaybackTimelineResult &result, const QString &channelName, bool autoStart)
{
    // Timeline 응답을 화면 상태로 투영하는 중심 함수다.
    // available range, marker, 현재 시각 기준값을 갱신하고 필요하면 바로 첫 재생 요청까지 이어진다.
    // 여기서 24h span, playable ranges, marker list, 현재 선택 시각,
    // 재생 시작 기준 시각을 분리해 두어야 이후 refreshTimelineUi()가 흔들리지 않는다.
    if (!result.ok) {
        const QString detail = result.errorMessage.trimmed();
        const QString message = detail.isEmpty()
            ? QStringLiteral("타임라인 조회 실패")
            : QStringLiteral("타임라인 조회 실패: %1").arg(detail);
        showActionStatus(m_actionStatusLabel, message, "error", 2800);
        return;
    }

    m_currentPlaybackChannelId = result.channelId;
    m_currentPlaybackChannelName = channelName;
    m_selectedPlaybackDate = result.date;
    highlightSidebarPlaybackItem(m_playbackTree, m_currentPlaybackChannelId, m_currentPlaybackChannelName);
    m_availableRanges = result.availableRanges;
    m_timelineMarkers = clusterPlaybackMarkers(result.eventMarkers, result.date);
    m_timelineSpanStartMs = kPlaybackDayStartMs;
    m_timelineSpanEndMs = kPlaybackDayEndMs;
    m_rateSupported = false;
    m_selectedTimelinePositionMs = result.availableRanges.isEmpty()
        ? -1
        : timeTextToDayMs(result.availableRanges.first().from);
    m_playbackStartTimelinePositionMs = m_selectedTimelinePositionMs;
    m_currentTimelinePositionMs = m_selectedTimelinePositionMs >= 0
        ? m_selectedTimelinePositionMs
        : m_timelineSpanStartMs;
    m_playbackStartWallclockMs = -1;
    m_speedIndex = 1;
    m_speedButton->setText(rateText(playbackRates()[m_speedIndex]));
    m_playPauseButton->setText(">");
    applyPlaybackCapabilities();
    m_playbackFileLabel->setText(QString("[%1] %2 타임라인 로드됨").arg(channelName, result.date));
    m_currentTimeLabel->setText(timeTextForDayMs(m_currentTimelinePositionMs));
    m_totalTimeLabel->setText(timeTextForDayMs(m_timelineSpanEndMs));
    if (m_timelineSlider) {
        m_timelineSlider->setValue(sliderValueForTimelinePosition(
            m_currentTimelinePositionMs,
            m_timelineSpanStartMs,
            m_timelineSpanEndMs));
    }
    updateTimelineHandle();
    rebuildEventMarkers();
    clearActionStatus(m_actionStatusLabel);

    if (autoStart) {
        const QString ts = defaultPlaybackTimestamp(result.date, result.availableRanges);
        if (ts.isEmpty()) {
            showActionStatus(m_actionStatusLabel, "재생 가능한 구간 없음", "error", 2500);
            return;
        }
        requestPlaybackStream(result.channelId, channelName, ts, true);
    }
}

void PlaybackScreen::requestPlaybackStream(int channelId, const QString &channelName, const QString &timestamp, bool autoStart)
{
    // Timeline이 "무엇을 재생할 수 있는지"라면, 이 함수는 "실제로 어떤 URL로 재생할지"를 결정한다.
    // server에서 playback URL을 받아 player source를 교체하고 playback 기준 시각을 새로 잡는다.
    // 트리 클릭은 available range의 from 시각, 이벤트 마커 클릭은 marker 시각을 넣어 호출하며,
    // 성공 시 player source를 교체하고 playback 기준 시각도 함께 갱신한다.
    if (!m_playbackService || m_streamRequestInFlight) {
        return;
    }
    m_streamRequestInFlight = true;
    showActionStatus(m_actionStatusLabel, "재생 URL 요청 중...", "progress");
    m_playbackService->requestStream(channelId, timestamp, this, [this, channelName, timestamp, autoStart](const PlaybackStreamResult &result) {
        m_streamRequestInFlight = false;
        if (!result.ok) {
            const QString detail = result.errorMessage.trimmed();
            const QString message = detail.isEmpty()
                ? QStringLiteral("재생 URL 요청 실패")
                : QStringLiteral("재생 URL 요청 실패: %1").arg(detail);
            showActionStatus(m_actionStatusLabel, message, "error", 2800);
            return;
        }

        m_currentPlaybackSource = result.absoluteUri;
        m_currentPlaybackChannelId = result.channelId;
        m_currentPlaybackChannelName = channelName;
        m_rateSupported = false;
        const qint64 requestedAbsoluteMs = timelinePositionFromTimestamp(timestamp);
        if (requestedAbsoluteMs >= 0) {
            m_selectedTimelinePositionMs = requestedAbsoluteMs;
            m_playbackStartTimelinePositionMs = requestedAbsoluteMs;
            m_currentTimelinePositionMs = requestedAbsoluteMs;
        } else {
            m_playbackStartTimelinePositionMs = m_timelineSpanStartMs;
            m_currentTimelinePositionMs = m_timelineSpanStartMs;
        }
        m_playbackStartWallclockMs = -1;
        m_player->stop();
        m_player->setRenderWidget(m_videoHost);
        m_player->setSource(m_currentPlaybackSource);
        if (autoStart) {
            m_player->start();
        }
        applyPlaybackCapabilities();
        m_playbackFileLabel->setText(QString("[%1] %2").arg(channelName, displayNameFromSource(m_currentPlaybackSource)));
        m_playPauseButton->setText(autoStart ? "||" : ">");
        showActionStatus(m_actionStatusLabel, "재생 URL 준비됨", "success", 1500);
    });
}

void PlaybackScreen::applyPlaybackCapabilities()
{
    m_rateSupported = false;

    if (m_timelineSlider) {
        m_timelineSlider->setEnabled(!m_availableRanges.isEmpty());
        m_timelineSlider->setToolTip(
            m_availableRanges.isEmpty()
                ? QStringLiteral("재생 가능한 타임라인이 없습니다")
                : QStringLiteral("이벤트 마커 클릭으로만 이동할 수 있습니다"));
    }
    if (m_speedButton) {
        m_speedButton->setEnabled(false);
        m_speedButton->setToolTip(QStringLiteral("현재 스트림은 배속 미지원"));
    }
}

void PlaybackScreen::refreshTimelineUi()
{
    // Timeline UI는 실제 player position과 "현재 playback이 시작된 절대 시각"을 합쳐 계산한다.
    // 결과적으로 현재 시각 라벨, 전체 시각 라벨, 슬라이더 값, 핸들 위치를 같이 갱신한다.
    // 즉 슬라이더/현재 시각 라벨은 clip 내부 position이 아니라,
    // 선택된 날짜의 24시간 축 위에서 지금 어디를 보고 있는지를 보여주는 역할이다.
    if (!m_timelineSlider || !m_currentTimeLabel || !m_totalTimeLabel || !m_player) {
        return;
    }

    const qint64 durationMs = m_player->durationMs();
    const qint64 positionMs = m_player->positionMs();
    const qint64 safeDurationMs = std::max<qint64>(0, durationMs);
    const qint64 safePositionMs = std::clamp<qint64>(positionMs, 0, safeDurationMs);

    if (!m_availableRanges.isEmpty()) {
        qint64 absoluteMs = m_playbackStartTimelinePositionMs >= 0
            ? m_playbackStartTimelinePositionMs
            : (m_selectedTimelinePositionMs >= 0 ? m_selectedTimelinePositionMs : m_timelineSpanStartMs);
        qint64 effectivePositionMs = safePositionMs;
        if (effectivePositionMs <= 0
            && !m_player->isPaused()
            && m_player->status() == StreamStatus::Playing
            && m_playbackStartWallclockMs > 0) {
            effectivePositionMs = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - m_playbackStartWallclockMs);
        }
        if (effectivePositionMs > 0 || safeDurationMs > 0) {
            absoluteMs = std::clamp<qint64>(
                absoluteMs + effectivePositionMs,
                m_timelineSpanStartMs,
                m_timelineSpanEndMs);
        }
        m_currentTimelinePositionMs = absoluteMs;
        m_currentTimeLabel->setText(timeTextForDayMs(m_currentTimelinePositionMs));
        m_totalTimeLabel->setText(timeTextForDayMs(m_timelineSpanEndMs));
        const int value = sliderValueForTimelinePosition(
            m_currentTimelinePositionMs,
            m_timelineSpanStartMs,
            m_timelineSpanEndMs);
        m_timelineSlider->setValue(std::clamp(value, 0, 1000));
        updateTimelineHandle();
        return;
    }

    m_currentTimeLabel->setText(formatClipDuration(safePositionMs / 1000));
    m_totalTimeLabel->setText(formatClipDuration(safeDurationMs / 1000));

    if (safeDurationMs <= 0) {
        m_timelineSlider->setValue(0);
    } else {
        const int value = static_cast<int>((safePositionMs * 1000) / safeDurationMs);
        m_timelineSlider->setValue(std::clamp(value, 0, 1000));
    }
    updateTimelineHandle();
}

void PlaybackScreen::rebuildEventMarkers()
{
    // Marker 재구성은 타임라인 표현 중 가장 비용이 큰 작업이다.
    // playable range bar와 이벤트 마커 오버레이를 현재 24시간 축 좌표계에 맞춰 다시 배치한다.
    // 현재 정책은:
    // - 24시간 전체 트랙은 슬라이더가 담당
    // - playable range는 한 덩어리 envelope bar
    // - 이벤트는 클러스터링된 얇은 클릭 마커
    // 로 나뉘며, 이 함수가 그 오버레이 위젯들을 다시 배치한다.
    if (!m_timelineSlider) {
        return;
    }

    for (QWidget *w : m_markerWidgets) {
        if (w) {
            w->deleteLater();
        }
    }
    m_markerWidgets.clear();

    const QRect sliderRect = m_timelineSlider->rect();
    const int overlayW = sliderRect.width();
    const int overlayH = sliderRect.height();
    const int grooveMargin = 10;
    const int grooveW = std::max(1, overlayW - grooveMargin * 2);
    const int overlayMidY = overlayH / 2;
    const int rangeH = 6;
    const int markerH = 16;
    const qint64 dayDurationMs = std::max<qint64>(1, m_timelineSpanEndMs - m_timelineSpanStartMs);
    bool hasPlayableEnvelope = false;
    qint64 playableFromMs = 0;
    qint64 playableToMs = 0;

    for (const auto &range : m_availableRanges) {
        const qint64 fromMs = timeTextToDayMs(range.from);
        const qint64 toMs = timeTextToDayMs(range.to);
        if (fromMs < 0 || toMs <= fromMs) {
            continue;
        }
        const qint64 clampedFromMs = std::clamp(fromMs, m_timelineSpanStartMs, m_timelineSpanEndMs);
        const qint64 clampedToMs = std::clamp(toMs, m_timelineSpanStartMs, m_timelineSpanEndMs);
        if (clampedToMs <= clampedFromMs) {
            continue;
        }
        if (!hasPlayableEnvelope) {
            hasPlayableEnvelope = true;
            playableFromMs = clampedFromMs;
            playableToMs = clampedToMs;
        } else {
            playableFromMs = std::min(playableFromMs, clampedFromMs);
            playableToMs = std::max(playableToMs, clampedToMs);
        }
    }

    if (hasPlayableEnvelope && playableToMs > playableFromMs) {
        const double fromRatio = static_cast<double>(playableFromMs - m_timelineSpanStartMs) / static_cast<double>(dayDurationMs);
        const double toRatio = static_cast<double>(playableToMs - m_timelineSpanStartMs) / static_cast<double>(dayDurationMs);
        const int x1 = grooveMargin + static_cast<int>(std::clamp(fromRatio, 0.0, 1.0) * grooveW);
        const int x2 = grooveMargin + static_cast<int>(std::clamp(toRatio, 0.0, 1.0) * grooveW);
        QWidget *segmentParent = m_timelineSlider->parentWidget() ? m_timelineSlider->parentWidget() : m_timelineSlider;
        auto *segment = new QFrame(segmentParent);
        segment->setObjectName("playbackAvailableRange");
        segment->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        const int localX = std::clamp(x1, 0, overlayW);
        const int localY = std::max(0, overlayMidY - rangeH / 2);
        const int rangeW = std::max(2, x2 - x1);
        if (segmentParent == m_timelineSlider) {
            segment->setGeometry(localX, localY, rangeW, rangeH);
        } else {
            const QPoint sliderTopLeft = m_timelineSlider->pos();
            segment->setGeometry(
                sliderTopLeft.x() + localX,
                sliderTopLeft.y() + localY,
                rangeW,
                rangeH);
        }
        segment->show();
        m_markerWidgets.push_back(segment);
    }

    for (const auto &marker : m_timelineMarkers) {
        const qint64 markerMs = markerTimestampMs(m_selectedPlaybackDate, marker.ts);
        if (markerMs < 0) {
            continue;
        }
        const double ratio = static_cast<double>(markerMs - m_timelineSpanStartMs) / static_cast<double>(dayDurationMs);
        if (ratio < 0.0 || ratio > 1.0) {
            continue;
        }
        const int x = grooveMargin + static_cast<int>(ratio * grooveW);
        QWidget *dotParent = m_timelineSlider->parentWidget() ? m_timelineSlider->parentWidget() : m_timelineSlider;
        auto *dot = new QPushButton(dotParent);
        dot->setFixedSize(2, markerH);
        dot->setObjectName("playbackMarkerDot");
        dot->setCursor(Qt::PointingHandCursor);
        const int dotX = std::clamp(x - 1, 0, std::max(0, overlayW - dot->width()));
        const int dotY = std::max(0, overlayMidY - markerH / 2);
        if (dotParent == m_timelineSlider) {
            dot->move(dotX, dotY);
        } else {
            const QPoint sliderTopLeft = m_timelineSlider->pos();
            dot->move(sliderTopLeft.x() + dotX, sliderTopLeft.y() + dotY);
        }
        dot->setToolTip(QString("%1 | %2 | %3")
                            .arg(m_currentPlaybackChannelName, marker.type, marker.ts));
        connect(dot, &QPushButton::clicked, this, [this, marker]() {
            if (m_currentPlaybackChannelId < 0 || m_selectedPlaybackDate.isEmpty()) {
                return;
            }
            const qint64 markerMs = markerTimestampMs(m_selectedPlaybackDate, marker.ts);
            const qint64 requestMs = playbackMarkerRequestPosition(markerMs, m_availableRanges);
            const QString ts = playbackTimestampForDateAndMs(m_selectedPlaybackDate, requestMs);
            if (ts.isEmpty()) {
                return;
            }
            requestPlaybackStream(m_currentPlaybackChannelId, m_currentPlaybackChannelName, ts, true);
        });
        dot->show();
        dot->raise();
        m_markerWidgets.push_back(dot);
    }

    updateTimelineHandle();
}

void PlaybackScreen::updateTimelineHandle()
{
    if (!m_timelineSlider || !m_timelineHandle) {
        return;
    }

    if (!m_timelineSlider->isVisible()) {
        m_timelineHandle->hide();
        return;
    }

    const QRect sliderRect = m_timelineSlider->rect();
    const int overlayW = sliderRect.width();
    const int overlayH = sliderRect.height();
    const int grooveMargin = 10;
    const int grooveW = std::max(1, overlayW - grooveMargin * 2);
    const double ratio = static_cast<double>(m_timelineSlider->value()) / static_cast<double>(std::max(1, m_timelineSlider->maximum()));
    const int x = grooveMargin + static_cast<int>(std::clamp(ratio, 0.0, 1.0) * grooveW);
    const int handleX = std::clamp(x - (m_timelineHandle->width() / 2), 0, std::max(0, overlayW - m_timelineHandle->width()));
    const int handleY = std::max(0, (overlayH - m_timelineHandle->height()) / 2);

    if (m_timelineHandle->parentWidget() == m_timelineSlider) {
        m_timelineHandle->move(handleX, handleY);
    } else {
        const QPoint sliderTopLeft = m_timelineSlider->pos();
        m_timelineHandle->move(sliderTopLeft.x() + handleX, sliderTopLeft.y() + handleY);
    }
    m_timelineHandle->show();
    m_timelineHandle->raise();
}

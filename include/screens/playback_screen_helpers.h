#ifndef PLAYBACK_SCREEN_HELPERS_H
#define PLAYBACK_SCREEN_HELPERS_H

#include "playback_service.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QString>
#include <QTime>
#include <QTimeZone>
#include <QUrl>
#include <QVector>

#include <algorithm>

namespace PlaybackScreenHelpers {

inline const QVector<double> &playbackRates()
{
    static const QVector<double> kRates{0.5, 1.0, 2.0, 4.0, 8.0};
    return kRates;
}

inline QString rateText(double rate)
{
    if (rate == 0.5) {
        return "0.5x";
    }
    if (rate == 1.0) {
        return "1x";
    }
    if (rate == 2.0) {
        return "2x";
    }
    if (rate == 4.0) {
        return "4x";
    }
    if (rate == 8.0) {
        return "8x";
    }
    return QString::number(rate) + "x";
}

inline QString defaultPlaybackTimestamp(const QString &date, const QVector<PlaybackTimeRange> &ranges)
{
    if (date.trimmed().isEmpty()) {
        return {};
    }
    QString timeText = QStringLiteral("00:00:00");
    if (!ranges.isEmpty() && !ranges.first().from.trimmed().isEmpty()) {
        timeText = ranges.first().from.trimmed();
    }
    const QDate parsedDate = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
    const QTime parsedTime = QTime::fromString(timeText, QStringLiteral("HH:mm:ss"));
    if (!parsedDate.isValid() || !parsedTime.isValid()) {
        return {};
    }
    return QDateTime(parsedDate, parsedTime, QTimeZone::systemTimeZone()).toString(Qt::ISODate);
}

inline qint64 timeTextToDayMs(const QString &timeText)
{
    const QTime parsedTime = QTime::fromString(timeText.trimmed(), QStringLiteral("HH:mm:ss"));
    if (!parsedTime.isValid()) {
        return -1;
    }
    return QTime(0, 0).msecsTo(parsedTime);
}

inline QString playbackTimestampForDateAndMs(const QString &date, qint64 dayMs)
{
    const QDate parsedDate = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
    if (!parsedDate.isValid() || dayMs < 0) {
        return {};
    }
    const QTime time = QTime(0, 0).addMSecs(static_cast<int>(std::clamp<qint64>(dayMs, 0, 24LL * 60LL * 60LL * 1000LL - 1)));
    return QDateTime(parsedDate, time, QTimeZone::systemTimeZone()).toString(Qt::ISODate);
}

inline QString timeTextForDayMs(qint64 dayMs)
{
    if (dayMs < 0) {
        return QStringLiteral("00:00:00");
    }
    const qint64 clamped = std::clamp<qint64>(dayMs, 0, 24LL * 60LL * 60LL * 1000LL - 1);
    return QTime(0, 0).addMSecs(static_cast<int>(clamped)).toString(QStringLiteral("HH:mm:ss"));
}

inline qint64 timelinePositionFromTimestamp(const QString &timestamp)
{
    const QDateTime dt = QDateTime::fromString(timestamp.trimmed(), Qt::ISODate);
    if (!dt.isValid()) {
        return -1;
    }
    return QTime(0, 0).msecsTo(dt.time());
}

inline qint64 playableTimelinePosition(qint64 requestedMs, const QVector<PlaybackTimeRange> &ranges)
{
    if (ranges.isEmpty()) {
        return -1;
    }

    qint64 previousRangeEnd = -1;
    for (const auto &range : ranges) {
        const qint64 fromMs = timeTextToDayMs(range.from);
        const qint64 toMs = timeTextToDayMs(range.to);
        if (fromMs < 0 || toMs < 0) {
            continue;
        }
        if (requestedMs <= fromMs) {
            if (previousRangeEnd >= 0 && requestedMs <= previousRangeEnd) {
                return previousRangeEnd;
            }
            return fromMs;
        }
        if (requestedMs >= fromMs && requestedMs <= toMs) {
            return requestedMs;
        }
        previousRangeEnd = toMs;
    }

    return previousRangeEnd;
}

inline int sliderValueForTimelinePosition(qint64 positionMs, qint64 spanStartMs, qint64 spanEndMs)
{
    const qint64 safeSpanEnd = std::max(spanEndMs, spanStartMs + 1);
    const qint64 clampedPos = std::clamp(positionMs, spanStartMs, safeSpanEnd);
    return static_cast<int>(((clampedPos - spanStartMs) * 1000) / (safeSpanEnd - spanStartMs));
}

inline qint64 timelinePositionForSliderValue(int sliderValue, qint64 spanStartMs, qint64 spanEndMs)
{
    const qint64 safeSpanEnd = std::max(spanEndMs, spanStartMs + 1);
    return spanStartMs + ((safeSpanEnd - spanStartMs) * static_cast<qint64>(sliderValue)) / 1000;
}

inline qint64 markerTimestampMs(const QString &date, const QString &timeText)
{
    const QDate parsedDate = QDate::fromString(date, QStringLiteral("yyyy-MM-dd"));
    const QTime parsedTime = QTime::fromString(timeText.trimmed(), QStringLiteral("HH:mm:ss"));
    if (!parsedDate.isValid() || !parsedTime.isValid()) {
        return -1;
    }
    const QDateTime startOfDay(parsedDate, QTime(0, 0));
    const QDateTime markerDateTime(parsedDate, parsedTime);
    return startOfDay.msecsTo(markerDateTime);
}

inline qint64 playbackRangeStartForPosition(qint64 requestedMs, const QVector<PlaybackTimeRange> &ranges)
{
    for (const auto &range : ranges) {
        const qint64 fromMs = timeTextToDayMs(range.from);
        const qint64 toMs = timeTextToDayMs(range.to);
        if (fromMs < 0 || toMs < 0) {
            continue;
        }
        if (requestedMs >= fromMs && requestedMs <= toMs) {
            return fromMs;
        }
    }
    return -1;
}

inline qint64 playbackMarkerRequestPosition(qint64 markerMs, const QVector<PlaybackTimeRange> &ranges)
{
    return playableTimelinePosition(markerMs, ranges);
}

inline QString displayNameFromSource(const QString &source)
{
    const QString trimmed = source.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (trimmed.startsWith(QStringLiteral("http://")) || trimmed.startsWith(QStringLiteral("https://"))) {
        const QUrl url(trimmed);
        const QString fileName = QFileInfo(url.path()).fileName();
        return fileName.isEmpty() ? trimmed : fileName;
    }
    return QFileInfo(trimmed).fileName();
}

} // namespace PlaybackScreenHelpers

#endif // PLAYBACK_SCREEN_HELPERS_H

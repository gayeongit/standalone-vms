#include "ugv_screen.h"
#include <QGraphicsLineItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

QPointF UgvScreen::mapPointForGps(double latitude, double longitude) const
{
    if (!m_mapScene) {
        return {};
    }
    const QRectF rect = m_mapScene->sceneRect();
    const double lonRatio = std::clamp((longitude - m_mapMinLon) / (m_mapMaxLon - m_mapMinLon), 0.0, 1.0);
    const double latRatio = std::clamp((latitude - m_mapMinLat) / (m_mapMaxLat - m_mapMinLat), 0.0, 1.0);
    const double x = rect.left() + lonRatio * rect.width();
    const double y = rect.bottom() - latRatio * rect.height();
    return {x, y};
}

void UgvScreen::updateMapTelemetry(const UgvGpsTelemetry &telemetry)
{
    // 지도 갱신은 매 수신마다 점을 무한히 쌓지 않고,
    // GPS telemetry를 scene 좌표로 바꿔 marker, heading line, route path를 업데이트한다.
    // 일정 거리 이상 움직였을 때만 route path를 늘려 성능과 가독성을 같이 관리한다.
    // marker / heading / route를 따로 그리는 이유도 각 요소의 업데이트 비용을 분리하기 위해서다.
    if (!m_mapScene || !m_mapMarker || !m_mapRoute || !m_mapHeadingLine || !m_mapPulse || !m_mapHeadingChevron) {
        return;
    }

    const QPointF point = mapPointForGps(telemetry.latitude, telemetry.longitude);
    double renderHeadingDeg = telemetry.headingDeg;
    bool hasRenderableHeading = std::isfinite(renderHeadingDeg);

    if (m_hasLastMapPoint) {
        const double dx = point.x() - m_lastMapPoint.x();
        const double dy = point.y() - m_lastMapPoint.y();
        const double movePx = std::sqrt(dx * dx + dy * dy);
        constexpr double kHeadingEstimateMinMovePx = 2.0;
        if (movePx >= kHeadingEstimateMinMovePx) {
            constexpr double kRadToDeg = 57.2957795130823208768;
            double estimated = 90.0 - std::atan2(-dy, dx) * kRadToDeg;
            while (estimated < 0.0) {
                estimated += 360.0;
            }
            while (estimated >= 360.0) {
                estimated -= 360.0;
            }

            if (!m_hasEstimatedHeading) {
                m_estimatedHeadingDeg = estimated;
                m_hasEstimatedHeading = true;
            } else {
                // Smooth heading to avoid sudden swings on jittery GPS.
                double delta = estimated - m_estimatedHeadingDeg;
                while (delta > 180.0) {
                    delta -= 360.0;
                }
                while (delta < -180.0) {
                    delta += 360.0;
                }
                m_estimatedHeadingDeg += delta * 0.35;
                while (m_estimatedHeadingDeg < 0.0) {
                    m_estimatedHeadingDeg += 360.0;
                }
                while (m_estimatedHeadingDeg >= 360.0) {
                    m_estimatedHeadingDeg -= 360.0;
                }
            }
            renderHeadingDeg = m_estimatedHeadingDeg;
            hasRenderableHeading = true;
        } else if (m_hasEstimatedHeading) {
            renderHeadingDeg = m_estimatedHeadingDeg;
            hasRenderableHeading = true;
        }
    }

    m_hasLastMapPoint = true;
    m_lastMapPoint = point;

    m_mapPulse->setRect(point.x() - 56.0, point.y() - 56.0, 112.0, 112.0);
    m_mapMarker->setRect(point.x() - 16.0, point.y() - 16.0, 32.0, 32.0);
    if (hasRenderableHeading) {
        m_mapHeadingChevron->setPos(point);
        m_mapHeadingChevron->setRotation(renderHeadingDeg);
    }
    m_mapPulse->setVisible(true);
    m_mapMarker->setVisible(true);
    m_mapHeadingLine->setVisible(false);
    m_mapHeadingChevron->setVisible(hasRenderableHeading);

    if (m_routePoints.isEmpty() || QLineF(m_routePoints.last(), point).length() > 4.0) {
        m_routePoints.push_back(point);
        constexpr int kMaxRoutePoints = 2000;
        if (m_routePoints.size() > kMaxRoutePoints) {
            m_routePoints.remove(0, m_routePoints.size() - kMaxRoutePoints);
        }
    }

    if (!m_routePoints.isEmpty()) {
        QPainterPath path(m_routePoints.first());
        for (int i = 1; i < m_routePoints.size(); ++i) {
            path.lineTo(m_routePoints.at(i));
        }
        m_mapRoute->setPath(path);
    }
}




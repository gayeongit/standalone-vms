#include "app_config_loader.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QStringList>
#include <algorithm>

bool loadAppConfig(AppConfig *out, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (!out) {
        if (errorMessage) {
            *errorMessage = "설정 출력 포인터가 비어 있습니다.";
        }
        return false;
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/app_config.json",
        appDir + "/../app_config.json",
        appDir + "/../../app_config.json"
    };

    QString foundPath;
    QByteArray raw;
    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.exists()) {
            continue;
        }
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        raw = f.readAll();
        foundPath = path;
        break;
    }

    if (foundPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString("app_config.json 파일을 찾지 못했습니다. 경로: %1").arg(candidates.join(", "));
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QString("app_config.json 파싱 실패(%1): %2").arg(foundPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QString apiBaseUrl = root.value("apiBaseUrl").toString().trimmed();
    if (apiBaseUrl.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QString("app_config.json(apiBaseUrl)이 비어 있습니다: %1").arg(foundPath);
        }
        return false;
    }

    out->apiBaseUrl = apiBaseUrl;
    out->requestTimeoutMs = (std::max)(1000, root.value("requestTimeoutMs").toInt(8000));

    const QJsonObject authObj = root.value("auth").toObject();
    const QString loginPath = authObj.value("loginPath").toString().trimmed();
    const QString logoutPath = authObj.value("logoutPath").toString().trimmed();
    const QString signupPath = authObj.value("signupPath").toString().trimmed();
    if (!loginPath.isEmpty()) {
        out->loginPath = loginPath;
    }
    if (!logoutPath.isEmpty()) {
        out->logoutPath = logoutPath;
    }
    if (!signupPath.isEmpty()) {
        out->signupPath = signupPath;
    }

    const QJsonObject eventObj = root.value("event").toObject();
    const QString wsUrl = eventObj.value("wsUrl").toString().trimmed();
    if (!wsUrl.isEmpty()) {
        out->eventWsUrl = wsUrl;
    } else {
        QUrl derived(apiBaseUrl);
        if (derived.isValid() && (derived.scheme() == "http" || derived.scheme() == "https")) {
            derived.setScheme(derived.scheme() == "https" ? "wss" : "ws");
            derived.setPath("/events/ws");
            derived.setQuery(QString());
            derived.setFragment(QString());
            out->eventWsUrl = derived.toString();
        }
    }
    const QString wsSubprotocol = eventObj.value("wsSubprotocol").toString().trimmed();
    if (!wsSubprotocol.isEmpty()) {
        out->eventWsSubprotocol = wsSubprotocol;
    }
    const QString eventsPath = eventObj.value("eventsPath").toString().trimmed();
    if (!eventsPath.isEmpty()) {
        out->eventEventsPath = eventsPath;
    }
    const QString eventDetailPath = eventObj.value("detailPath").toString().trimmed();
    if (!eventDetailPath.isEmpty()) {
        out->eventDetailPathTemplate = eventDetailPath;
    }

    const QJsonObject deviceObj = root.value("device").toObject();
    const QString devicesPath = deviceObj.value("devicesPath").toString().trimmed();
    if (!devicesPath.isEmpty()) {
        out->devicesPath = devicesPath;
    }
    const QString deviceChannelsPath = deviceObj.value("deviceChannelsPath").toString().trimmed();
    if (!deviceChannelsPath.isEmpty()) {
        out->deviceChannelsPathTemplate = deviceChannelsPath;
    }
    const QString channelDetailPath = deviceObj.value("channelDetailPath").toString().trimmed();
    if (!channelDetailPath.isEmpty()) {
        out->channelDetailPathTemplate = channelDetailPath;
    }

    const QJsonObject cctvObj = root.value("cctv").toObject();
    const QString zoomPath = cctvObj.value("zoomPath").toString().trimmed();
    if (!zoomPath.isEmpty()) {
        out->cctvZoomPathTemplate = zoomPath;
    }
    const QString focusPath = cctvObj.value("focusPath").toString().trimmed();
    if (!focusPath.isEmpty()) {
        out->cctvFocusPathTemplate = focusPath;
    }

    const QJsonObject playbackObj = root.value("playback").toObject();
    QString channelsByDatePath = playbackObj.value("channelsByDatePathTemplate").toString().trimmed();
    if (channelsByDatePath.isEmpty()) {
        channelsByDatePath = playbackObj.value("channelsByDatePath").toString().trimmed();
    }
    if (!channelsByDatePath.isEmpty()) {
        out->playbackChannelsByDatePathTemplate = channelsByDatePath;
    }
    const QString timelinePath = playbackObj.value("timelinePath").toString().trimmed();
    if (!timelinePath.isEmpty()) {
        out->playbackTimelinePath = timelinePath;
    }
    const QString streamPath = playbackObj.value("streamPath").toString().trimmed();
    if (!streamPath.isEmpty()) {
        out->playbackStreamPath = streamPath;
    }
    const QString exportPath = playbackObj.value("exportPath").toString().trimmed();
    if (!exportPath.isEmpty()) {
        out->playbackExportPath = exportPath;
    }
    const QString exportStatusPath = playbackObj.value("exportStatusPathTemplate").toString().trimmed();
    if (!exportStatusPath.isEmpty()) {
        out->playbackExportStatusPathTemplate = exportStatusPath;
    }

    const QJsonObject ugvObj = root.value("ugv").toObject();
    const QString ugvWsUrl = ugvObj.value("gatewayWsUrl").toString().trimmed();
    if (!ugvWsUrl.isEmpty()) {
        out->ugvGatewayWsUrl = ugvWsUrl;
    } else {
        QUrl derived(apiBaseUrl);
        if (derived.isValid() && (derived.scheme() == "http" || derived.scheme() == "https")) {
            derived.setScheme(derived.scheme() == "https" ? "wss" : "ws");
            derived.setPath("/gw/ws");
            derived.setQuery(QString());
            derived.setFragment(QString());
            out->ugvGatewayWsUrl = derived.toString();
        }
    }
    const QString ugvWsSubprotocol = ugvObj.value("gatewayWsSubprotocol").toString().trimmed();
    if (!ugvWsSubprotocol.isEmpty()) {
        out->ugvGatewayWsSubprotocol = ugvWsSubprotocol;
    }
    const QJsonObject mapBoundsObj = ugvObj.value("mapBounds").toObject();
    const double minLat = mapBoundsObj.value("minLat").toDouble(out->ugvMapMinLat);
    const double maxLat = mapBoundsObj.value("maxLat").toDouble(out->ugvMapMaxLat);
    const double minLon = mapBoundsObj.value("minLon").toDouble(out->ugvMapMinLon);
    const double maxLon = mapBoundsObj.value("maxLon").toDouble(out->ugvMapMaxLon);
    if (minLat < maxLat) {
        out->ugvMapMinLat = minLat;
        out->ugvMapMaxLat = maxLat;
    }
    if (minLon < maxLon) {
        out->ugvMapMinLon = minLon;
        out->ugvMapMaxLon = maxLon;
    }

    return true;
}

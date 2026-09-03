#ifndef APP_CONFIG_LOADER_H
#define APP_CONFIG_LOADER_H

#include <QString>

struct AppConfig {
    QString apiBaseUrl;
    int requestTimeoutMs = 8000;
    QString loginPath = "/auth/login";
    QString logoutPath = "/auth/logout";
    QString signupPath = "/auth/signup";
    QString eventWsUrl;
    QString eventWsSubprotocol = "vms.events.v1";
    QString eventEventsPath = "/events";
    QString eventDetailPathTemplate = "/event/{eventId}";
    QString devicesPath = "/devices";
    QString deviceChannelsPathTemplate = "/device/{deviceId}/channels";
    QString channelDetailPathTemplate = "/channel/{channelId}";
    QString cctvZoomPathTemplate = "/channel/{channelId}/zoom";
    QString cctvFocusPathTemplate = "/channel/{channelId}/focus";
    QString playbackChannelsByDatePathTemplate = "/playback/dates/{date}/channels";
    QString playbackTimelinePath = "/playback/timeline";
    QString playbackStreamPath = "/playback/stream";
    QString playbackExportPath = "/playback/export";
    QString playbackExportStatusPathTemplate = "/playback/export/{jobId}";
    QString ugvGatewayWsUrl;
    QString ugvGatewayWsSubprotocol = "vms.gw.v1";
    double ugvMapMinLat = 33.0;
    double ugvMapMaxLat = 39.0;
    double ugvMapMinLon = 124.0;
    double ugvMapMaxLon = 132.0;
};

bool loadAppConfig(AppConfig *out, QString *errorMessage);

#endif // APP_CONFIG_LOADER_H

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <QString>

struct EventInfo {
    QString eventId;
    int deviceId = -1;
    int channelId = -1;
    QString timestamp;
    QString channel;
    QString type;
    QString summary;
    QString bestshotId;
};

#endif // EVENT_TYPES_H

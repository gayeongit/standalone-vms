#ifndef SELECTED_CHANNEL_CONTEXT_H
#define SELECTED_CHANNEL_CONTEXT_H

#include <QString>

struct SelectedChannelContext
{
    int deviceId = -1;
    int channelId = -1;
    int channelNo = -1;
    QString displayName;
    QString deviceIp;
    QString deviceType;
    QString model;
    QString videoCodec;
    bool online = false;
    QString health;
};

#endif // SELECTED_CHANNEL_CONTEXT_H

#ifndef MAINWINDOW_INTERNAL_H
#define MAINWINDOW_INTERNAL_H

#include "app_state.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace MainWindowInternal {

int toStackIndex(ScreenId screenId);
bool isCompactScreen(ScreenId screenId);
bool shouldTrackFirstFrameForScreen(ScreenId screenId);
QVector<SelectedChannelContext> normalizeSelectedContextsForRuntime(const QVector<SelectedChannelContext> &contexts);
QString screenIdName(ScreenId screenId);
void applyDeviceChangesToRuntimeState(AppState &state);
void pruneStateDeviceSelection(AppState &state);
QSet<QString> activeChannelsForScreen(ScreenId screenId);

} // namespace MainWindowInternal

#endif // MAINWINDOW_INTERNAL_H

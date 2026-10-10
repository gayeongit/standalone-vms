#include "event_ui_helpers.h"

#include "app_state.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "event_service.h"

#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QTreeWidget>
#include <algorithm>

namespace {

EventService *g_eventService = nullptr;

enum class NotificationRange {
    Last12Hours,
    Last1Day,
    Last3Days
};

bool parseFlexibleTimestamp(const QString &ts, QDateTime *out)
{
    const QString trimmed = ts.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    const auto accept = [out](const QDateTime &dt) {
        if (!dt.isValid()) {
            return false;
        }
        if (out) {
            *out = dt;
        }
        return true;
    };

    if (accept(QDateTime::fromString(trimmed, Qt::ISODateWithMs))) {
        return true;
    }
    if (accept(QDateTime::fromString(trimmed, Qt::ISODate))) {
        return true;
    }

    QString normalized = trimmed;
    if (normalized.size() > 10 && normalized.at(10) == QLatin1Char(' ')) {
        normalized[10] = QLatin1Char('T');
    }
    normalized.replace(QRegularExpression(QStringLiteral("([+-]\\d{2})$")), QStringLiteral("\\1:00"));
    normalized.replace(QRegularExpression(QStringLiteral("([+-]\\d{2})(\\d{2})$")), QStringLiteral("\\1:\\2"));

    if (accept(QDateTime::fromString(normalized, Qt::ISODateWithMs))) {
        return true;
    }
    if (accept(QDateTime::fromString(normalized, Qt::ISODate))) {
        return true;
    }

    const QStringList formats = {
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.z"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss.zzzzzz")
    };
    for (const QString &format : formats) {
        if (accept(QDateTime::fromString(trimmed, format))) {
            return true;
        }
    }
    return false;
}

QDateTime notificationReferenceNow(const QVector<EventInfo> &events)
{
    QDateTime latest = QDateTime::currentDateTime();
    bool found = false;
    for (const auto &eventInfo : events) {
        QDateTime ts;
        if (!parseFlexibleTimestamp(eventInfo.timestamp, &ts)) {
            continue;
        }
        if (!found || ts > latest) {
            latest = ts;
            found = true;
        }
    }
    return latest;
}

QDateTime notificationThreshold(NotificationRange range, const QDateTime &referenceNow)
{
    switch (range) {
    case NotificationRange::Last12Hours:
        return referenceNow.addSecs(-12 * 60 * 60);
    case NotificationRange::Last1Day:
        return referenceNow.addDays(-1);
    case NotificationRange::Last3Days:
        return referenceNow.addDays(-3);
    }
    return referenceNow.addDays(-3);
}

QString resolveChannelName(int channelId, const QString &fallback = {})
{
    if (channelId >= 0) {
        const auto &contexts = AppState::instance().selectedChannelContexts;
        for (const auto &ctx : contexts) {
            if (ctx.channelId == channelId) {
                const QString displayName = ctx.displayName.trimmed();
                if (!displayName.isEmpty()) {
                    return displayName;
                }
                break;
            }
        }
        return QStringLiteral("Channel %1").arg(channelId);
    }
    return fallback.trimmed().isEmpty() ? QStringLiteral("Unknown") : fallback;
}

QString filteredChannelName(const EventInfo &eventInfo)
{
    return resolveChannelName(eventInfo.channelId, eventInfo.channel);
}

QString modelLabelWithIpSuffix(const QString &modelName, const QString &deviceIp, int deviceId)
{
    const QString trimmedModel = modelName.trimmed().isEmpty()
        ? QStringLiteral("Unknown Model")
        : modelName.trimmed();
    const QString trimmedIp = deviceIp.trimmed();
    if (!trimmedIp.isEmpty()) {
        const QStringList parts = trimmedIp.split('.');
        const QString lastOctet = parts.isEmpty() ? QString() : parts.constLast().trimmed();
        if (!lastOctet.isEmpty()) {
            return QStringLiteral("%1 (.%2)").arg(trimmedModel, lastOctet);
        }
    }
    Q_UNUSED(deviceId);
    return trimmedModel;
}

QString shortEventTimestamp(const QString &value)
{
    QDateTime ts;
    if (!parseFlexibleTimestamp(value, &ts)) {
        return value;
    }
    const QDateTime local = ts.timeSpec() == Qt::LocalTime ? ts : ts.toLocalTime();
    return local.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString detailedEventTimestamp(const QString &value)
{
    QDateTime ts;
    if (!parseFlexibleTimestamp(value, &ts)) {
        return value;
    }
    const QDateTime local = ts.timeSpec() == Qt::LocalTime ? ts : ts.toLocalTime();
    return local.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString displayEventTypeName(const QString &rawType)
{
    const QString trimmed = rawType.trimmed();
    if (trimmed.isEmpty()) {
        return trimmed;
    }

    if (trimmed.contains(QStringLiteral("TemperatureAlarm"), Qt::CaseInsensitive)) {
        return QStringLiteral("이상 온도 감지");
    }
    if (trimmed.contains(QStringLiteral("TamperingDetection"), Qt::CaseInsensitive)) {
        return QStringLiteral("카메라 가림 감지");
    }
    if (trimmed.contains(QStringLiteral("ShockDetection"), Qt::CaseInsensitive)) {
        return QStringLiteral("충격 감지");
    }
    if (trimmed.contains(QStringLiteral("MotionAlarm"), Qt::CaseInsensitive)) {
        return QStringLiteral("움직임 감지");
    }

    const int slashIndex = trimmed.lastIndexOf(QLatin1Char('/'));
    if (slashIndex >= 0 && slashIndex + 1 < trimmed.size()) {
        return trimmed.mid(slashIndex + 1).trimmed();
    }
    return trimmed;
}

QString shortChannelName(const EventInfo &eventInfo)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        const bool matchedById = eventInfo.channelId >= 0 && ctx.channelId == eventInfo.channelId;
        const bool matchedByName = !eventInfo.channel.trimmed().isEmpty()
            && ctx.displayName.trimmed() == eventInfo.channel.trimmed();
        if (matchedById || matchedByName) {
            return modelLabelWithIpSuffix(ctx.model, ctx.deviceIp, ctx.deviceId);
        }
    }

    return filteredChannelName(eventInfo);
}

QString detailedChannelName(const EventInfo &eventInfo)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        const bool matchedById = eventInfo.channelId >= 0 && ctx.channelId == eventInfo.channelId;
        const bool matchedByName = !eventInfo.channel.trimmed().isEmpty()
            && ctx.displayName.trimmed() == eventInfo.channel.trimmed();
        if (matchedById || matchedByName) {
            const QString modelLabel = modelLabelWithIpSuffix(ctx.model, ctx.deviceIp, ctx.deviceId);
            if (ctx.channelNo >= 0) {
                return QStringLiteral("%1 Channel %2").arg(modelLabel).arg(ctx.channelNo);
            }
            return modelLabel;
        }
    }

    return filteredChannelName(eventInfo);
}

QVector<EventInfo> filteredNotificationEvents(NotificationRange range)
{
    QVector<EventInfo> filtered;
    QVector<QPair<QDateTime, EventInfo>> parsedEvents;
    const auto events = EventUiHelpers::currentEvents();
    parsedEvents.reserve(events.size());
    for (const auto &eventInfo : events) {
        QDateTime ts;
        if (!parseFlexibleTimestamp(eventInfo.timestamp, &ts)) {
            continue;
        }
        parsedEvents.push_back({ts, eventInfo});
    }

    std::sort(parsedEvents.begin(), parsedEvents.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first > rhs.first;
    });

    const QDateTime referenceNow = notificationReferenceNow(events);
    const QDateTime threshold = notificationThreshold(range, referenceNow);
    for (const auto &entry : parsedEvents) {
        if (entry.first >= threshold) {
            filtered.push_back(entry.second);
        }
    }
    return filtered;
}

} // namespace

namespace EventUiHelpers {

void setEventService(EventService *service)
{
    g_eventService = service;
}

EventService *eventService()
{
    return g_eventService;
}

QVector<EventInfo> currentEvents()
{
    // Phase 6: 예전엔 선택된 CCTV 채널과 channelId/표시명이 정확히 일치하는 이벤트만 통과시켰다.
    // Phase 4에서 카메라가 UDP로 직접 쏘는 로컬 이벤트를 받기 시작하면서 이 필터가 조용히
    // 이벤트를 떨어뜨리는 버그가 됐다 — 카메라가 자체적으로 붙이는 channelId/channelName은
    // ONVIF discovery가 VMS 쪽에서 독자적으로 매기는 channelId/displayName과 근본적으로 다른
    // 번호 체계라 서로 안 맞을 수 있다(우연히 맞는 것만 통과, 나머지는 조용히 사라짐).
    // 대시보드 인라인 이벤트뷰는 애초에 이 필터를 안 거쳐서 전부 잘 보이고 있었다 — 세 화면
    // (대시보드/이벤트 검색/알림센터)이 같은 데이터를 봐야 하므로, 매칭 실패를 이유로 숨기지
    // 않고 전부 보여주는 쪽으로 통일한다.
    if (!g_eventService) {
        return {};
    }
    const QVector<EventInfo> all = g_eventService->events();
    QVector<EventInfo> withoutTelemetry;
    withoutTelemetry.reserve(all.size());
    for (const auto &eventInfo : all) {
        if (!eventInfo.type.trimmed().startsWith(QStringLiteral("telemetry."), Qt::CaseInsensitive)) {
            withoutTelemetry.push_back(eventInfo);
        }
    }
    return withoutTelemetry;
}

int currentUnreadCount()
{
    if (!g_eventService) {
        return 0;
    }
    return g_eventService->unreadCount();
}

bool showEventDetailDialog(QWidget *parent, const EventInfo &eventInfo, bool showDispatchButton)
{
    QDialog dialog(parent);
    bool dispatchRequested = false;
    dialog.setObjectName("eventDetailDialog");
    dialog.setWindowTitle(QStringLiteral("\uC774\uBCA4\uD2B8 \uC0C1\uC138"));
    dialog.setFixedSize(420, 230);
    dialog.winId();
    applyNativeDarkTitleBar(&dialog);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("\uC774\uBCA4\uD2B8 \uC0C1\uC138"), &dialog);
    title->setObjectName("modalDialogTitle");

    auto *timeLabel = new QLabel(QStringLiteral("\uB0A0\uC9DC: %1").arg(detailedEventTimestamp(eventInfo.timestamp)), &dialog);
    auto *channelLabel = new QLabel(QStringLiteral("\uCC44\uB110: %1").arg(detailedChannelName(eventInfo)), &dialog);
    auto *typeLabel = new QLabel(&dialog);
    timeLabel->setObjectName("eventDetailMetaLabel");
    channelLabel->setObjectName("eventDetailMetaLabel");
    typeLabel->setObjectName("eventDetailMetaLabel");
    typeLabel->setWordWrap(true);

    auto updateTypeLabel = [typeLabel](const QString &rawType) {
        const QString mapped = displayEventTypeName(rawType).trimmed();
        if (mapped.isEmpty()) {
            typeLabel->setText(QStringLiteral("이벤트: --"));
            return;
        }
        const QString raw = rawType.trimmed();
        if (raw.isEmpty() || raw == mapped) {
            typeLabel->setText(QStringLiteral("이벤트: %1").arg(mapped));
            return;
        }
        typeLabel->setText(QStringLiteral("이벤트: %1\n(%2)").arg(mapped, raw));
    };
    updateTypeLabel(eventInfo.type);

    if (!eventInfo.eventId.isEmpty() && g_eventService) {
        g_eventService->fetchEventDetail(
            eventInfo.eventId,
            &dialog,
            [channelLabel, updateTypeLabel](bool ok, const EventInfo &detail) {
                if (!ok) {
                    return;
                }
                const bool hasChannelFromDetail = (detail.channelId >= 0) || !detail.channel.trimmed().isEmpty();
                if (hasChannelFromDetail) {
                    channelLabel->setText(QStringLiteral("\uCC44\uB110: %1").arg(detailedChannelName(detail)));
                }
                if (!detail.type.trimmed().isEmpty()) {
                    updateTypeLabel(detail.type);
                }
            });
    }

    QPushButton *dispatchButton = nullptr;
    if (showDispatchButton) {
        dispatchButton = new QPushButton(QStringLiteral("UGV 출동"), &dialog);
        dispatchButton->setObjectName("ok");
        dispatchButton->setProperty("danger", true);
    }

    auto *closeButton = new QPushButton(QStringLiteral("닫기"), &dialog);
    if (dispatchButton) {
        const int uniformWidth = std::max(dispatchButton->sizeHint().width(), closeButton->sizeHint().width());
        dispatchButton->setMinimumWidth(uniformWidth);
        closeButton->setMinimumWidth(uniformWidth);
    }
    layout->addWidget(title);
    layout->addSpacing(8);
    layout->addWidget(timeLabel);
    layout->addWidget(channelLabel);
    layout->addWidget(typeLabel);
    layout->addStretch();

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(8);
    buttonRow->addStretch();
    if (dispatchButton) {
        buttonRow->addWidget(dispatchButton);
    }
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    if (dispatchButton) {
        QObject::connect(dispatchButton, &QPushButton::clicked, &dialog, [&dialog, &dispatchRequested]() {
            dispatchRequested = true;
            dialog.accept();
        });
    }
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, [&dialog]() {
        dialog.reject();
    });
    dialog.exec();
    return dispatchRequested;
}

void openEventSearchDialog(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setObjectName("eventSearchDialog");
    dialog.setWindowTitle(QStringLiteral("이벤트 검색"));
    dialog.setFixedSize(680, 460);
    dialog.winId();
    applyNativeDarkTitleBar(&dialog);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("이벤트 검색"), &dialog);
    title->setObjectName("eventSearchTitle");
    auto *filterRow = new QHBoxLayout();
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(8);
    auto *fromDate = new QDateEdit(QDate::currentDate().addDays(-7), &dialog);
    fromDate->setCalendarPopup(true);
    fromDate->setDisplayFormat("yyyy-MM-dd");
    auto *toDate = new QDateEdit(QDate::currentDate(), &dialog);
    toDate->setCalendarPopup(true);
    toDate->setDisplayFormat("yyyy-MM-dd");
    auto *typeBox = new QComboBox(&dialog);
    typeBox->addItem(QStringLiteral("전체"));
    auto *channelBox = new QComboBox(&dialog);
    channelBox->addItem(QStringLiteral("전체"));
    fromDate->setFixedHeight(28);
    toDate->setFixedHeight(28);
    typeBox->setFixedHeight(28);
    channelBox->setFixedHeight(28);
    fromDate->setFixedWidth(120);
    toDate->setFixedWidth(120);
    typeBox->setFixedWidth(120);
    channelBox->setFixedWidth(120);
    fromDate->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    toDate->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    typeBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    channelBox->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *searchButton = new QPushButton(QStringLiteral("검색"), &dialog);
    searchButton->setObjectName("eventSearchSearchButton");
    searchButton->setFixedSize(108, 28);
    searchButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    filterRow->addWidget(fromDate);
    filterRow->addWidget(toDate);
    filterRow->addWidget(typeBox);
    filterRow->addWidget(channelBox);
    filterRow->addWidget(searchButton);

    auto *resultList = new QTreeWidget(&dialog);
    resultList->setObjectName("eventSearchResultList");
    resultList->setSelectionMode(QAbstractItemView::SingleSelection);
    resultList->setRootIsDecorated(false);
    resultList->setItemsExpandable(false);
    resultList->setUniformRowHeights(true);
    resultList->setAlternatingRowColors(false);
    resultList->setHeaderLabels({
        QStringLiteral("시간"),
        QStringLiteral("채널"),
        QStringLiteral("이벤트 종류")
    });
    if (auto *header = resultList->header()) {
        header->setStretchLastSection(false);
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSortIndicator(0, Qt::DescendingOrder);
        header->setSortIndicatorShown(true);
    }
    resultList->setSortingEnabled(true);

    // Phase 6: 예전엔 팀 프로젝트의 고정 taxonomy 4종을 항상 옵션에 끼워 넣었는데, 지금 목업은
    // 그 타입들을 아예 안 만들어서 골라도 항상 결과 0개인 죽은 옵션으로 남아있었다. 실제로 들어온
    // 이벤트 타입만으로 적응형으로 채운다 — 서버가 붙어 그 타입의 이벤트가 실제로 오면 자동으로 나타난다.
    QSet<QString> typeLabels;
    for (const auto &eventInfo : EventUiHelpers::currentEvents()) {
        const QString typeLabel = displayEventTypeName(eventInfo.type).trimmed();
        if (!typeLabel.isEmpty()) {
            typeLabels.insert(typeLabel);
        }
    }
    QList<QString> sortedTypes = typeLabels.values();
    std::sort(sortedTypes.begin(), sortedTypes.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const QString &typeLabel : sortedTypes) {
        typeBox->addItem(typeLabel);
    }

    QSet<QString> channelLabels;
    for (const auto &eventInfo : EventUiHelpers::currentEvents()) {
        const QString label = shortChannelName(eventInfo).trimmed();
        if (!label.isEmpty()) {
            channelLabels.insert(label);
        }
    }
    QList<QString> sortedChannels = channelLabels.values();
    std::sort(sortedChannels.begin(), sortedChannels.end(), [](const QString &a, const QString &b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    for (const QString &name : sortedChannels) {
        channelBox->addItem(name);
    }

    auto refillResults = [resultList, fromDate, toDate, typeBox, channelBox]() {
        resultList->setSortingEnabled(false);
        resultList->clear();
        const auto events = EventUiHelpers::currentEvents();
        for (const auto &e : events) {
            QDateTime ts;
            if (!parseFlexibleTimestamp(e.timestamp, &ts)) {
                continue;
            }
            if (ts.date() < fromDate->date() || ts.date() > toDate->date()) {
                continue;
            }
            const QString typeName = displayEventTypeName(e.type);
            if (typeBox->currentText() != QStringLiteral("전체") && typeName != typeBox->currentText()) {
                continue;
            }
            const QString resolvedChannel = shortChannelName(e);
            if (channelBox->currentText() != QStringLiteral("전체") && resolvedChannel != channelBox->currentText()) {
                continue;
            }
            auto *item = new QTreeWidgetItem(resultList);
            item->setText(0, shortEventTimestamp(e.timestamp));
            item->setText(1, resolvedChannel);
            item->setText(2, typeName);
            item->setData(0, Qt::UserRole, e.eventId);
            item->setData(0, Qt::UserRole + 1, e.timestamp);
            item->setData(0, Qt::UserRole + 2, resolvedChannel);
            item->setData(0, Qt::UserRole + 3, e.type);
            item->setData(0, Qt::UserRole + 4, e.channelId);
        }
        resultList->setSortingEnabled(true);
        resultList->sortItems(0, Qt::DescendingOrder);
    };

    QObject::connect(searchButton, &QPushButton::clicked, &dialog, refillResults);
    QObject::connect(resultList, &QTreeWidget::itemClicked, &dialog, [&dialog](QTreeWidgetItem *item) {
        if (!item) {
            return;
        }
        EventInfo info;
        info.eventId = item->data(0, Qt::UserRole).toString();
        info.timestamp = item->data(0, Qt::UserRole + 1).toString();
        info.channel = item->data(0, Qt::UserRole + 2).toString();
        info.type = item->data(0, Qt::UserRole + 3).toString();
        info.channelId = item->data(0, Qt::UserRole + 4).toInt();
        EventUiHelpers::showEventDetailDialog(&dialog, info, false);
    });

    layout->addWidget(title);
    layout->addLayout(filterRow);
    layout->addWidget(resultList, 1);
    refillResults();
    dialog.exec();
}
void applyNotificationUnreadState(TopbarWidget *topbar, bool unread, int unreadCount)
{
    if (!topbar) {
        return;
    }
    topbar->setNotificationUnread(unread);
    Q_UNUSED(unreadCount);
    topbar->clearGlobalStatusMessage();
}

void openNotificationCenterDialog(QWidget *parent, TopbarWidget *topbar)
{
    if (g_eventService) {
        g_eventService->markAllRead();
    }
    EventUiHelpers::applyNotificationUnreadState(topbar, false, 0);

    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("알림센터"));
    dialog.setModal(true);
    dialog.setMinimumSize(520, 420);
    dialog.winId();
    applyNativeDarkTitleBar(&dialog);
    dialog.setObjectName("notificationCenterDialog");

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("알림센터"), &dialog);
    title->setObjectName("notificationCenterTitle");

    auto *summary = new QLabel(
        QStringLiteral("최근 24시간 이벤트를 최신순으로 확인합니다."),
        &dialog);
    summary->setWordWrap(true);
    summary->setObjectName("notificationCenterSummary");

    auto *resultList = new QTreeWidget(&dialog);
    resultList->setObjectName("notificationCenterList");
    resultList->setSelectionMode(QAbstractItemView::SingleSelection);
    resultList->setRootIsDecorated(false);
    resultList->setItemsExpandable(false);
    resultList->setUniformRowHeights(true);
    resultList->setAlternatingRowColors(false);
    resultList->setHeaderLabels({
        QStringLiteral("시간"),
        QStringLiteral("채널"),
        QStringLiteral("이벤트 종류")
    });
    if (auto *header = resultList->header()) {
        header->setStretchLastSection(true);
        header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        header->setSectionResizeMode(1, QHeaderView::Stretch);
        header->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        header->setSortIndicator(0, Qt::DescendingOrder);
        header->setSortIndicatorShown(true);
    }
    resultList->setSortingEnabled(true);

    auto *emptyLabel = new QLabel(QStringLiteral("최근 24시간에 표시할 이벤트가 없습니다."), &dialog);
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setObjectName("topbarGlobalStatus");
    emptyLabel->hide();

    auto refillResults = [resultList, emptyLabel](NotificationRange range) {
        resultList->setSortingEnabled(false);
        resultList->clear();
        const auto events = filteredNotificationEvents(range);
        if (events.isEmpty()) {
            emptyLabel->show();
            resultList->hide();
            resultList->setSortingEnabled(true);
            return;
        }
        emptyLabel->hide();
        resultList->show();
        for (const auto &eventInfo : events) {
            const QString channelName = shortChannelName(eventInfo);
            const QString typeName = displayEventTypeName(eventInfo.type);
            auto *item = new QTreeWidgetItem(resultList);
            item->setText(0, shortEventTimestamp(eventInfo.timestamp));
            item->setText(1, channelName);
            item->setText(2, typeName);
            item->setData(0, Qt::UserRole, eventInfo.eventId);
            item->setData(0, Qt::UserRole + 1, eventInfo.timestamp);
            item->setData(0, Qt::UserRole + 2, channelName);
            item->setData(0, Qt::UserRole + 3, eventInfo.type);
            item->setData(0, Qt::UserRole + 4, eventInfo.channelId);
        }
        resultList->setSortingEnabled(true);
        resultList->sortItems(0, Qt::DescendingOrder);
    };

    QObject::connect(resultList, &QTreeWidget::itemClicked, &dialog, [&dialog](QTreeWidgetItem *item) {
        if (!item) {
            return;
        }
        EventInfo info;
        info.eventId = item->data(0, Qt::UserRole).toString();
        info.timestamp = item->data(0, Qt::UserRole + 1).toString();
        info.channel = item->data(0, Qt::UserRole + 2).toString();
        info.type = item->data(0, Qt::UserRole + 3).toString();
        info.channelId = item->data(0, Qt::UserRole + 4).toInt();
        EventUiHelpers::showEventDetailDialog(&dialog, info, false);
    });

    layout->addWidget(title);
    layout->addWidget(summary);
    layout->addWidget(resultList, 1);
    layout->addWidget(emptyLabel, 1);
    refillResults(NotificationRange::Last1Day);
    dialog.exec();
}
} // namespace EventUiHelpers

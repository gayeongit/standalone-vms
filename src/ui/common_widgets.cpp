#include "common_widgets.h"

#include "app_state.h"
#include "common_ui.h"
#include "playback_service.h"

#include <QAbstractItemView>
#include <QDate>
#include <QDateTime>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtAlgorithms>

#include <algorithm>
#include <memory>

namespace {
class ElidedButton : public QPushButton
{
public:
    explicit ElidedButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setMinimumWidth(0);
    }

    void setFullText(const QString &text)
    {
        m_fullText = text;
        setToolTip(m_fullText);
        updateElidedText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QPushButton::resizeEvent(event);
        updateElidedText();
    }

private:
    void updateElidedText()
    {
        if (m_fullText.isEmpty()) {
            QPushButton::setText(QString());
            return;
        }
        QPushButton::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, std::max(0, contentsRect().width() - 8)));
    }

    QString m_fullText;
};

bool parseEventTimestamp(const QString &value, QDateTime *out)
{
    if (!out) {
        return false;
    }
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    QDateTime parsed = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(trimmed, Qt::ISODate);
    }
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(trimmed, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(trimmed, QStringLiteral("yyyy-MM-dd HH:mm"));
    }
    if (!parsed.isValid()) {
        return false;
    }

    *out = parsed.timeSpec() == Qt::LocalTime ? parsed : parsed.toLocalTime();
    return true;
}

QString eventTimestampShort(const QString &value)
{
    QDateTime parsed;
    if (!parseEventTimestamp(value, &parsed)) {
        return value;
    }
    return parsed.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
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

QString modelLabelWithIpSuffix(const QString &modelName, const QString &deviceIp, int deviceId);

QString eventChannelShort(const EventInfo &eventInfo)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        const bool matchedById = eventInfo.channelId >= 0 && ctx.channelId == eventInfo.channelId;
        const bool matchedByName = !eventInfo.channel.trimmed().isEmpty()
            && ctx.displayName.trimmed() == eventInfo.channel.trimmed();
        if (!matchedById && !matchedByName) {
            continue;
        }
        return modelLabelWithIpSuffix(ctx.model, ctx.deviceIp, ctx.deviceId);
    }

    return eventInfo.channel.trimmed().isEmpty() ? QStringLiteral("Unknown") : eventInfo.channel.trimmed();
}

QString eventSummaryText(const EventInfo &eventInfo)
{
    return QStringLiteral("%1 | %2 | %3")
        .arg(eventTimestampShort(eventInfo.timestamp),
             eventChannelShort(eventInfo),
             displayEventTypeName(eventInfo.type));
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

QPixmap centerCropPixmap(const QPixmap &pixmap, const QSize &targetSize)
{
    if (pixmap.isNull() || !targetSize.isValid()) {
        return {};
    }

    const QPixmap scaled = pixmap.scaled(targetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = std::max(0, (scaled.width() - targetSize.width()) / 2);
    const int y = std::max(0, (scaled.height() - targetSize.height()) / 2);
    return scaled.copy(x, y, targetSize.width(), targetSize.height());
}

QStringList playbackDateCandidates()
{
    QStringList dates;
    const QDate today = QDate::currentDate();
    for (int offset = 0; offset < 3; ++offset) {
        dates.append(today.addDays(-offset).toString(QStringLiteral("yyyy-MM-dd")));
    }
    return dates;
}

bool isPlaybackChannelSelected(const PlaybackChannelSummary &channel)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        const bool matchedById = ctx.channelId >= 0 && ctx.channelId == channel.channelId;
        const bool matchedByName = !ctx.displayName.trimmed().isEmpty()
            && !channel.name.trimmed().isEmpty()
            && ctx.displayName.trimmed() == channel.name.trimmed();
        if (matchedById || matchedByName) {
            return true;
        }
    }
    return false;
}

QFont sidebarModelFont(const QFont &baseFont)
{
    QFont font = baseFont;
    font.setBold(true);
    font.setPixelSize(12);
    return font;
}

QFont sidebarChannelFont(const QFont &baseFont)
{
    QFont font = baseFont;
    font.setBold(false);
    font.setPixelSize(12);
    return font;
}

QTreeWidgetItem *findTreeLeafItem(
    QTreeWidgetItem *root,
    const std::function<bool(QTreeWidgetItem *)> &predicate)
{
    if (!root) {
        return nullptr;
    }
    if (root->childCount() == 0 && predicate(root)) {
        return root;
    }
    for (int i = 0; i < root->childCount(); ++i) {
        if (auto *match = findTreeLeafItem(root->child(i), predicate)) {
            return match;
        }
    }
    return nullptr;
}

QTreeWidgetItem *findTreeLeafItem(
    QTreeWidget *tree,
    const std::function<bool(QTreeWidgetItem *)> &predicate)
{
    if (!tree) {
        return nullptr;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (auto *match = findTreeLeafItem(tree->topLevelItem(i), predicate)) {
            return match;
        }
    }
    return nullptr;
}

void applyTreeCurrentItem(QTreeWidget *tree, QTreeWidgetItem *item)
{
    if (!tree) {
        return;
    }
    tree->clearSelection();
    tree->setCurrentItem(nullptr);
    if (!item) {
        return;
    }
    tree->setCurrentItem(item);
    item->setSelected(true);
    tree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
}

QString deviceTypeForPlaybackChannel(const PlaybackChannelSummary &channel)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId >= 0 && ctx.channelId == channel.channelId) {
            return ctx.deviceType.trimmed().toUpper();
        }
        if (!ctx.displayName.trimmed().isEmpty() && ctx.displayName.trimmed() == channel.name.trimmed()) {
            return ctx.deviceType.trimmed().toUpper();
        }
    }
    return QStringLiteral("CCTV");
}

const SelectedChannelContext *playbackContextForChannel(const PlaybackChannelSummary &channel)
{
    const auto &contexts = AppState::instance().selectedChannelContexts;
    for (const auto &ctx : contexts) {
        if (ctx.channelId >= 0 && ctx.channelId == channel.channelId) {
            return &ctx;
        }
        if (!ctx.displayName.trimmed().isEmpty() && ctx.displayName.trimmed() == channel.name.trimmed()) {
            return &ctx;
        }
    }
    return nullptr;
}

void addPlaybackChannelsToDateRoot(QTreeWidgetItem *dateRoot, const PlaybackChannelsResult &result)
{
    if (!dateRoot) {
        return;
    }

    const auto staleChildren = dateRoot->takeChildren();
    qDeleteAll(staleChildren);
    if (!result.ok) {
        auto *errorItem = new QTreeWidgetItem(dateRoot, QStringList() << QStringLiteral("조회 실패"));
        errorItem->setFlags(errorItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
        return;
    }
    QVector<PlaybackChannelSummary> filteredChannels;
    filteredChannels.reserve(result.channels.size());
    for (const auto &channel : result.channels) {
        if (isPlaybackChannelSelected(channel)) {
            filteredChannels.push_back(channel);
        }
    }
    if (filteredChannels.isEmpty()) {
        auto *emptyItem = new QTreeWidgetItem(dateRoot, QStringList() << QStringLiteral("\uB179\uD654 \uC5C6\uC74C"));
        emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
        return;
    }

    auto *cctvRoot = new QTreeWidgetItem(dateRoot, QStringList() << QStringLiteral("CCTV"));
    auto *ugvRoot = new QTreeWidgetItem(dateRoot, QStringList() << QStringLiteral("UGV"));
    
    const QFont rootFont = sidebarModelFont(cctvRoot->font(0));
    
    for (auto* rootNode : {cctvRoot, ugvRoot}) {
        rootNode->setFlags(rootNode->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
        rootNode->setFont(0, rootFont);
        rootNode->setForeground(0, QBrush(QColor("#E5E5EA")));
    }

    QHash<QString, QTreeWidgetItem *> modelRoots;
    for (const auto &channel : filteredChannels) {
        auto *groupRoot = deviceTypeForPlaybackChannel(channel) == QStringLiteral("UGV") ? ugvRoot : cctvRoot;

        const SelectedChannelContext *ctx = playbackContextForChannel(channel);
        const QString modelName = (ctx && !ctx->model.trimmed().isEmpty())
            ? ctx->model.trimmed()
            : QStringLiteral("Unknown Model");
        const QString deviceIp = ctx ? ctx->deviceIp : QString();
        const int deviceId = ctx ? ctx->deviceId : -1;
        const QString modelLabel = modelLabelWithIpSuffix(modelName, deviceIp, deviceId);
        const QString modelKey = QStringLiteral("%1::%2")
                                     .arg(groupRoot == ugvRoot ? QStringLiteral("UGV") : QStringLiteral("CCTV"),
                                          QString::number(deviceId));

        auto *modelRoot = modelRoots.value(modelKey, nullptr);
        if (!modelRoot) {
            modelRoot = new QTreeWidgetItem(groupRoot, QStringList() << modelLabel);
            modelRoot->setFlags(modelRoot->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
            const QFont modelFont = sidebarModelFont(modelRoot->font(0));
            modelRoot->setFont(0, modelFont);
            modelRoot->setForeground(0, QBrush(QColor("#E5E5EA")));
            modelRoots.insert(modelKey, modelRoot);
        }

        const QString channelLabel = (ctx && ctx->channelNo >= 0)
            ? QStringLiteral("Channel %1").arg(ctx->channelNo)
            : channel.name;
        auto *item = new QTreeWidgetItem(modelRoot, QStringList() << channelLabel);
        const QFont itemFont = sidebarChannelFont(item->font(0));
        item->setFont(0, itemFont);
        item->setData(0, Qt::UserRole, channel.channelId);
        item->setData(0, Qt::UserRole + 1, result.date);
        item->setData(0, Qt::UserRole + 2, channel.name);
    }

    auto sortGroupTree = [](QTreeWidgetItem *groupRoot) {
        if (!groupRoot) {
            return;
        }
        for (int i = 0; i < groupRoot->childCount(); ++i) {
            if (auto *modelRoot = groupRoot->child(i)) {
                modelRoot->sortChildren(0, Qt::AscendingOrder);
            }
        }
        groupRoot->sortChildren(0, Qt::AscendingOrder);
    };
    sortGroupTree(cctvRoot);
    sortGroupTree(ugvRoot);

    if (cctvRoot->childCount() == 0) {
        delete cctvRoot;
        cctvRoot = nullptr;
    }
    if (ugvRoot->childCount() == 0) {
        delete ugvRoot;
        ugvRoot = nullptr;
    }
    if (!cctvRoot && !ugvRoot) {
        auto *emptyItem = new QTreeWidgetItem(dateRoot, QStringList() << QStringLiteral("\uB179\uD654 \uC5C6\uC74C"));
        emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
    }
}

} // namespace

void highlightSidebarChannelItem(QTreeWidget *tree, const QString &displayName)
{
    const QString target = displayName.trimmed();
    if (!tree) {
        return;
    }
    if (target.isEmpty()) {
        applyTreeCurrentItem(tree, nullptr);
        return;
    }

    auto *match = findTreeLeafItem(tree, [target](QTreeWidgetItem *item) {
        return item
            && item->data(0, Qt::UserRole + 2).toString().trimmed() == target;
    });
    applyTreeCurrentItem(tree, match);
}

void highlightSidebarPlaybackItem(QTreeWidget *tree, int channelId, const QString &displayName)
{
    const QString target = displayName.trimmed();
    if (!tree) {
        return;
    }

    auto *match = findTreeLeafItem(tree, [channelId, target](QTreeWidgetItem *item) {
        if (!item) {
            return false;
        }
        if (channelId >= 0 && item->data(0, Qt::UserRole).toInt() == channelId) {
            return true;
        }
        return !target.isEmpty()
            && item->data(0, Qt::UserRole + 2).toString().trimmed() == target;
    });
    applyTreeCurrentItem(tree, match);
}

TopbarWidget::TopbarWidget(const TopbarWidget::Config &config, QWidget *parent)
    : QFrame(parent)
{
    setObjectName("topbar");
    setFixedHeight(32);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 6, 0);
    layout->setSpacing(6);

    m_globalStatusLabel = new QLabel(this);
    m_globalStatusLabel->setObjectName("topbarGlobalStatus");
    m_globalStatusLabel->setVisible(false);
    layout->addWidget(m_globalStatusLabel);
    layout->addStretch();

    auto createTopbarIconButton = [](const QString &iconPath, const QString &tooltip, QWidget *parent) {
        auto *button = new QPushButton(parent);
        button->setObjectName("topbarIconButton");
        button->setIcon(QIcon(iconPath));
        button->setIconSize(QSize(15, 15));
        button->setFixedSize(24, 24);
        button->setToolTip(tooltip);
        button->setCursor(Qt::PointingHandCursor);
        button->setFlat(true);
        return button;
    };

    if (config.showNotification) {
        auto *notificationWrap = new QFrame(this);
        notificationWrap->setObjectName("topbarNotificationWrap");
        notificationWrap->setFixedSize(24, 24);

        auto *notificationButton = createTopbarIconButton(
            ":/styles/topbar_notify.svg",
            QStringLiteral("\uC54C\uB9BC"),
            notificationWrap);
        notificationButton->move(0, 0);
        connect(notificationButton, &QPushButton::clicked, this, &TopbarWidget::notificationCenterClicked);

        m_notificationBadge = new QLabel(notificationButton);
        m_notificationBadge->setObjectName("topbarNotifyBadge");
        m_notificationBadge->setFixedSize(8, 8);
        m_notificationBadge->hide();
        m_notificationBadge->move(15, 2);
        m_notificationBadge->raise();

        layout->addWidget(notificationWrap);
    }

    if (config.showSettings) {
        auto *settingsButton = createTopbarIconButton(
            ":/styles/topbar_settings.svg",
            QStringLiteral("\uC124\uC815"),
            this);
        layout->addWidget(settingsButton);
        connect(settingsButton, &QPushButton::clicked, this, &TopbarWidget::settingsClicked);
    }
    if (config.showLogout) {
        auto *logoutButton = createTopbarIconButton(
            ":/styles/topbar_logout.svg",
            QStringLiteral("\uB85C\uADF8\uC544\uC6C3"),
            this);
        layout->addWidget(logoutButton);
        connect(logoutButton, &QPushButton::clicked, this, &TopbarWidget::logoutClicked);
    }
}

void TopbarWidget::setNotificationUnread(bool unread)
{
    if (!m_notificationBadge) {
        return;
    }
    m_notificationBadge->setVisible(unread);
}

void TopbarWidget::clearGlobalStatusMessage()
{
    if (!m_globalStatusLabel) {
        return;
    }
    m_globalStatusLabel->clear();
    m_globalStatusLabel->hide();
}

EventViewWidget::EventViewWidget(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("eventViewPanel");
    setMinimumWidth(280);
    setMaximumWidth(480);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Search row (unified surface, no separate header frame)
    auto *searchRow = new QHBoxLayout();
    searchRow->setContentsMargins(10, 2, 8, 2);
    searchRow->setSpacing(4);
    auto *searchButton = new QPushButton(this);
    searchButton->setObjectName("eventViewSearchButton");
    searchButton->setIcon(QIcon(":/styles/search.svg"));
    searchButton->setIconSize(QSize(15, 15));
    searchButton->setFixedSize(24, 24);
    searchButton->setToolTip("Search");
    searchButton->setCursor(Qt::PointingHandCursor);
    searchButton->setFlat(true);
    searchRow->addStretch();
    searchRow->addWidget(searchButton, 0, Qt::AlignTop);
    layout->addLayout(searchRow);

    connect(searchButton, &QPushButton::clicked, this, &EventViewWidget::searchRequested);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("eventScrollArea");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *scrollContent = new QWidget(scroll);
    scroll->setWidget(scrollContent);
    m_contentLayout = new QVBoxLayout(scrollContent);
    m_contentLayout->setContentsMargins(0, 0, 0, 8);
    m_contentLayout->setSpacing(8);
    layout->addWidget(scroll, 1);
}

void EventViewWidget::setEvents(const QVector<EventInfo> &events, int maxItems, bool showDispatchButton)
{
    // 메인 이벤트뷰는 최근 이벤트를 빠르게 짚어보는 UI다.
    // 모든 항목을 텍스트 형태로 통일해서 정보 밀도와 확인을 높인다.
    if (!m_contentLayout) {
        return;
    }
    while (QLayoutItem *item = m_contentLayout->takeAt(0)) {
        if (item->widget()) {
            delete item->widget();
        }
        delete item;
    }

    const int count = maxItems > 0
        ? std::min(maxItems, static_cast<int>(events.size()))
        : static_cast<int>(events.size());
    for (int i = 0; i < count; ++i) {
        const EventInfo eventData = events[i];
        auto openDetail = [this, eventData, showDispatchButton]() {
            const bool dispatch = showEventDetailDialog(this, eventData, showDispatchButton);
            if (dispatch) {
                emit ugvDispatchRequested();
            }
        };
        const QString summary = eventSummaryText(events[i]);
        auto *rowWrap = new QWidget(this);
        auto *rowWrapLayout = new QHBoxLayout(rowWrap);
        rowWrapLayout->setContentsMargins(14, 0, 14, 0);
        rowWrapLayout->setSpacing(0);

        auto *row = new ElidedButton(rowWrap);
        row->setObjectName("eventButton");
        row->setMinimumWidth(0);
        row->setFullText(summary);
        row->setToolTip(QString());
        connect(row, &QPushButton::clicked, this, openDetail);
        rowWrapLayout->addWidget(row);
        m_contentLayout->addWidget(rowWrap);
    }
    m_contentLayout->addStretch();
}

SidebarWidget::SidebarWidget(const SidebarWidget::Config &config, QWidget *parent)
    : QFrame(parent)
{
    setObjectName("sidebar");
    setMinimumWidth(150);
    setMaximumWidth(240);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    if (config.showTabs) {
        auto *tabRow = new QFrame(this);
        tabRow->setObjectName("sidebarTabBar");
        tabRow->setFixedHeight(28);
        auto *tabLayout = new QHBoxLayout(tabRow);
        tabLayout->setContentsMargins(0, 0, 0, 0);
        tabLayout->setSpacing(0);
        m_channelTab = new QPushButton(QStringLiteral("채널"), tabRow);
        m_channelTab->setObjectName("tabInactive");
        m_playbackTab = new QPushButton(QStringLiteral("플레이백"), tabRow);
        m_playbackTab->setObjectName("tabInactive");
        tabLayout->addWidget(m_channelTab);
        tabLayout->addWidget(m_playbackTab);
        mainLayout->addWidget(tabRow);
    }

    auto *innerLayout = new QVBoxLayout();
    innerLayout->setContentsMargins(0, 0, 0, 0);
    innerLayout->setSpacing(0);

    // Middle area is split into top/bottom halves:
    // top = tabs/tree, bottom = screen-specific controls.
    auto *middleArea = new QWidget(this);
    auto *middleLayout = new QVBoxLayout(middleArea);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->setSpacing(0);

    auto *topRegion = new QWidget(middleArea);
    auto *topLayout = new QVBoxLayout(topRegion);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    m_preTreeLayout = new QVBoxLayout();
    m_preTreeLayout->setContentsMargins(0, 0, 0, 0);
    m_preTreeLayout->setSpacing(0);
    topLayout->addLayout(m_preTreeLayout);

    m_channelPage = new QWidget(this);
    auto *channelPageLayout = new QVBoxLayout(m_channelPage);
    channelPageLayout->setContentsMargins(0, 0, 0, 0);
    channelPageLayout->setSpacing(4);

    auto *cctvLabel = new QLabel(QStringLiteral("CCTV"), m_channelPage);
    cctvLabel->setObjectName("sidebarDeviceSectionLabel");
    channelPageLayout->addWidget(cctvLabel);

    m_channelTree = new QTreeWidget(m_channelPage);
    m_channelTree->setObjectName("channelTree");
    m_channelTree->setHeaderHidden(true);
    m_channelTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_channelTree->setIndentation(0);
    channelPageLayout->addWidget(m_channelTree, 3);

    auto *ugvLabel = new QLabel(QStringLiteral("UGV"), m_channelPage);
    ugvLabel->setObjectName("sidebarDeviceSectionLabel");
    channelPageLayout->addWidget(ugvLabel);

    m_ugvTree = new QTreeWidget(m_channelPage);
    m_ugvTree->setObjectName("channelTree");
    m_ugvTree->setHeaderHidden(true);
    m_ugvTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ugvTree->setIndentation(0);
    m_ugvTree->setExpandsOnDoubleClick(false);
    m_ugvTree->setItemsExpandable(false);
    channelPageLayout->addWidget(m_ugvTree, 1);

    m_playbackTree = createPlaybackTree(this);
    m_playbackTree->setItemsExpandable(false);
    m_playbackTree->setExpandsOnDoubleClick(false);
    m_treeStack = new QStackedWidget(this);
    m_treeStack->addWidget(m_channelPage);
    m_treeStack->addWidget(m_playbackTree);
    m_treeStack->setCurrentWidget(m_channelPage);
    topLayout->addWidget(m_treeStack, 1);

    auto *bottomRegion = new QWidget(middleArea);
    auto *bottomLayout = new QVBoxLayout(bottomRegion);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(4);

    m_controlsLayout = new QVBoxLayout();
    m_controlsLayout->setContentsMargins(0, 0, 0, 0);
    m_controlsLayout->setSpacing(4);
    bottomLayout->addLayout(m_controlsLayout);
    bottomLayout->addStretch();

    middleLayout->addWidget(topRegion, 1);
    middleLayout->addWidget(bottomRegion, 1);
    innerLayout->addWidget(middleArea, 1);

    if (config.showBottomButtons) {
        auto *sep = new QFrame(this);
        sep->setObjectName("sidebarSeparator");
        sep->setFixedHeight(1);
        innerLayout->addWidget(sep);

        m_actionStatusLabel = new QLabel(this);
        m_actionStatusLabel->setObjectName("sidebarActionStatus");
        m_actionStatusLabel->setWordWrap(true);
        m_actionStatusLabel->hide();
        innerLayout->addWidget(m_actionStatusLabel);

        auto *bottomButtons = new QHBoxLayout();
        bottomButtons->setContentsMargins(8, 8, 8, 8);
        bottomButtons->setSpacing(8);
        m_primaryBottomButton = new QPushButton(config.primaryBottomText, this);
        m_primaryBottomButton->setObjectName("snapshotButton");
        m_primaryBottomButton->setFixedHeight(30);
        m_secondaryBottomButton = new QPushButton(config.secondaryBottomText, this);
        m_secondaryBottomButton->setObjectName("clipButton");
        m_secondaryBottomButton->setFixedHeight(30);
        bottomButtons->addWidget(m_primaryBottomButton);
        bottomButtons->addWidget(m_secondaryBottomButton);
        innerLayout->addLayout(bottomButtons);
    }

    mainLayout->addLayout(innerLayout, 1);

    if (m_playbackTab) {
        connect(m_playbackTab, &QPushButton::clicked, this, [this]() {
            reloadPlaybackTree();
        });
    }
}


void SidebarWidget::populateChannelTree()
{
    // 사이드바 채널 트리는 AppState.selectedChannelContexts를 화면용 tree model로 재구성한다.
    // 선택된 채널 목록을 모델 루트와 채널 leaf로 나누어 CCTV/UGV 트리에 각각 채운다.
    // 여기서 중요한 것:
    // - CCTV/UGV를 서로 다른 tree로 분리하고, model/device/channel 계층을 다시 만들며
    // - UGV의 선택/드래그 불가 정책이 바로 반영된다는 것이다.
    if (!m_channelTree) {
        return;
    }
    m_channelTree->clear();
    if (m_ugvTree) {
        m_ugvTree->clear();
    }
    const auto &contexts = AppState::instance().selectedChannelContexts;

    QHash<QString, QTreeWidgetItem *> modelRoots;
    QSet<QString> existingLeafKeys;
    for (const auto &ctx : contexts) {
        const QString displayName = ctx.displayName.trimmed();
        if (displayName.isEmpty()) {
            continue;
        }
        const QString type = ctx.deviceType.trimmed().toUpper();
        if (type != QStringLiteral("CCTV") && type != QStringLiteral("UGV")) {
            continue;
        }
        existingLeafKeys.insert(QStringLiteral("%1|%2").arg(type, displayName));
        QTreeWidget *targetTree = (type == QStringLiteral("UGV") && m_ugvTree) ? m_ugvTree : m_channelTree;

        const QString modelName = ctx.model.trimmed().isEmpty()
            ? QStringLiteral("Unknown Model")
            : ctx.model.trimmed();
        const QString modelLabel = modelLabelWithIpSuffix(modelName, ctx.deviceIp, ctx.deviceId);
        const QString modelKey = QStringLiteral("%1::%2").arg(type, QString::number(ctx.deviceId));

        auto *modelItem = modelRoots.value(modelKey, nullptr);
        if (!modelItem) {
            modelItem = new QTreeWidgetItem(targetTree, QStringList() << modelLabel);
            modelItem->setFlags(modelItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
            const QFont modelFont = sidebarModelFont(modelItem->font(0));
            modelItem->setFont(0, modelFont);
            modelItem->setForeground(0, QBrush(QColor("#E5E5EA")));
            modelRoots.insert(modelKey, modelItem);
        }

        const QString channelLabel = ctx.channelNo >= 0
            ? QStringLiteral("Channel %1").arg(ctx.channelNo)
            : displayName;
        auto *item = new QTreeWidgetItem(modelItem, QStringList() << channelLabel);
        Qt::ItemFlags itemFlags = item->flags() | Qt::ItemIsEnabled;
        if (type == QStringLiteral("CCTV")) {
            itemFlags |= Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
        } else {
            itemFlags |= Qt::ItemIsSelectable;
            itemFlags &= ~Qt::ItemIsDragEnabled;
        }
        item->setFlags(itemFlags);
        const QFont channelFont = sidebarChannelFont(item->font(0));
        item->setFont(0, channelFont);
        item->setData(0, Qt::UserRole, ctx.channelId);
        item->setData(0, Qt::UserRole + 1, ctx.deviceId);
        item->setData(0, Qt::UserRole + 2, displayName);
        item->setData(0, Qt::UserRole + 3, type);
    }

    auto sortChannelTree = [](QTreeWidget *tree) {
        if (!tree) {
            return;
        }
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (auto *modelRoot = tree->topLevelItem(i)) {
                modelRoot->sortChildren(0, Qt::AscendingOrder);
            }
        }
        tree->sortItems(0, Qt::AscendingOrder);
    };
    sortChannelTree(m_channelTree);
    sortChannelTree(m_ugvTree);

    if (m_channelTree->topLevelItemCount() == 0) {
        auto *emptyItem = new QTreeWidgetItem(m_channelTree, QStringList() << QStringLiteral("선택된 장치 없음"));
        emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
    }
    m_channelTree->expandAll();
    if (m_ugvTree && m_ugvTree->topLevelItemCount() == 0) {
        auto *emptyItem = new QTreeWidgetItem(
            m_ugvTree,
            QStringList() << QStringLiteral("\uC5F0\uACB0\uB41C UGV \uC5C6\uC74C"));
        emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
    }
    if (m_ugvTree) {
        m_ugvTree->expandAll();
    }
}

void SidebarWidget::setPlaybackService(PlaybackService *service)
{
    m_playbackService = service;
    reloadPlaybackTree();
}

void SidebarWidget::reloadPlaybackTree()
{
    // Playback tree는 정적 목록이 아니라 "날짜별 서버 조회 결과"를 채워 넣는 lazy tree다.
    // 날짜 root를 먼저 만들고, 각 날짜별 재생 가능 채널을 비동기로 받아 트리에 채워 넣는다.
    // 사용자가 탭을 열었을 때만 필요한 정보만 로드한다.
    if (!m_playbackTree) {
        return;
    }

    ++m_playbackTreeLoadGeneration;
    const int generation = m_playbackTreeLoadGeneration;
    m_playbackTree->clear();

    // m_playbackService가 non-null인 건 "서버 설정(apiBaseUrl)이 있었다"는 뜻일 뿐,
    // "로그인했다"는 뜻이 아니다 — 게스트로 들어가도 로컬에 apiBaseUrl이 남아있으면
    // 서비스 객체 자체는 만들어진다. 실제로 필요한 건 로그인 여부라서 isAuthenticated도 같이 본다.
    if (!m_playbackService || !AppState::instance().isAuthenticated) {
        auto *loginItem = new QTreeWidgetItem(m_playbackTree, QStringList() << QStringLiteral("로그인이 필요한 기능입니다"));
        loginItem->setFlags(loginItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
        return;
    }

    const QStringList dates = playbackDateCandidates();
    if (dates.isEmpty()) {
        auto *emptyItem = new QTreeWidgetItem(m_playbackTree, QStringList() << QStringLiteral("\uB179\uD654 \uC5C6\uC74C"));
        emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
        return;
    }

    auto pendingCount = std::make_shared<int>(dates.size());
    auto addedAnyDate = std::make_shared<bool>(false);

    for (const QString &date : dates) {
        m_playbackService->fetchAvailableChannels(date, this, [this, generation, date, pendingCount, addedAnyDate](const PlaybackChannelsResult &result) {
            if (generation != m_playbackTreeLoadGeneration) {
                return;
            }

            if (result.ok) {
                QVector<PlaybackChannelSummary> filteredChannels;
                filteredChannels.reserve(result.channels.size());
                for (const auto &channel : result.channels) {
                    if (isPlaybackChannelSelected(channel)) {
                        filteredChannels.push_back(channel);
                    }
                }

                if (!filteredChannels.isEmpty()) {
                    PlaybackChannelsResult filteredResult = result;
                    filteredResult.channels = std::move(filteredChannels);

                    auto *dateRoot = new QTreeWidgetItem(m_playbackTree, QStringList() << date);
                    dateRoot->setFlags(dateRoot->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
                    QFont dateFont = dateRoot->font(0);
                    dateFont.setBold(true);
                    dateFont.setPixelSize(12);
                    dateRoot->setFont(0, dateFont);
                    dateRoot->setForeground(0, QBrush(QColor("#E5E5EA")));
                    dateRoot->setData(0, Qt::UserRole + 1, date);

                    addPlaybackChannelsToDateRoot(dateRoot, filteredResult);
                    *addedAnyDate = true;
                }
            }

            *pendingCount -= 1;
            if (*pendingCount <= 0) {
                if (!*addedAnyDate) {
                    auto *emptyItem = new QTreeWidgetItem(m_playbackTree, QStringList() << QStringLiteral("\uB179\uD654 \uC5C6\uC74C"));
                    emptyItem->setFlags(emptyItem->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsDragEnabled));
                } else {
                    m_playbackTree->sortItems(0, Qt::AscendingOrder);
                }
                m_playbackTree->expandAll();
            }
        });
    }
}

QPushButton *SidebarWidget::channelTab() const
{
    return m_channelTab;
}

QPushButton *SidebarWidget::playbackTab() const
{
    return m_playbackTab;
}

QWidget *SidebarWidget::channelPage() const
{
    return m_channelPage;
}

QTreeWidget *SidebarWidget::channelTree() const
{
    return m_channelTree;
}

QTreeWidget *SidebarWidget::cctvTree() const
{
    return m_channelTree;
}

QTreeWidget *SidebarWidget::ugvTree() const
{
    return m_ugvTree;
}

QTreeWidget *SidebarWidget::playbackTree() const
{
    return m_playbackTree;
}

QStackedWidget *SidebarWidget::treeStack() const
{
    return m_treeStack;
}

QPushButton *SidebarWidget::primaryBottomButton() const
{
    return m_primaryBottomButton;
}

QPushButton *SidebarWidget::secondaryBottomButton() const
{
    return m_secondaryBottomButton;
}

QLabel *SidebarWidget::actionStatusLabel() const
{
    return m_actionStatusLabel;
}

QVBoxLayout *SidebarWidget::preTreeLayout() const
{
    return m_preTreeLayout;
}

QVBoxLayout *SidebarWidget::controlsLayout() const
{
    return m_controlsLayout;
}


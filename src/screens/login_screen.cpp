#include "login_screen.h"
#include "common_ui.h"
#include "device_service.h"
#include "popup_manager.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QBrush>
#include <QSizePolicy>
#include <QScrollArea>
#include <QSharedPointer>
#include <QAbstractItemView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QVector>
#include <QFont>
#include <QPainter>
#include <QVariantAnimation>
#include <QStyleOptionButton>
#include <QStyle>
#include <QTimer>
#include <algorithm>

namespace {
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
    return deviceId >= 0
        ? QStringLiteral("%1 (#%2)").arg(trimmedModel, QString::number(deviceId))
        : trimmedModel;
}

QString deviceTreeStatusText(bool online, const QString &health)
{
    const QString normalizedHealth = health.trimmed().toUpper();
    if (!online) {
        return QStringLiteral("● 연결 안 됨");
    }
    if (normalizedHealth.contains(QStringLiteral("DELAY"))
        || normalizedHealth.contains(QStringLiteral("CONNECTING"))
        || normalizedHealth.contains(QStringLiteral("연결 지연"))
        || normalizedHealth.contains(QStringLiteral("연결중"))) {
        return QStringLiteral("● 연결 중");
    }
    return QStringLiteral("● 연결됨");
}

QString deviceTreeStatusState(bool online, const QString &health)
{
    const QString normalizedHealth = health.trimmed().toUpper();
    if (!online) {
        return QStringLiteral("disconnected");
    }
    if (normalizedHealth.contains(QStringLiteral("DELAY"))
        || normalizedHealth.contains(QStringLiteral("CONNECTING"))
        || normalizedHealth.contains(QStringLiteral("연결 지연"))
        || normalizedHealth.contains(QStringLiteral("연결중"))) {
        return QStringLiteral("delayed");
    }
    return QStringLiteral("connected");
}

QBrush deviceTreeStatusBrush(const QString &state)
{
    if (state == QStringLiteral("connected")) {
        return QBrush(QColor("#2ECC71"));
    }
    if (state == QStringLiteral("delayed")) {
        return QBrush(QColor("#F1C40F"));
    }
    return QBrush(QColor("#E74C3C"));
}
} // namespace

SpinningButton::SpinningButton(QWidget *parent)
    : QPushButton(parent)
{
    m_animation = new QVariantAnimation(this);
    m_animation->setStartValue(0.0);
    m_animation->setEndValue(360.0);
    m_animation->setDuration(900);
    m_animation->setLoopCount(-1); // Infinite
    
    connect(m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value){
        m_angle = value.toReal();
        update();
    });
}

void SpinningButton::setSpinning(bool spinning)
{
    if (m_spinning == spinning) return;
    m_spinning = spinning;
    
    if (m_spinning) {
        m_animation->start();
    } else {
        m_animation->stop();
        m_angle = 0;
        update();
    }
}

void SpinningButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QStyleOptionButton option;
    initStyleOption(&option);
    option.icon = QIcon();
    
    // Draw button background and border using style
    style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);

    // Draw rotating icon
    if (!icon().isNull()) {
        QRect iconRect = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
        QSize actualIconSize = iconSize();
        
        painter.save();
        painter.translate(iconRect.center());
        if (m_spinning) {
            painter.rotate(m_angle);
        }
        painter.drawPixmap(-actualIconSize.width() / 2, -actualIconSize.height() / 2, 
                           icon().pixmap(actualIconSize, QIcon::Normal, QIcon::Off));
        painter.restore();
    }
}

LoginScreen::LoginScreen(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("authScreen");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 18, 0, 18);
    layout->setSpacing(0);

    auto *logo = new QLabel(this);
    logo->setObjectName("authLogo");
    QPixmap logoPixmap(":/styles/clue_logo.png");
    if (logoPixmap.isNull()) {
        logoPixmap.load("styles/clue_logo.png");
    }
    if (!logoPixmap.isNull()) {
        logo->setPixmap(logoPixmap.scaledToHeight(48, Qt::SmoothTransformation));
    } else {
        logo->setText("CLUE");
    }

    auto *formWrap = new QWidget(this);
    formWrap->setObjectName("authFormWrap");
    formWrap->setFixedWidth(324);

    m_idEdit = new QLineEdit(this);
    m_pwEdit = new QLineEdit(this);
    m_idEdit->setObjectName("authInput");
    m_pwEdit->setObjectName("authInput");
    m_idEdit->setPlaceholderText("아이디");
    m_pwEdit->setPlaceholderText("비밀번호");
    m_pwEdit->setEchoMode(QLineEdit::Password);
    m_idEdit->setFixedHeight(38);
    m_pwEdit->setFixedHeight(38);

    m_loginButton = makePrimaryButton("로그인", this);
    m_loginButton->setObjectName("authPrimaryButton");
    m_loginButton->setFixedHeight(38);

    m_signupButton = new QPushButton("회원가입", this);
    m_signupButton->setObjectName("authSecondaryButton");
    m_signupButton->setFixedHeight(38);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("authStatusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();

    auto *divider = new QFrame(this);
    divider->setObjectName("authDivider");
    divider->setFrameShape(QFrame::HLine);

    auto *formLayout = new QVBoxLayout(formWrap);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);
    formLayout->addWidget(m_idEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_pwEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_statusLabel);
    formLayout->addSpacing(22);
    formLayout->addWidget(m_loginButton);
    formLayout->addSpacing(8);
    formLayout->addWidget(divider);
    formLayout->addSpacing(8);
    formLayout->addWidget(m_signupButton);

    layout->addStretch(1);
    layout->addSpacing(6);
    layout->addWidget(logo);
    layout->setAlignment(logo, Qt::AlignHCenter);
    layout->addSpacing(26);
    layout->addWidget(formWrap);
    layout->setAlignment(formWrap, Qt::AlignHCenter);
    layout->addStretch(1);

    connect(m_loginButton, &QPushButton::clicked, this, [this]() {
        clearLoginStatus();
        if (!m_idEdit || !m_pwEdit) {
            return;
        }
        const QString username = m_idEdit->text().trimmed();
        const QString password = m_pwEdit->text();
        if (username.isEmpty() || password.isEmpty()) {
            showLoginError("아이디/비밀번호를 입력해 주세요.");
            return;
        }
        emit loginRequested(username, password);
    });
    connect(m_signupButton, &QPushButton::clicked, this, &LoginScreen::signupRequested);
    connect(m_idEdit, &QLineEdit::returnPressed, m_loginButton, &QPushButton::click);
    connect(m_pwEdit, &QLineEdit::returnPressed, m_loginButton, &QPushButton::click);
}

void LoginScreen::setLoginInProgress(bool inProgress)
{
    if (m_loginButton) {
        m_loginButton->setEnabled(!inProgress);
        m_loginButton->setText(inProgress ? "로그인 중..." : "로그인");
    }
    if (m_signupButton) {
        m_signupButton->setEnabled(!inProgress);
    }
}

void LoginScreen::showLoginError(const QString &message)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(message.trimmed().isEmpty() ? "로그인에 실패했습니다." : message.trimmed());
    m_statusLabel->setProperty("state", "error");
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->show();
}

void LoginScreen::showConfigError(const QString &message)
{
    showLoginError(message.trimmed().isEmpty() ? "설정 파일 오류로 로그인할 수 없습니다." : message.trimmed());
    if (m_loginButton) {
        m_loginButton->setEnabled(false);
    }
}

void LoginScreen::clearLoginStatus()
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->clear();
    m_statusLabel->setProperty("state", QVariant());
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->hide();
}

void LoginScreen::resetLoginInputs()
{
    if (m_idEdit) {
        m_idEdit->clear();
        m_idEdit->setFocus();
    }
    if (m_pwEdit) {
        m_pwEdit->clear();
    }
    clearLoginStatus();
    setLoginInProgress(false);
}

SignupScreen::SignupScreen(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("authScreen");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 18, 0, 18);
    layout->setSpacing(0);

    auto *title = new QLabel("회원가입", this);
    title->setObjectName("authSubtitle");

    auto *formWrap = new QWidget(this);
    formWrap->setObjectName("authFormWrap");
    formWrap->setFixedWidth(324);

    m_nameEdit = new QLineEdit(this);
    m_idEdit = new QLineEdit(this);
    m_pwEdit = new QLineEdit(this);
    m_pwConfirmEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("authInput");
    m_idEdit->setObjectName("authInput");
    m_pwEdit->setObjectName("authInput");
    m_pwConfirmEdit->setObjectName("authInput");
    m_nameEdit->setPlaceholderText("이름");
    m_idEdit->setPlaceholderText("아이디");
    m_pwEdit->setPlaceholderText("비밀번호");
    m_pwConfirmEdit->setPlaceholderText("비밀번호 확인");
    m_pwEdit->setEchoMode(QLineEdit::Password);
    m_pwConfirmEdit->setEchoMode(QLineEdit::Password);
    m_nameEdit->setFixedHeight(38);
    m_idEdit->setFixedHeight(38);
    m_pwEdit->setFixedHeight(38);
    m_pwConfirmEdit->setFixedHeight(38);

    m_createButton = makePrimaryButton("계정등록", this);
    m_createButton->setObjectName("authPrimaryButton");
    m_createButton->setFixedHeight(38);

    m_backButton = new QPushButton("로그인으로 돌아가기", this);
    m_backButton->setObjectName("authSecondaryButton");
    m_backButton->setFixedHeight(38);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("authStatusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->hide();

    auto *divider = new QFrame(this);
    divider->setObjectName("authDivider");
    divider->setFrameShape(QFrame::HLine);

    auto *formLayout = new QVBoxLayout(formWrap);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);
    formLayout->addWidget(m_nameEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_idEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_pwEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_pwConfirmEdit);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_statusLabel);
    formLayout->addSpacing(22);
    formLayout->addWidget(m_createButton);
    formLayout->addSpacing(8);
    formLayout->addWidget(divider);
    formLayout->addSpacing(8);
    formLayout->addWidget(m_backButton);

    layout->addStretch(1);
    layout->addSpacing(6);
    layout->addWidget(title);
    layout->setAlignment(title, Qt::AlignHCenter);
    layout->addSpacing(26);
    layout->addWidget(formWrap);
    layout->setAlignment(formWrap, Qt::AlignHCenter);
    layout->addStretch(1);

    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        clearSignupStatus();
        if (!m_nameEdit || !m_idEdit || !m_pwEdit || !m_pwConfirmEdit) {
            return;
        }
        if (m_pwEdit->text() != m_pwConfirmEdit->text()) {
            showSignupError("비밀번호가 일치하지 않습니다.");
            return;
        }
        const QString name = m_nameEdit->text().trimmed();
        const QString username = m_idEdit->text().trimmed();
        const QString password = m_pwEdit->text();
        if (name.isEmpty() || username.isEmpty() || password.isEmpty()) {
            showSignupError("이름/아이디/비밀번호를 입력해 주세요.");
            return;
        }
        emit signupRequested(name, username, password);
    });
    connect(m_backButton, &QPushButton::clicked, this, &SignupScreen::backToLoginRequested);
    connect(m_nameEdit, &QLineEdit::returnPressed, m_createButton, &QPushButton::click);
    connect(m_idEdit, &QLineEdit::returnPressed, m_createButton, &QPushButton::click);
    connect(m_pwEdit, &QLineEdit::returnPressed, m_createButton, &QPushButton::click);
    connect(m_pwConfirmEdit, &QLineEdit::returnPressed, m_createButton, &QPushButton::click);
}

void SignupScreen::setSignupInProgress(bool inProgress)
{
    if (m_createButton) {
        m_createButton->setEnabled(!inProgress);
        m_createButton->setText(inProgress ? "생성 중..." : "계정 생성");
    }
    if (m_backButton) {
        m_backButton->setEnabled(!inProgress);
    }
}

void SignupScreen::showSignupError(const QString &message)
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->setText(message.trimmed().isEmpty() ? "회원가입에 실패했습니다." : message.trimmed());
    m_statusLabel->setProperty("state", "error");
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->show();
}

void SignupScreen::clearSignupStatus()
{
    if (!m_statusLabel) {
        return;
    }
    m_statusLabel->clear();
    m_statusLabel->setProperty("state", QVariant());
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->hide();
}

void SignupScreen::resetSignupInputs()
{
    if (m_nameEdit) {
        m_nameEdit->clear();
    }
    if (m_idEdit) {
        m_idEdit->clear();
    }
    if (m_pwEdit) {
        m_pwEdit->clear();
    }
    if (m_pwConfirmEdit) {
        m_pwConfirmEdit->clear();
    }
    if (m_nameEdit) {
        m_nameEdit->setFocus();
    }
}

DeviceCheckScreen::DeviceCheckScreen(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("authScreen");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 8, 18, 12);
    layout->setSpacing(0);

    auto *formWrap = new QWidget(this);
    formWrap->setObjectName("authFormWrap");
    formWrap->setFixedWidth(504);

    auto *formLayout = new QVBoxLayout(formWrap);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);

    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(12);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("deviceStatusLabel");
    m_statusLabel->hide();
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *cctvLabel = new QLabel("CCTV", this);
    cctvLabel->setObjectName("deviceScreenSectionLabel");
    auto *ugvLabel = new QLabel("UGV", this);
    ugvLabel->setObjectName("deviceScreenSectionLabel");

    auto setupTree = [](QTreeWidget *tree) {
        tree->setObjectName("deviceChannelTree");
        tree->setColumnCount(2);
        tree->setHeaderLabels({QString(), QString()});
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(true);
        tree->setUniformRowHeights(true);
        tree->setSelectionMode(QAbstractItemView::NoSelection);
        tree->setIndentation(0);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    };

    m_cctvTree = new QTreeWidget(this);
    setupTree(m_cctvTree);
    m_cctvTree->setMinimumHeight(120);

    m_ugvTree = new QTreeWidget(this);
    setupTree(m_ugvTree);
    m_ugvTree->setMinimumHeight(72);
 
    m_refreshButton = new SpinningButton(this);
    m_refreshButton->setObjectName("authSecondaryButton");
    m_refreshButton->setIcon(QIcon(":/styles/refresh.svg"));
    m_refreshButton->setIconSize(QSize(15, 15));
    m_refreshButton->setFixedWidth(28);
    m_refreshButton->setFixedHeight(28);
    m_refreshButton->setToolTip("새로고침");

    toolbar->addWidget(m_statusLabel, 1);
    toolbar->addWidget(m_refreshButton, 0, Qt::AlignRight);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(12);
    m_startButton = makePrimaryButton("VMS 시작", this);
    m_startButton->setObjectName("authPrimaryButton");
    m_startButton->setFixedHeight(38);
    m_startButton->setFixedWidth(246);
    
    auto *backButton = new QPushButton("로그인으로 돌아가기", this);
    backButton->setObjectName("authSecondaryButton");
    backButton->setFixedHeight(38);
    backButton->setFixedWidth(246);
    
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_startButton);
    buttonRow->addWidget(backButton);
    buttonRow->addStretch(1);

    layout->addSpacing(4);
    layout->addWidget(formWrap);
    layout->setAlignment(formWrap, Qt::AlignHCenter);

    formLayout->addLayout(toolbar);
    formLayout->addSpacing(6);
    formLayout->addWidget(cctvLabel);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_cctvTree, 5);
    formLayout->addSpacing(10);
    formLayout->addWidget(ugvLabel);
    formLayout->addSpacing(6);
    formLayout->addWidget(m_ugvTree, 1);
    formLayout->addSpacing(10);
    formLayout->addLayout(buttonRow);

    connect(m_refreshButton, &QPushButton::clicked, this, [this]() {
        if (m_refreshButton && m_refreshButton->isSpinning()) {
            return;
        }
        reloadDevices();
    });
    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        const auto selected = selectedContexts();
        if (selected.isEmpty()) {
            PopupManager::showInfo(this, "장치 탐색", "장치 미선택 시 VMS에 진입할 수 없습니다.");
            return;
        }
        emit startRequested(selected);
    });
    auto connectTreeToggle = [this](QTreeWidget *tree) {
        connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
            Q_UNUSED(column);
            if (!item || item->childCount() > 0) {
                return;
            }
            const Qt::CheckState next = (item->checkState(0) == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            item->setCheckState(0, next);
        });
    };
    connectTreeToggle(m_cctvTree);
    connectTreeToggle(m_ugvTree);
    connect(backButton, &QPushButton::clicked, this, &DeviceCheckScreen::backToLoginRequested);

}

void DeviceCheckScreen::setDeviceService(DeviceService *service)
{
    m_deviceService = service;
}

void DeviceCheckScreen::refreshDevices()
{
    reloadDevices();
}

void DeviceCheckScreen::reloadDevices(int retryCount)
{
    // DeviceCheck의 핵심은 "장치 목록 + 장치별 채널"을 비동기 fan-out으로 모으는 것이다.
    // 먼저 장치 목록을 가져오고, 각 장치의 채널을 제한된 동시성으로 조회한 뒤 결과를 트리에 반영한다.
    // 첫 진입 시 로그인 직후 네트워크가 몰리는 문제를 줄이기 위해:
    // - reload generation으로 stale callback을 차단하고
    // - devices fetch 실패(Operation canceled) 1회 재시도
    // - channels fetch는 제한된 동시성으로 펌프한다.
    if (!m_cctvTree || !m_ugvTree) {
        return;
    }
    if (!m_deviceService) {
        showDeviceStatusMessage("DeviceService 미연결", "error");
        m_cctvTree->clear();
        m_ugvTree->clear();
        setUiBusy(false);
        return;
    }
    
    if (m_statusLabel) {
        m_statusLabel->hide();
    }
    
    if (m_refreshButton) {
        m_refreshButton->setIcon(QIcon(":/styles/spinner_refresh.svg"));
        m_refreshButton->setSpinning(true);
    }

    showDeviceStatusMessage("장치 목록 불러오는 중...", "info");
    setUiBusy(true);
    const int reloadGeneration = ++m_reloadGeneration;

    m_deviceService->fetchDevices(this, [this, reloadGeneration, retryCount](const DeviceServiceResult &result) {
        if (!m_cctvTree || !m_ugvTree) {
            setUiBusy(false);
            return;
        }
        if (reloadGeneration != m_reloadGeneration) {
            return;
        }
        if (!result.ok) {
            if (retryCount < 1 && result.errorMessage.contains(QStringLiteral("Operation canceled"), Qt::CaseInsensitive)) {
                showDeviceStatusMessage(QStringLiteral("장치 목록 다시 불러오는 중..."), QStringLiteral("info"));
                QTimer::singleShot(250, this, [this, retryCount]() {
                    reloadDevices(retryCount + 1);
                });
                return;
            }
            showDeviceStatusMessage(describeFetchError(result), "error");
            m_cctvTree->clear();
            m_ugvTree->clear();
            setUiBusy(false);
            return;
        }

        if (result.devices.isEmpty()) {
            applyDeviceTree({});
            showDeviceStatusMessage("장치가 없습니다.", "info");
            setUiBusy(false);
            return;
        }

        auto devices = QSharedPointer<QVector<DeviceSummary>>::create(result.devices);
        auto contexts = QSharedPointer<QVector<SelectedChannelContext>>::create();
        auto nextIndex = QSharedPointer<int>::create(0);
        auto inFlight = QSharedPointer<int>::create(0);
        auto completed = QSharedPointer<int>::create(0);
        auto hadError = QSharedPointer<bool>::create(false);
        constexpr int kMaxConcurrentChannelLoads = 4;
        constexpr int kChannelRetryDelayMs = 150;
        auto finish = [this, contexts, hadError, reloadGeneration]() {
            if (reloadGeneration != m_reloadGeneration) {
                return;
            }
            applyDeviceTree(*contexts);
            if (contexts->isEmpty()) {
                showDeviceStatusMessage("채널이 없습니다.", "info");
            } else if (*hadError) {
                showDeviceStatusMessage(QString("일부 채널 조회 실패 (총 %1개 채널)").arg(contexts->size()), "error");
            } else {
                showDeviceStatusMessage(QString("채널 %1건").arg(contexts->size()), "success");
            }
            setUiBusy(false);
        };

        auto pump = QSharedPointer<std::function<void()>>::create();
        auto fetchDeviceWithRetry = QSharedPointer<std::function<void(const DeviceSummary &, int)>>::create();

        *pump = [this, devices, nextIndex, inFlight, completed, hadError, contexts, finish, reloadGeneration, pump, fetchDeviceWithRetry]() {
            if (reloadGeneration != m_reloadGeneration) {
                return;
            }
            while (*inFlight < kMaxConcurrentChannelLoads && *nextIndex < devices->size()) {
                const DeviceSummary device = devices->at(*nextIndex);
                *nextIndex += 1;
                *inFlight += 1;
                (*fetchDeviceWithRetry)(device, 0);
            }
            if (*completed >= devices->size()) {
                finish();
            }
        };

        *fetchDeviceWithRetry = [this, contexts, inFlight, completed, hadError, finish, reloadGeneration, pump, fetchDeviceWithRetry](const DeviceSummary &device, int attempt) {
            m_deviceService->fetchDeviceChannels(device.deviceId, this, [this, device, attempt, contexts, inFlight, completed, hadError, finish, reloadGeneration, pump, fetchDeviceWithRetry](const DeviceChannelsResult &channelResult) {
                if (reloadGeneration != m_reloadGeneration) {
                    return;
                }
                if (!channelResult.ok && attempt < 1) {
                    QTimer::singleShot(kChannelRetryDelayMs, this, [fetchDeviceWithRetry, device, attempt]() {
                        (*fetchDeviceWithRetry)(device, attempt + 1);
                    });
                    return;
                }

                if (!channelResult.ok) {
                    *hadError = true;
                } else {
                    for (const auto &channel : channelResult.channels) {
                        SelectedChannelContext ctx;
                        ctx.deviceId = device.deviceId;
                        ctx.channelId = channel.channelId;
                        ctx.channelNo = channel.channelNo;
                        ctx.deviceIp = device.ip.trimmed();
                        const QString channelLabel = channel.name.trimmed().isEmpty()
                            ? (channel.channelNo >= 0 ? QString("CH%1").arg(channel.channelNo) : QString("CH"))
                            : channel.name.trimmed();
                        ctx.displayName = (device.channelCount > 1 || channelResult.channels.size() > 1)
                            ? QString("%1 - %2").arg(device.name, channelLabel)
                            : device.name;
                        ctx.deviceType = device.type.trimmed().isEmpty() ? "UNKNOWN" : device.type.trimmed().toUpper();
                        ctx.model = device.model;
                        ctx.online = device.online;
                        ctx.health = device.health.trimmed().isEmpty()
                            ? (device.online ? "OK" : "DOWN")
                            : device.health.trimmed();
                        contexts->push_back(ctx);
                    }
                }

                *completed += 1;
                *inFlight -= 1;
                (*pump)();
            });
        };

        (*pump)();
    });
}

void DeviceCheckScreen::applyDeviceTree(const QVector<SelectedChannelContext> &contexts)
{
    // 장치확인은 DeviceSummary 원본을 그대로 보여주지 않고,
    // 정규화된 SelectedChannelContext 목록을 CCTV/UGV tree item 구조로 다시 변환해 렌더링한다.
    // 런타임에서 실제로 쓰는 SelectedChannelContext를 tree model로 투영한다.
    // 이 단계에서 CCTV/UGV를 분리하고, model root / channel leaf / custom device fallback까지
    // 모두 "사이드바/자동배치에 재사용 가능한 형태"로 정규화한다.
    if (!m_cctvTree || !m_ugvTree) {
        return;
    }
    m_cctvTree->clear();
    m_ugvTree->clear();

    constexpr int kRoleDeviceId = Qt::UserRole + 1;
    constexpr int kRoleDisplayName = Qt::UserRole + 2;
    constexpr int kRoleDeviceType = Qt::UserRole + 3;
    constexpr int kRoleModel = Qt::UserRole + 4;
    constexpr int kRoleOnline = Qt::UserRole + 5;
    constexpr int kRoleHealth = Qt::UserRole + 6;
    constexpr int kRoleChannelId = Qt::UserRole + 7;
    constexpr int kRoleChannelNo = Qt::UserRole + 8;
    constexpr int kRoleDeviceIp = Qt::UserRole + 9;

    auto populateTree = [&](QTreeWidget *tree, const QString &targetType) {
        QMap<QString, QTreeWidgetItem *> modelNodes;
        for (const auto &ctx : contexts) {
            const QString type = ctx.deviceType.trimmed().isEmpty() ? "UNKNOWN" : ctx.deviceType.trimmed().toUpper();
            if (type != targetType) {
                continue;
            }

            const QString model = ctx.model.trimmed().isEmpty() ? "Unknown Model" : ctx.model.trimmed();
            const QString modelKey = model + "|" + QString::number(ctx.deviceId);
            if (!modelNodes.contains(modelKey)) {
                const QString modelLabel = modelLabelWithIpSuffix(model, ctx.deviceIp, ctx.deviceId);
                auto *modelItem = new QTreeWidgetItem(tree, QStringList() << modelLabel << "");
                modelItem->setFlags(modelItem->flags() & ~Qt::ItemIsSelectable);
                QFont modelFont = modelItem->font(0);
                modelFont.setBold(true);
                modelFont.setPixelSize(12);
                modelItem->setFont(0, modelFont);
                modelItem->setForeground(0, QBrush(QColor("#E5E5EA")));
                modelNodes.insert(modelKey, modelItem);
            }

            const QString statusState = deviceTreeStatusState(ctx.online, ctx.health);
            const QString status = deviceTreeStatusText(ctx.online, ctx.health);
            const QString channelLabel = (ctx.channelNo >= 0)
                ? QString("Channel %1").arg(ctx.channelNo)
                : (!ctx.displayName.trimmed().isEmpty() ? ctx.displayName.trimmed() : QString("Channel"));

            auto *leaf = new QTreeWidgetItem(modelNodes[modelKey], QStringList() << channelLabel << status);
            leaf->setFlags((leaf->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsSelectable);
            leaf->setCheckState(0, ctx.online ? Qt::Checked : Qt::Unchecked);
            if (!ctx.online) {
                leaf->setDisabled(true);
            }

            QFont channelFont = leaf->font(0);
            channelFont.setPixelSize(12);
            leaf->setFont(0, channelFont);
            leaf->setFont(1, channelFont);
            leaf->setForeground(1, deviceTreeStatusBrush(statusState));

            leaf->setData(1, Qt::UserRole, statusState);
            leaf->setData(0, kRoleDeviceId, ctx.deviceId);
            leaf->setData(0, kRoleDisplayName, ctx.displayName);
            leaf->setData(0, kRoleDeviceType, ctx.deviceType);
            leaf->setData(0, kRoleModel, ctx.model);
            leaf->setData(0, kRoleOnline, ctx.online);
            leaf->setData(0, kRoleHealth, ctx.health);
            leaf->setData(0, kRoleChannelId, ctx.channelId);
            leaf->setData(0, kRoleChannelNo, ctx.channelNo);
            leaf->setData(0, kRoleDeviceIp, ctx.deviceIp);
            leaf->setData(1, Qt::TextAlignmentRole, static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
        }
        auto sortModelChildren = [&](QTreeWidgetItem *modelRoot) {
            if (!modelRoot) {
                return;
            }
            QList<QTreeWidgetItem *> children = modelRoot->takeChildren();
            std::sort(children.begin(), children.end(), [](QTreeWidgetItem *lhs, QTreeWidgetItem *rhs) {
                if (!lhs || !rhs) {
                    return lhs < rhs;
                }
                const bool lOnline = lhs->data(0, kRoleOnline).toBool();
                const bool rOnline = rhs->data(0, kRoleOnline).toBool();
                if (lOnline != rOnline) {
                    return lOnline; // connected first
                }
                return lhs->text(0).localeAwareCompare(rhs->text(0)) < 0;
            });
            modelRoot->addChildren(children);
        };

        for (auto it = modelNodes.constBegin(); it != modelNodes.constEnd(); ++it) {
            sortModelChildren(it.value());
        }

        QList<QTreeWidgetItem *> topItems;
        while (tree->topLevelItemCount() > 0) {
            topItems.push_back(tree->takeTopLevelItem(0));
        }
        auto hasOnlineChild = [&](QTreeWidgetItem *modelRoot) {
            if (!modelRoot) {
                return false;
            }
            for (int i = 0; i < modelRoot->childCount(); ++i) {
                if (auto *child = modelRoot->child(i); child && child->data(0, kRoleOnline).toBool()) {
                    return true;
                }
            }
            return false;
        };
        std::sort(topItems.begin(), topItems.end(), [&](QTreeWidgetItem *lhs, QTreeWidgetItem *rhs) {
            if (!lhs || !rhs) {
                return lhs < rhs;
            }
            const bool lHasOnline = hasOnlineChild(lhs);
            const bool rHasOnline = hasOnlineChild(rhs);
            if (lHasOnline != rHasOnline) {
                return lHasOnline; // connected group first
            }
            return lhs->text(0).localeAwareCompare(rhs->text(0)) < 0;
        });
        for (auto *item : topItems) {
            tree->addTopLevelItem(item);
        }
        tree->expandAll();
    };

    populateTree(m_cctvTree, QStringLiteral("CCTV"));
    populateTree(m_ugvTree, QStringLiteral("UGV"));
}

QVector<SelectedChannelContext> DeviceCheckScreen::selectedContexts() const
{
    QVector<SelectedChannelContext> out;
    if (!m_cctvTree || !m_ugvTree) {
        return out;
    }

    constexpr int kRoleDeviceId = Qt::UserRole + 1;
    constexpr int kRoleDisplayName = Qt::UserRole + 2;
    constexpr int kRoleDeviceType = Qt::UserRole + 3;
    constexpr int kRoleModel = Qt::UserRole + 4;
    constexpr int kRoleOnline = Qt::UserRole + 5;
    constexpr int kRoleHealth = Qt::UserRole + 6;
    constexpr int kRoleChannelId = Qt::UserRole + 7;
    constexpr int kRoleChannelNo = Qt::UserRole + 8;
    constexpr int kRoleDeviceIp = Qt::UserRole + 9;

    auto appendSelectedFromTree = [&](QTreeWidget *tree) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            QTreeWidgetItem *item = *it;
            ++it;
            if (!item || item->childCount() > 0) {
                continue;
            }
            if (item->checkState(0) != Qt::Checked) {
                continue;
            }
            SelectedChannelContext ctx;
            bool idOk = false;
            const int parsedId = item->data(0, kRoleDeviceId).toInt(&idOk);
            ctx.deviceId = idOk ? parsedId : -1;
            bool channelIdOk = false;
            const int parsedChannelId = item->data(0, kRoleChannelId).toInt(&channelIdOk);
            ctx.channelId = channelIdOk ? parsedChannelId : -1;
            bool channelNoOk = false;
            const int parsedChannelNo = item->data(0, kRoleChannelNo).toInt(&channelNoOk);
            ctx.channelNo = channelNoOk ? parsedChannelNo : -1;
            ctx.displayName = item->data(0, kRoleDisplayName).toString();
            ctx.deviceIp = item->data(0, kRoleDeviceIp).toString();
            ctx.deviceType = item->data(0, kRoleDeviceType).toString();
            ctx.model = item->data(0, kRoleModel).toString();
            ctx.online = item->data(0, kRoleOnline).toBool();
            ctx.health = item->data(0, kRoleHealth).toString();
            out.push_back(ctx);
        }
    };

    appendSelectedFromTree(m_cctvTree);
    appendSelectedFromTree(m_ugvTree);
    return out;
}

void DeviceCheckScreen::setUiBusy(bool busy)
{
    if (m_refreshButton) {
        if (!busy) {
            m_refreshButton->setSpinning(false);
            m_refreshButton->setIcon(QIcon(":/styles/refresh.svg"));
        }
    }
    if (m_startButton) {
        m_startButton->setEnabled(!busy);
    }
    if (m_cctvTree) {
        m_cctvTree->setEnabled(!busy);
    }
    if (m_ugvTree) {
        m_ugvTree->setEnabled(!busy);
    }
}

void DeviceCheckScreen::showDeviceStatusMessage(const QString &message, const QString &state)
{
    if (!m_statusLabel) {
        return;
    }
    const QString text = message.trimmed().isEmpty() ? "상태 없음" : message.trimmed();
    m_statusLabel->setText(text);
    m_statusLabel->setProperty("channelState", QVariant());
    if (state == "success") {
        m_statusLabel->setProperty("state", "success");
        m_statusLabel->setStyleSheet("color: #FFFFFF; font-weight: 600;");
    } else if (state == "error") {
        m_statusLabel->setProperty("state", "error");
        m_statusLabel->setStyleSheet("color: #FF6B6B; font-weight: 600;");
    } else {
        m_statusLabel->setProperty("state", "info");
        m_statusLabel->setStyleSheet("color: #FFFFFF; font-weight: 600;");
    }
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->show();
}

QString DeviceCheckScreen::describeFetchError(const DeviceServiceResult &result) const
{
    if (result.httpStatus == 401) {
        return "인증이 만료되었습니다. 다시 로그인해 주세요.";
    }
    if (result.httpStatus > 0) {
        return QString("장치 목록 조회 실패 (HTTP %1)").arg(result.httpStatus);
    }
    if (!result.errorMessage.trimmed().isEmpty()) {
        return QString("장치 목록 조회 실패: %1").arg(result.errorMessage.trimmed());
    }
    return "장치 목록 조회 실패: 알 수 없는 오류";
}

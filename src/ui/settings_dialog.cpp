#include "settings_dialog.h"

#include "common_ui.h"
#include "popup_manager.h"

#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>
#include <functional>
#include <memory>
namespace {

struct ManagedDeviceEntry {
    QString name;
    QString type;
    QString url;
};

QVector<ManagedDeviceEntry> loadManagedDevices()
{
    QVector<ManagedDeviceEntry> result;
    QSettings settings("TeamClue", "VMS_v1");
    const int size = settings.beginReadArray("devices");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        ManagedDeviceEntry e;
        e.name = settings.value("name").toString().trimmed();
        e.type = settings.value("type").toString().trimmed();
        e.url = settings.value("url").toString().trimmed();
        if (!e.name.isEmpty()) {
            result.push_back(e);
        }
    }
    settings.endArray();

    return result;
}

void saveManagedDevices(const QVector<ManagedDeviceEntry> &devices)
{
    QSettings settings("TeamClue", "VMS_v1");
    settings.beginWriteArray("devices");
    for (int i = 0; i < devices.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", devices[i].name);
        settings.setValue("type", devices[i].type);
        settings.setValue("url", devices[i].url);
    }
    settings.endArray();
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("settingsDialog");
    setWindowTitle("설정");
    setFixedSize(560, 380);
    winId();
    applyNativeDarkTitleBar(this);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(6);


    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("settingsTabs");
    root->addWidget(tabs, 1);

    // Device tab
    auto *devicePage = new QWidget(this);
    auto *deviceLayout = new QVBoxLayout(devicePage);
    deviceLayout->setContentsMargins(6, 6, 6, 6);
    deviceLayout->setSpacing(6);
    auto *deviceList = new QListWidget(devicePage);
    deviceList->setObjectName("settingsDeviceList");
    auto *deviceBtns = new QHBoxLayout();
    auto *addBtn = new QPushButton("추가", devicePage);
    auto *updateBtn = new QPushButton("수정", devicePage);
    auto *deleteBtn = new QPushButton("삭제", devicePage);
    deviceBtns->addWidget(addBtn);
    deviceBtns->addWidget(updateBtn);
    deviceBtns->addWidget(deleteBtn);
    deviceLayout->addWidget(new QLabel("장치 목록", devicePage));
    deviceLayout->addWidget(deviceList, 1);
    deviceLayout->addWidget(new QLabel("목록 더블클릭 또는 [수정]으로 장치 정보를 수정할 수 있습니다.", devicePage));
    deviceLayout->addLayout(deviceBtns);
    tabs->addTab(devicePage, "장치 관리");

    auto devices = std::make_shared<QVector<ManagedDeviceEntry>>(loadManagedDevices());
    auto refreshDeviceList = [deviceList, devices]() {
        deviceList->clear();
        for (const auto &d : *devices) {
            deviceList->addItem(QString("%1 [%2]").arg(d.name, d.type));
        }
    };
    refreshDeviceList();

    std::function<bool(const QString &, ManagedDeviceEntry *)> openDeviceEditor =
        [this](const QString &title, ManagedDeviceEntry *entry) -> bool {
            if (!entry) {
                return false;
            }

            QDialog editor(this);
            editor.setObjectName("settingsDialogEditor");
            editor.setWindowTitle(title);
            editor.setMinimumWidth(420);
            editor.winId();
            applyNativeDarkTitleBar(&editor);

            auto *layout = new QVBoxLayout(&editor);
            auto *nameEdit = new QLineEdit(&editor);
            auto *typeBox = new QComboBox(&editor);
            auto *urlEdit = new QLineEdit(&editor);
            typeBox->addItems({"CCTV", "UGV"});
            nameEdit->setPlaceholderText("채널명 (예: Channel 4)");
            urlEdit->setPlaceholderText("RTSP URL (예: rtsp://127.0.0.1:8554/ch4)");
            nameEdit->setText(entry->name);
            typeBox->setCurrentText(entry->type.isEmpty() ? "CCTV" : entry->type);
            urlEdit->setText(entry->url);
            layout->addWidget(new QLabel("종류", &editor));
            layout->addWidget(typeBox);
            layout->addWidget(new QLabel("채널명", &editor));
            layout->addWidget(nameEdit);
            layout->addWidget(new QLabel("RTSP URL", &editor));
            layout->addWidget(urlEdit);

            auto *btnRow = new QHBoxLayout();
            auto *okBtn = new QPushButton("완료", &editor);
            auto *cancelBtn = new QPushButton("취소", &editor);
            const QSize cancelSize = cancelBtn->sizeHint();
            okBtn->setFixedSize(cancelSize);
            cancelBtn->setFixedSize(cancelSize);
            okBtn->setStyleSheet(QStringLiteral("background: #F37321; color: #FFFFFF;"));
            btnRow->addStretch();
            btnRow->addWidget(okBtn);
            btnRow->addWidget(cancelBtn);
            layout->addLayout(btnRow);
            connect(okBtn, &QPushButton::clicked, &editor, &QDialog::accept);
            connect(cancelBtn, &QPushButton::clicked, &editor, &QDialog::reject);

            if (editor.exec() != QDialog::Accepted) {
                return false;
            }

            const QString name = nameEdit->text().trimmed();
            const QString type = typeBox->currentText().trimmed();
            const QString url = urlEdit->text().trimmed();
            if (name.isEmpty() || url.isEmpty()) {
                PopupManager::showInfo(this, "장치 관리", "장치명과 RTSP URL을 입력해 주세요.");
                return false;
            }

            entry->name = name;
            entry->type = type;
            entry->url = url;
            return true;
        };

    auto hasDuplicateDevice = [devices](const ManagedDeviceEntry &candidate, int selfIndex, QString *error) -> bool {
        for (int i = 0; i < devices->size(); ++i) {
            if (i == selfIndex) {
                continue;
            }
            if ((*devices)[i].name == candidate.name) {
                if (error) {
                    *error = QString("동일한 장치명이 이미 존재합니다: %1").arg((*devices)[i].name);
                }
                return true;
            }
            if ((*devices)[i].url == candidate.url) {
                if (error) {
                    *error = QString("동일한 RTSP URL이 이미 장치 '%1'에 등록되어 있습니다.").arg((*devices)[i].name);
                }
                return true;
            }
        }
        return false;
    };

    std::function<void()> editSelectedDevice = [this, deviceList, devices, openDeviceEditor, hasDuplicateDevice, refreshDeviceList]() {
        const int row = deviceList->currentRow();
        if (row < 0 || row >= devices->size()) {
            PopupManager::showInfo(this, "장치 관리", "수정할 장치를 선택해 주세요.");
            return;
        }

        ManagedDeviceEntry edited = (*devices)[row];
        if (!openDeviceEditor("장치 수정", &edited)) {
            return;
        }

        QString dupError;
        if (hasDuplicateDevice(edited, row, &dupError)) {
            PopupManager::showInfo(this, "장치 관리", dupError);
            return;
        }

        (*devices)[row] = edited;
        saveManagedDevices(*devices);
        m_devicesChanged = true;
        refreshDeviceList();
        deviceList->setCurrentRow(row);
    };

    connect(deviceList, &QListWidget::itemDoubleClicked, this, [editSelectedDevice](QListWidgetItem *) {
        editSelectedDevice();
    });
    connect(addBtn, &QPushButton::clicked, this, [this, devices, openDeviceEditor, hasDuplicateDevice, refreshDeviceList]() {
        ManagedDeviceEntry added;
        if (!openDeviceEditor("장치 추가", &added)) {
            return;
        }

        QString dupError;
        if (hasDuplicateDevice(added, -1, &dupError)) {
            PopupManager::showInfo(this, "장치 관리", dupError);
            return;
        }

        devices->push_back(added);
        saveManagedDevices(*devices);
        m_devicesChanged = true;
        refreshDeviceList();
    });
    connect(updateBtn, &QPushButton::clicked, this, [editSelectedDevice]() {
        editSelectedDevice();
    });
    connect(deleteBtn, &QPushButton::clicked, this, [this, deviceList, devices, refreshDeviceList]() {
        const int row = deviceList->currentRow();
        if (row < 0 || row >= devices->size()) {
            PopupManager::showInfo(this, "장치 관리", "삭제할 장치를 선택해 주세요.");
            return;
        }

        devices->removeAt(row);
        saveManagedDevices(*devices);
        m_devicesChanged = true;
        refreshDeviceList();
    });
    // Path tab
    auto *pathPage = new QWidget(this);
    auto *pathLayout = new QVBoxLayout(pathPage);
    pathLayout->setContentsMargins(6, 6, 6, 6);
    pathLayout->setSpacing(6);
    auto *snapshotPathEdit = new QLineEdit(pathPage);
    auto *snapshotBrowseBtn = new QPushButton("경로 선택", pathPage);
    auto *clipPathEdit = new QLineEdit(pathPage);
    auto *clipBrowseBtn = new QPushButton("경로 선택", pathPage);
    auto *resetPathBtn = new QPushButton("초기화", pathPage);
    auto *applyPathBtn = new QPushButton("경로 저장", pathPage);

    snapshotPathEdit->setText(snapshotSaveDirectory());
    clipPathEdit->setText(clipSaveDirectory());

    pathLayout->addWidget(new QLabel("스냅샷 저장 경로", pathPage));
    pathLayout->addWidget(snapshotPathEdit);
    pathLayout->addWidget(snapshotBrowseBtn);
    pathLayout->addSpacing(16);
    pathLayout->addWidget(new QLabel("클립 저장 경로", pathPage));
    pathLayout->addWidget(clipPathEdit);
    pathLayout->addWidget(clipBrowseBtn);
    pathLayout->addSpacing(14);
    auto *pathButtonRow = new QHBoxLayout();
    pathButtonRow->setContentsMargins(0, 0, 0, 0);
    pathButtonRow->setSpacing(6);
    pathButtonRow->addWidget(resetPathBtn);
    pathButtonRow->addWidget(applyPathBtn);
    pathLayout->addStretch();
    pathLayout->addLayout(pathButtonRow);
    tabs->addTab(pathPage, "저장 경로");

    connect(snapshotBrowseBtn, &QPushButton::clicked, this, [this, snapshotPathEdit]() {
        const QString selected = QFileDialog::getExistingDirectory(
            this,
            "스냅샷 저장 경로 선택",
            snapshotPathEdit->text().trimmed().isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                : snapshotPathEdit->text().trimmed());
        if (!selected.isEmpty()) {
            snapshotPathEdit->setText(selected);
        }
    });
    connect(clipBrowseBtn, &QPushButton::clicked, this, [this, clipPathEdit]() {
        const QString selected = QFileDialog::getExistingDirectory(
            this,
            "클립 저장 경로 선택",
            clipPathEdit->text().trimmed().isEmpty()
                ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
                : clipPathEdit->text().trimmed());
        if (!selected.isEmpty()) {
            clipPathEdit->setText(selected);
        }
    });
    connect(resetPathBtn, &QPushButton::clicked, this, [this, snapshotPathEdit, clipPathEdit]() {
        const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).trimmed();
        const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation).trimmed();
        const QString snapshotDefault = QDir(pictures.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : pictures)
                                            .filePath("snapshot");
        const QString clipDefault = QDir(videos.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : videos)
                                        .filePath("videoclip");
        QDir().mkpath(snapshotDefault);
        QDir().mkpath(clipDefault);
        snapshotPathEdit->setText(snapshotDefault);
        clipPathEdit->setText(clipDefault);
        QSettings settings("TeamClue", "VMS_v2");
        settings.setValue("paths/snapshotDir", snapshotDefault);
        settings.setValue("paths/clipDir", clipDefault);
    });
    connect(applyPathBtn, &QPushButton::clicked, this, [this, snapshotPathEdit, clipPathEdit]() {
        const QString snapshotPath = snapshotPathEdit->text().trimmed();
        const QString clipPath = clipPathEdit->text().trimmed();
        if (snapshotPath.isEmpty() || !QDir(snapshotPath).exists()) {
            PopupManager::showInfo(this, "저장 경로", "유효한 스냅샷 경로를 선택해 주세요.");
            return;
        }
        if (clipPath.isEmpty() || !QDir(clipPath).exists()) {
            PopupManager::showInfo(this, "저장 경로", "유효한 클립 경로를 선택해 주세요.");
            return;
        }
        const QFileInfo snapshotInfo(snapshotPath);
        if (!snapshotInfo.isWritable()) {
            PopupManager::showInfo(this, "저장 경로", "스냅샷 경로에 쓰기 권한이 없습니다.");
            return;
        }
        const QFileInfo clipInfo(clipPath);
        if (!clipInfo.isWritable()) {
            PopupManager::showInfo(this, "저장 경로", "클립 경로에 쓰기 권한이 없습니다.");
            return;
        }
        QSettings settings("TeamClue", "VMS_v2");
        settings.setValue("paths/snapshotDir", snapshotPath);
        settings.setValue("paths/clipDir", clipPath);
    });

    // Policy tab
    auto *policyPage = new QWidget(this);
    auto *policyLayout = new QVBoxLayout(policyPage);
    policyLayout->setContentsMargins(6, 6, 6, 6);
    policyLayout->setSpacing(6);
    auto *policyTitle = new QLabel("정책 안내", policyPage);
    auto *policyInfoLabel = new QLabel(policyPage);
    policyInfoLabel->setWordWrap(true);
    policyInfoLabel->setText(
        "스냅샷과 클립은 로컬 PC에 저장됩니다.\n"
        "Archive는 서버 보관 정책 대상입니다.\n"
        "보관 기간/용량/정리 기준은 현재 서버 정책을 따릅니다.\n"
        "경로 오류 또는 권한 부족 시 저장이 실패할 수 있습니다.\n"
        "설정 변경은 즉시 반영됩니다.");
    policyLayout->addWidget(policyTitle);
    policyLayout->addWidget(policyInfoLabel);
    policyLayout->addStretch();
    tabs->addTab(policyPage, "정책");

}

bool SettingsDialog::devicesChanged() const
{
    return m_devicesChanged;
}




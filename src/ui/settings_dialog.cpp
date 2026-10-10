#include "settings_dialog.h"

#include "common_ui.h"
#include "popup_manager.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

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

    // Phase 6: "장치 관리" 탭(수동 RTSP 장치, QSettings("TeamClue","VMS_v1") 기반)은 제거했다.
    // ONVIF discovery와 완전히 분리된 레거시 경로였고, 비활성화 상태로만 남겨뒀던 것도
    // (a) 탭은 잠가놔도 사이드바 채널 트리에는 여전히 그 QSettings 데이터가 "사용자 커스텀 장치"로
    // 주입되고 있었고 (b) rtspUrlForDisplayName()의 RTSP 폴백으로도 계속 읽히고 있어서 완전히
    // 죽은 코드가 아니었다 — ONVIF 선택 결과만 신뢰하도록 관련 코드를 전부 정리했다.

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


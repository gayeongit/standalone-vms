#include "playback_screen.h"

#include "app_state.h"
#include "common_ui.h"
#include "common_widgets.h"
#include "playback_screen_helpers.h"
#include "popup_manager.h"

#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSaveFile>
#include <QStyle>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QTimeZone>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

using namespace PlaybackScreenHelpers;

namespace {
void setExportButtonText(QPushButton *button, const QString &text)
{
    if (button) {
        button->setText(text);
    }
}

void clearExportButtonFeedback(QPushButton *button)
{
    if (!button) {
        return;
    }
    button->setProperty("feedbackState", QVariant());
    button->style()->unpolish(button);
    button->style()->polish(button);
    setExportButtonText(button, QStringLiteral("내보내기"));
}

void showExportButtonDone(QWidget *owner, QPushButton *button)
{
    if (!owner || !button) {
        return;
    }
    button->setProperty("feedbackState", QStringLiteral("done"));
    button->style()->unpolish(button);
    button->style()->polish(button);
    setExportButtonText(button, QStringLiteral("저장완료"));
    QTimer::singleShot(2000, owner, [button]() {
        clearExportButtonFeedback(button);
    });
}
} // namespace

bool PlaybackScreen::isExportBusy() const
{
    return m_exportDownloadReply != nullptr || !m_exportJobId.isEmpty() || m_exportStatusInFlight;
}

void PlaybackScreen::cancelExportOperations(bool notifyUser)
{
    const bool hadActiveWork = isExportBusy();
    const bool hadDownloadInProgress = (m_exportDownloadReply != nullptr);
    if (m_exportPollTimer) {
        m_exportPollTimer->stop();
    }
    ++m_exportGeneration;
    m_exportStatusInFlight = false;
    m_exportPollAttempts = 0;
    m_exportJobId.clear();

    if (m_exportDownloadReply) {
        m_exportDownloadAbortRequested = true;
        m_exportDownloadReply->abort();
    } else if (m_exportSaveFile) {
        m_exportSaveFile->cancelWriting();
        m_exportSaveFile->deleteLater();
        m_exportSaveFile = nullptr;
    }

    updateExportUiState();
    if (notifyUser && hadActiveWork) {
        showActionStatus(m_actionStatusLabel, "현재 내보내기 작업을 취소했습니다.", "info", 1800);
    } else if (!hadActiveWork || hadDownloadInProgress) {
        clearActionStatus(m_actionStatusLabel);
    }
}

void PlaybackScreen::updateExportUiState()
{
    if (!m_exportButton) {
        return;
    }
    const bool busy = isExportBusy();
    m_exportButton->setEnabled(!busy);
    if (!m_exportButton->property("feedbackState").isValid()) {
        setExportButtonText(m_exportButton, busy ? QStringLiteral("저장중") : QStringLiteral("내보내기"));
    }
    if (busy) {
        m_exportButton->setToolTip(QStringLiteral("현재 내보내기 작업이 진행 중입니다."));
    } else {
        m_exportButton->setToolTip(QString());
    }
}

void PlaybackScreen::openExportDialog()
{
    auto *sidebar = findChild<SidebarWidget *>();
    auto *actionStatusLabel = sidebar ? sidebar->actionStatusLabel() : m_actionStatusLabel;

    if (m_exportDownloadReply) {
        showActionStatus(actionStatusLabel, "현재 내보내기 다운로드가 진행 중입니다.", "info", 2200);
        return;
    }
    if (!m_exportJobId.isEmpty() || m_exportStatusInFlight) {
        showActionStatus(actionStatusLabel, "현재 내보내기 작업이 진행 중입니다.", "info", 2200);
        return;
    }

    if (m_currentPlaybackChannelId < 0 || m_selectedPlaybackDate.isEmpty()) {
        showActionStatus(actionStatusLabel, "먼저 재생할 항목을 선택해 주세요.", "error", 2200);
        return;
    }
    if (!m_playbackService) {
        PopupManager::showInfo(this, "내보내기", "PlaybackService가 연결되지 않았습니다.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("내보내기");
    dialog.setMinimumWidth(420);
    dialog.winId();
    applyNativeDarkTitleBar(&dialog);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    layout->addWidget(new QLabel(QString("파일명: %1").arg(displayNameFromSource(m_currentPlaybackSource)), &dialog));
    auto *startEdit = new QTimeEdit(QTime(0, 0, 0), &dialog);
    auto *endEdit = new QTimeEdit(QTime(0, 0, 10), &dialog);
    startEdit->setDisplayFormat("HH:mm:ss");
    endEdit->setDisplayFormat("HH:mm:ss");

    auto *row1 = new QHBoxLayout();
    row1->addWidget(new QLabel("시작", &dialog));
    row1->addWidget(startEdit, 1);
    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel("종료", &dialog));
    row2->addWidget(endEdit, 1);
    layout->addLayout(row1);
    layout->addLayout(row2);

    auto *buttons = new QHBoxLayout();
    auto *cancel = new QPushButton("취소", &dialog);
    auto *exportBtn = new QPushButton("내보내기", &dialog);
    exportBtn->setObjectName("primaryButton");
    buttons->addStretch();
    buttons->addWidget(exportBtn);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(exportBtn, &QPushButton::clicked, &dialog, [&dialog]() { dialog.accept(); });

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const int startSec = QTime(0, 0, 0).secsTo(startEdit->time());
    const int endSec = QTime(0, 0, 0).secsTo(endEdit->time());
    if (endSec <= startSec) {
        showActionStatus(actionStatusLabel, "종료 시간은 시작 시간보다 뒤여야 합니다.", "error", 2500);
        return;
    }

    const QString startTs = playbackTimestampForDateAndMs(m_selectedPlaybackDate, static_cast<qint64>(startSec) * 1000);
    const QString endTs = playbackTimestampForDateAndMs(m_selectedPlaybackDate, static_cast<qint64>(endSec) * 1000);
    if (startTs.isEmpty() || endTs.isEmpty()) {
        showActionStatus(actionStatusLabel, "내보내기 시간을 변환하지 못했습니다.", "error", 2500);
        return;
    }
    if (m_exportPollTimer) {
        m_exportPollTimer->stop();
    }
    m_exportJobId.clear();
    m_exportPollAttempts = 0;
    m_exportGeneration++;
    m_exportStatusInFlight = true;
    updateExportUiState();
    const int generation = m_exportGeneration;
    showActionStatus(actionStatusLabel, "내보내기 요청 중...", "progress");
    m_playbackService->requestExport(
        m_currentPlaybackChannelId,
        startTs,
        endTs,
        QStringLiteral("mp4"),
        this,
        [this, actionStatusLabel, generation](const PlaybackExportStartResult &result) {
            if (generation != m_exportGeneration) {
                return;
            }
            m_exportStatusInFlight = false;
            updateExportUiState();
            if (!result.ok || result.jobId.trimmed().isEmpty()) {
                showActionStatus(actionStatusLabel, "내보내기 요청 실패", "error", 2500);
                return;
            }
            m_exportJobId = result.jobId.trimmed();
            m_exportPollAttempts = 0;
            m_exportStatusInFlight = false;
            updateExportUiState();
            showActionStatus(actionStatusLabel, "내보내기 준비 중...", "progress");
            pollExportStatus();
            if (m_exportPollTimer && !m_exportPollTimer->isActive()) {
                m_exportPollTimer->start();
            }
        });
}

void PlaybackScreen::pollExportStatus()
{
    if (!m_playbackService || m_exportJobId.isEmpty()) {
        if (m_exportPollTimer) {
            m_exportPollTimer->stop();
        }
        m_exportStatusInFlight = false;
        updateExportUiState();
        return;
    }
    if (m_exportStatusInFlight) {
        return;
    }

    auto *sidebar = findChild<SidebarWidget *>();
    auto *actionStatusLabel = sidebar ? sidebar->actionStatusLabel() : nullptr;
    const QString jobId = m_exportJobId;
    const int generation = m_exportGeneration;
    ++m_exportPollAttempts;
    m_exportStatusInFlight = true;
    updateExportUiState();
    m_playbackService->fetchExportStatus(jobId, this, [this, actionStatusLabel, jobId, generation](const PlaybackExportStatusResult &result) {
        m_exportStatusInFlight = false;
        if (generation != m_exportGeneration || jobId != m_exportJobId) {
            return;
        }
        updateExportUiState();
        if (!result.ok) {
            if (m_exportPollTimer) {
                m_exportPollTimer->stop();
            }
            m_exportJobId.clear();
            updateExportUiState();
            showActionStatus(actionStatusLabel, "내보내기 조회 실패", "error", 2500);
            return;
        }

        const QString status = result.status.trimmed().toUpper();
        if (status == QStringLiteral("QUEUED") || status == QStringLiteral("PROCESSING")) {
            if (m_exportPollAttempts >= 60) {
                if (m_exportPollTimer) {
                    m_exportPollTimer->stop();
                }
                m_exportJobId.clear();
                updateExportUiState();
                showActionStatus(actionStatusLabel, "내보내기 시간 초과", "error", 2500);
                return;
            }
            showActionStatus(actionStatusLabel, "내보내기 준비 중...", "progress");
            return;
        }
        if (status == QStringLiteral("DONE")) {
            if (m_exportPollTimer) {
                m_exportPollTimer->stop();
            }
            const QString url = result.absoluteUri.trimmed().isEmpty() ? result.uri.trimmed() : result.absoluteUri.trimmed();
            m_exportJobId.clear();
            updateExportUiState();
            if (url.isEmpty()) {
                PopupManager::showInfo(this, "내보내기", "내보내기 URL이 비어 있습니다.");
                return;
            }
            showActionStatus(actionStatusLabel, "내보내기 다운로드 중...", "progress");
            startExportDownload(url, result.fileName);
            return;
        }

        if (status == QStringLiteral("FAILED")) {
            if (m_exportPollTimer) {
                m_exportPollTimer->stop();
            }
            m_exportJobId.clear();
            updateExportUiState();
            showActionStatus(actionStatusLabel, "내보내기 실패", "error", 2500);
            return;
        }

        if (m_exportPollTimer) {
            m_exportPollTimer->stop();
        }
        m_exportJobId.clear();
        updateExportUiState();
        showActionStatus(actionStatusLabel, "내보내기 상태 오류", "error", 2500);
    });
}

void PlaybackScreen::startExportDownload(const QString &url, const QString &fileName)
{
    auto *sidebar = findChild<SidebarWidget *>();
    auto *actionStatusLabel = sidebar ? sidebar->actionStatusLabel() : nullptr;

    if (!m_exportDownloadManager) {
        showActionStatus(actionStatusLabel, "다운로드 준비 실패", "error", 2500);
        return;
    }
    if (m_exportDownloadReply) {
        showActionStatus(actionStatusLabel, "이미 다운로드 중", "info", 2000);
        return;
    }

    QString resolvedDir;
    QString dirError;
    QString targetDir;
    if (isClipSaveDirectoryValid(&resolvedDir, &dirError)) {
        targetDir = resolvedDir;
    } else if (!dirError.trimmed().isEmpty()) {
        PopupManager::showInfo(this, "내보내기 저장 경로", dirError);
    }

    QString suggestedName = fileName.trimmed();
    if (suggestedName.isEmpty()) {
        const QUrl exportUrl(url);
        suggestedName = QFileInfo(exportUrl.path()).fileName();
        if (suggestedName.isEmpty()) {
            suggestedName = QStringLiteral("playback_export_%1.mp4")
                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        }
    }

    QString savePath;
    if (!targetDir.isEmpty()) {
        savePath = QDir(targetDir).filePath(suggestedName);
    } else {
        savePath = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("내보내기 저장"),
            QDir::home().filePath(suggestedName),
            QStringLiteral("MP4 Files (*.mp4);;All Files (*.*)"));
        if (savePath.isEmpty()) {
            clearActionStatus(actionStatusLabel);
            return;
        }
    }

    const QUrl downloadUrl(url.trimmed());
    const QString scheme = downloadUrl.scheme().trimmed().toLower();
    if (!downloadUrl.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
        PopupManager::showInfo(this, "내보내기", "유효하지 않은 다운로드 URL입니다.");
        return;
    }

    m_exportSaveFile = new QSaveFile(savePath, this);
    if (!m_exportSaveFile->open(QIODevice::WriteOnly)) {
        m_exportSaveFile->cancelWriting();
        m_exportSaveFile->deleteLater();
        m_exportSaveFile = nullptr;
        PopupManager::showInfo(this, "내보내기", QString("파일 저장을 시작할 수 없습니다: %1").arg(savePath));
        return;
    }

    QNetworkRequest request{downloadUrl};
    request.setRawHeader("Accept", "*/*");
    const QString accessToken = AppState::instance().accessToken.trimmed();
    if (!accessToken.isEmpty()) {
        request.setRawHeader("Authorization", QByteArray("Bearer ").append(accessToken.toUtf8()));
    }
    m_exportDownloadAbortRequested = false;
    m_exportDownloadReply = m_exportDownloadManager->get(request);
    updateExportUiState();

    connect(m_exportDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_exportDownloadReply) {
            if (m_exportSaveFile && m_exportSaveFile->write(m_exportDownloadReply->readAll()) < 0) {
                m_exportDownloadReply->abort();
            }
        }
    });

    connect(m_exportDownloadReply, &QNetworkReply::finished, this, [this, savePath, actionStatusLabel]() {
        const bool ok = m_exportDownloadReply
            && m_exportDownloadReply->error() == QNetworkReply::NoError;
        bool writeOk = true;
        if (m_exportDownloadReply) {
            writeOk = (!m_exportSaveFile || m_exportSaveFile->write(m_exportDownloadReply->readAll()) >= 0);
        }

        if (m_exportDownloadAbortRequested) {
            if (m_exportSaveFile) {
                m_exportSaveFile->cancelWriting();
            }
            clearActionStatus(actionStatusLabel);
        } else if (!ok || !writeOk) {
            if (m_exportSaveFile) {
                m_exportSaveFile->cancelWriting();
            }
            showActionStatus(actionStatusLabel, "내보내기 다운로드 실패", "error", 2500);
        } else {
            if (!m_exportSaveFile || !m_exportSaveFile->commit()) {
                if (m_exportSaveFile) {
                    m_exportSaveFile->cancelWriting();
                }
                PopupManager::showInfo(this, "내보내기", QString("파일 저장을 완료하지 못했습니다: %1").arg(savePath));
            } else {
                showExportButtonDone(this, m_exportButton);
                showActionStatus(actionStatusLabel, "내보내기 완료", "success", 2500);
            }
        }

        if (m_exportDownloadReply) {
            m_exportDownloadReply->deleteLater();
            m_exportDownloadReply = nullptr;
        }
        if (m_exportSaveFile) {
            m_exportSaveFile->deleteLater();
            m_exportSaveFile = nullptr;
        }
        m_exportDownloadAbortRequested = false;
        updateExportUiState();
    });
}

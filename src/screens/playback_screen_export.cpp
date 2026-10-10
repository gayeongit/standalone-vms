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

// Phase 6: 서버가 준 파일명에서 디렉터리 성분(경로 traversal 포함)을 제거하고 순수 파일명만 남긴다.
// QFileInfo::fileName()은 "/"/"\\" 앞부분과 ".."로 시작하는 경로 조작을 자연스럽게 걸러낸다.
QString sanitizeExportFileName(const QString &rawFileName)
{
    const QString name = QFileInfo(rawFileName.trimmed()).fileName();
    if (name.isEmpty() || name == QStringLiteral(".") || name == QStringLiteral("..")) {
        return QString();
    }
    return name;
}

// Phase 6: 두 URL이 scheme+host+port까지 같은 origin인지 비교한다 — Bearer 토큰을 외부 URL에
// 실수로 흘려보내지 않기 위한 판단 기준. apiBaseUrl이 비어 있으면(서버 미설정) 항상 false.
bool isSameOrigin(const QUrl &a, const QUrl &b)
{
    if (!a.isValid() || !b.isValid()) {
        return false;
    }
    const auto effectivePort = [](const QUrl &url) {
        const int port = url.port(-1);
        if (port >= 0) {
            return port;
        }
        return url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ? 443 : 80;
    };
    return a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0
        && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
        && effectivePort(a) == effectivePort(b);
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

    // Phase 6: 서버가 준 파일명을 그대로 저장 경로에 붙이면 "../../evil.exe" 같은 경로 조작이
    // 가능했다 — sanitizeExportFileName()으로 디렉터리 성분을 제거한 순수 파일명만 쓴다.
    QString suggestedName = sanitizeExportFileName(fileName);
    if (suggestedName.isEmpty()) {
        const QUrl exportUrl(url);
        suggestedName = sanitizeExportFileName(QFileInfo(exportUrl.path()).fileName());
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
    // Phase 6: 다운로드 URL이 API 서버와 같은 origin일 때만 Bearer 토큰을 붙인다 — 서버가 외부
    // 스토리지(서명된 URL 등)를 가리키는 URL을 내려줘도 우리 세션 토큰이 거기로 새지 않게 막는다.
    const QUrl apiBaseUrl(AppState::instance().apiBaseUrl.trimmed());
    if (isSameOrigin(downloadUrl, apiBaseUrl)) {
        const QString accessToken = AppState::instance().accessToken.trimmed();
        if (!accessToken.isEmpty()) {
            request.setRawHeader("Authorization", QByteArray("Bearer ").append(accessToken.toUtf8()));
        }
    }
    // 리다이렉트를 자동으로 안 따라간다 — 자동으로 따라가면 위의 origin 판단이 리다이렉트
    // 이후에는 재검증되지 않아 다른 origin으로 토큰이 넘어갈 수 있다. 리다이렉트 응답은
    // finished 핸들러의 HTTP status 체크에서 실패로 처리된다.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
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
        // HTTP status도 같이 봐야 한다 — ManualRedirectsPolicy 때문에 3xx 응답은 네트워크 계층
        // error()가 NoError로 남을 수 있고, 그걸 그냥 두면 리다이렉트 응답 본문을 파일로 저장해버린다.
        const int httpStatus = m_exportDownloadReply
            ? m_exportDownloadReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
            : 0;
        const bool ok = m_exportDownloadReply
            && m_exportDownloadReply->error() == QNetworkReply::NoError
            && httpStatus >= 200 && httpStatus < 300;
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

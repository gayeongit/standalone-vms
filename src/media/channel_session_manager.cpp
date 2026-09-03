#include "channel_session_manager.h"

#include "common_ui.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QWidget>

namespace {

QString urlForChannel(const QString &channel)
{
    return rtspUrlForDisplayName(channel);
}

QString codecForChannel(const QString &channel)
{
    return videoCodecForDisplayName(channel);
}

} // namespace

ChannelSessionManager &ChannelSessionManager::instance()
{
    static ChannelSessionManager manager;
    return manager;
}

ChannelSessionManager::ChannelSessionManager(QObject *parent)
    : QObject(parent)
{}

void ChannelSessionManager::pruneHosts(Session &session)
{
    QVector<QPointer<QWidget>> filtered;
    filtered.reserve(session.hosts.size());
    QHash<quintptr, StreamQualityProfile> filteredProfiles;
    for (const auto &host : session.hosts) {
        if (!host.isNull()) {
            filtered.push_back(host);
            const quintptr key = reinterpret_cast<quintptr>(host.data());
            filteredProfiles.insert(key, session.hostProfiles.value(key, StreamQualityProfile::Normal));
        }
    }
    session.hosts = filtered;
    session.hostProfiles = filteredProfiles;
}

ChannelSessionManager::Session &ChannelSessionManager::ensureSession(const QString &channel, const QString &url)
{
    Session &session = m_sessions[channel];
    if (!session.player) {
        session.player = new StreamPlayer(this);
    }
    session.player->setVideoCodecHint(codecForChannel(channel));
    if (session.sourceUrl != url) {
        session.sourceUrl = url;
        session.player->setSource(session.sourceUrl);
    }
    if (session.player->status() == StreamStatus::Idle || session.player->status() == StreamStatus::Error) {
        session.player->start();
    }
    return session;
}

void ChannelSessionManager::refreshEffectiveProfile(Session &session)
{
    if (!session.player) {
        return;
    }
    StreamQualityProfile effective = StreamQualityProfile::DenseGrid;
    bool hasAny = false;
    for (auto it = session.hostProfiles.constBegin(); it != session.hostProfiles.constEnd(); ++it) {
        hasAny = true;
        if (static_cast<int>(it.value()) < static_cast<int>(effective)) {
            effective = it.value();
        }
    }
    if (!hasAny) {
        effective = StreamQualityProfile::Normal;
    }
    session.player->setQualityProfile(effective);
}

void ChannelSessionManager::bindChannelToWidget(const QString &channel, QWidget *widget, StreamQualityProfile profile)
{
    // 하나의 채널을 여러 host widget이 공유할 수 있다는 것이 이 클래스의 핵심이다.
    // 주어진 channel에 대한 session/player를 확보하고, 현재 widget을 해당 session의 render target으로 붙인다.
    // bind는 "새 스트림 생성"이 아니라:
    // - channel 별 session 확보
    // - host 목록 갱신
    // - profile 협상
    // - render widget binding
    // 을 수행해 같은 player를 여러 화면 컨텍스트에서 재사용하게 만든다.
    if (channel.isEmpty() || !widget) {
        return;
    }
    const QString url = urlForChannel(channel);
    if (url.isEmpty()) {
        return;
    }

    Session &session = ensureSession(channel, url);
    pruneHosts(session);
    if (m_hasAppliedActiveChannels && session.player) {
        const bool active = m_lastActiveChannels.contains(channel);
        const bool desiredPaused = !active;
        if (session.player->isPaused() != desiredPaused) {
            session.player->setPaused(desiredPaused);
        }
    }

    for (const auto &host : session.hosts) {
        if (host != widget) {
            continue;
        }
        const quintptr key = reinterpret_cast<quintptr>(widget);
        const StreamQualityProfile previousProfile = session.hostProfiles.value(key, StreamQualityProfile::Normal);
        if (previousProfile != profile) {
            session.hostProfiles.insert(key, profile);
            refreshEffectiveProfile(session);
        }
        return;
    }
    QVector<QPointer<QWidget>> updated;
    updated.reserve(session.hosts.size() + 1);
    for (const auto &host : session.hosts) {
        if (host != widget) {
            updated.push_back(host);
        }
    }
    updated.push_back(widget);
    session.hosts = updated;
    session.hostProfiles.insert(reinterpret_cast<quintptr>(widget), profile);
    refreshEffectiveProfile(session);

    session.player->bindRenderWidget(widget);
}

void ChannelSessionManager::unbindChannelFromWidget(const QString &channel, QWidget *widget)
{
    if (channel.isEmpty() || !widget) {
        return;
    }
    auto it = m_sessions.find(channel);
    if (it == m_sessions.end()) {
        return;
    }

    Session &session = it.value();
    pruneHosts(session);
    QVector<QPointer<QWidget>> updated;
    updated.reserve(session.hosts.size());
    for (const auto &host : session.hosts) {
        if (host != widget) {
            updated.push_back(host);
        }
    }
    session.hosts = updated;
    session.hostProfiles.remove(reinterpret_cast<quintptr>(widget));

    if (!session.player) {
        return;
    }
    session.player->unbindRenderWidget(widget);
    if (session.hosts.isEmpty()) {
        // No target widget left: stop pipeline to avoid unnecessary decode work.
        session.player->stop();
        session.player->clearRenderWidgets();
        return;
    }
    refreshEffectiveProfile(session);
}

StreamStatus ChannelSessionManager::statusForChannel(const QString &channel) const
{
    const auto it = m_sessions.constFind(channel);
    if (it == m_sessions.constEnd() || !it.value().player) {
        return StreamStatus::Idle;
    }
    return it.value().player->status();
}

QSharedPointer<const QImage> ChannelSessionManager::latestFrameForChannelShared(const QString &channel) const
{
    const auto it = m_sessions.constFind(channel);
    if (it == m_sessions.constEnd() || !it.value().player) {
        return {};
    }
    return it.value().player->latestFrameShared();
}

QImage ChannelSessionManager::latestFrameForChannelCopy(const QString &channel) const
{
    const auto frame = latestFrameForChannelShared(channel);
    if (frame.isNull()) {
        return {};
    }
    return *frame;
}

void ChannelSessionManager::applyActiveChannels(const QSet<QString> &activeChannels)
{
    // showScreen() 이후 어떤 채널만 계속 디코딩할지 결정하는 함수다.
    // active set과 이전 set을 비교해 필요한 player만 pause/unpause 하고 적용 시간을 기록한다.
    // 여기서는 세션을 만들거나 지우지 않고, 기존 player를 pause/unpause 하는 쪽으로 다뤄
    // 화면 전환마다 pipeline을 매번 새로 올리는 비용을 줄인다.
    QElapsedTimer applyTimer;
    applyTimer.start();

    const auto applyPauseIfNeeded = [](Session &session, bool active) -> bool {
        if (!session.player) {
            return false;
        }
        const bool desiredPaused = !active;
        if (session.player->isPaused() == desiredPaused) {
            return false;
        }
        session.player->setPaused(desiredPaused);
        return true;
    };

    int changedCount = 0;
    if (!m_hasAppliedActiveChannels) {
        for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
            Session &session = it.value();
            const bool active = activeChannels.contains(it.key());
            if (applyPauseIfNeeded(session, active)) {
                ++changedCount;
            }
        }
        m_hasAppliedActiveChannels = true;
    } else {
        QSet<QString> changedChannels = m_lastActiveChannels - activeChannels;
        changedChannels.unite(activeChannels - m_lastActiveChannels);

        for (const QString &channel : changedChannels) {
            auto it = m_sessions.find(channel);
            if (it == m_sessions.end()) {
                continue;
            }
            const bool active = activeChannels.contains(channel);
            if (applyPauseIfNeeded(it.value(), active)) {
                ++changedCount;
            }
        }
    }

    m_lastActiveChannels = activeChannels;
    qInfo().noquote()
        << QString("perf metric=active_apply_ms context=changed:%1 value=%2 unit=ms")
              .arg(changedCount)
              .arg(applyTimer.elapsed());
}

void ChannelSessionManager::shutdown()
{
    // 로그아웃/종료 시에는 pause 수준이 아니라 세션 전체를 내려야 한다.
    // 모든 player를 stop하고 render target/source 캐시를 정리한 뒤 session map을 비운다.
    // 모든 player를 stop/clear/deleteLater 하고 host/sourceUrl 캐시도 비워
    // 다음 인증 세션이 완전히 새 상태에서 시작되게 만든다.
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it) {
        Session &session = it.value();
        if (session.player) {
            session.player->stop();
            session.player->clearRenderWidgets();
            session.player->deleteLater();
            session.player = nullptr;
        }
        session.hosts.clear();
        session.hostProfiles.clear();
        session.sourceUrl.clear();
    }
    m_sessions.clear();
    m_lastActiveChannels.clear();
    m_hasAppliedActiveChannels = false;
}

#include "credential_store.h"

#include <keychain.h>

#include <QEventLoop>

namespace {

const QString kService = QStringLiteral("standalone-vms-camera");

QString keyFor(const QString &deviceIp, const QString &field)
{
    return deviceIp + QStringLiteral("/") + field;
}

bool readOne(const QString &key, QString *out)
{
    QKeychain::ReadPasswordJob job(kService);
    job.setKey(key);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    if (job.error() != QKeychain::NoError) {
        return false;
    }
    *out = job.textData();
    return true;
}

void writeOne(const QString &key, const QString &value)
{
    QKeychain::WritePasswordJob job(kService);
    job.setKey(key);
    job.setTextData(value);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
}

} // namespace

namespace CredentialStore {

bool load(const QString &deviceIp, QString *username, QString *password)
{
    const QString trimmedIp = deviceIp.trimmed();
    if (trimmedIp.isEmpty() || !username || !password) {
        return false;
    }
    QString loadedUsername;
    QString loadedPassword;
    if (!readOne(keyFor(trimmedIp, QStringLiteral("username")), &loadedUsername)) {
        return false;
    }
    if (!readOne(keyFor(trimmedIp, QStringLiteral("password")), &loadedPassword)) {
        return false;
    }
    *username = loadedUsername;
    *password = loadedPassword;
    return true;
}

void save(const QString &deviceIp, const QString &username, const QString &password)
{
    const QString trimmedIp = deviceIp.trimmed();
    if (trimmedIp.isEmpty()) {
        return;
    }
    writeOne(keyFor(trimmedIp, QStringLiteral("username")), username);
    writeOne(keyFor(trimmedIp, QStringLiteral("password")), password);
}

} // namespace CredentialStore

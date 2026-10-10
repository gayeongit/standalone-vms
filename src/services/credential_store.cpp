#include "credential_store.h"

#include <keychain.h>

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString kService = QStringLiteral("standalone-vms-camera");

QString keyFor(const QString &deviceIp)
{
    return deviceIp + QStringLiteral("/credential");
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

bool writeOne(const QString &key, const QString &value)
{
    QKeychain::WritePasswordJob job(kService);
    job.setKey(key);
    job.setTextData(value);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    return job.error() == QKeychain::NoError;
}

} // namespace

namespace CredentialStore {

bool load(const QString &deviceIp, QString *username, QString *password)
{
    const QString trimmedIp = deviceIp.trimmed();
    if (trimmedIp.isEmpty() || !username || !password) {
        return false;
    }
    QString serialized;
    if (!readOne(keyFor(trimmedIp), &serialized)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(serialized.toUtf8());
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject record = doc.object();
    const QJsonValue usernameValue = record.value(QStringLiteral("username"));
    const QJsonValue passwordValue = record.value(QStringLiteral("password"));
    // 필드가 존재하는지가 아니라 실제로 문자열이고 비어있지 않은지까지 확인한다 —
    // {"username":1,"password":false} 같은 손상된 레코드를 "성공"으로 취급하지 않기 위해.
    if (!usernameValue.isString() || !passwordValue.isString()) {
        return false;
    }
    const QString loadedUsername = usernameValue.toString();
    const QString loadedPassword = passwordValue.toString();
    if (loadedUsername.isEmpty() || loadedPassword.isEmpty()) {
        return false;
    }
    *username = loadedUsername;
    *password = loadedPassword;
    return true;
}

bool save(const QString &deviceIp, const QString &username, const QString &password)
{
    const QString trimmedIp = deviceIp.trimmed();
    if (trimmedIp.isEmpty()) {
        return false;
    }
    QJsonObject record;
    record.insert(QStringLiteral("username"), username);
    record.insert(QStringLiteral("password"), password);
    const QString serialized = QString::fromUtf8(QJsonDocument(record).toJson(QJsonDocument::Compact));
    return writeOne(keyFor(trimmedIp), serialized);
}

} // namespace CredentialStore

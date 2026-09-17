#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <QString>

// Phase 3b: 로그인 사용자용 영구 자격증명 저장(QtKeychain). deviceIp를 키로 쓴다 —
// AppState의 세션 캐시(deviceCredentialUsernameByIp 등)와 동일한 키 스킴.
// Keychain 접근은 로컬이라 빠르므로 QEventLoop로 감싼 동기 API로 노출한다.
namespace CredentialStore {

bool load(const QString &deviceIp, QString *username, QString *password);
// 반환값은 두 키(username/password) 모두 쓰기 성공했는지를 의미한다 — 하나만 실패해도 false.
bool save(const QString &deviceIp, const QString &username, const QString &password);

} // namespace CredentialStore

#endif // CREDENTIAL_STORE_H

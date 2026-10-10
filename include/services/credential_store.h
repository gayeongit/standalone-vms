#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <QString>

// Phase 3b: 로그인 사용자용 영구 자격증명 저장(QtKeychain). deviceIp를 키로 쓴다 —
// AppState의 세션 캐시(deviceCredentialUsernameByIp 등)와 동일한 키 스킴.
// Keychain 접근은 로컬이라 빠르므로 QEventLoop로 감싼 동기 API로 노출한다.
namespace CredentialStore {

bool load(const QString &deviceIp, QString *username, QString *password);
// username/password를 하나의 직렬화된 레코드로 단일 키에 저장한다(Phase 6 — 예전엔 두 키로 나눠
// 저장해서 하나만 쓰기 실패하면 아이디/비번이 서로 다른 시점 값으로 남는 문제가 있었음).
bool save(const QString &deviceIp, const QString &username, const QString &password);

} // namespace CredentialStore

#endif // CREDENTIAL_STORE_H

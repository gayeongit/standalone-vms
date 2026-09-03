# standalone-vms 개발 실행 계획

- 기준 문서:
  - `docs/roadmap.md`
  - `docs/core_classes.md`
- 문서 성격: 이 파일 하나에 Phase별 실행계획을 이어서 누적 작성한다. Phase마다 별도 파일을 만들지 않는다 (`VMS_v2_dev_execution_plan.md` 스타일 참고).

## 문서 운용 기준

- 메인 문서(실행 기준): `docs/dev_execution_plan.md` (이 파일)
- 참고 문서(설계/구조 확인): `docs/roadmap.md`, `docs/core_classes.md`

> 개발 진행 중 의사결정은 이 문서에 먼저 반영하고, 필요 시 `docs/roadmap.md`를 동기화한다.

---

## 0. 실행 원칙

1. Phase 게이트 방식으로 진행한다.
2. 각 Phase 종료 시 최소 스모크 테스트를 수행한다.
3. 실패 시 다음 Phase로 넘어가지 않고 해당 Phase에서 수정 후 재검증한다.
4. 각 Phase 종료 후 `docs/core_classes.md`에서 실제로 바뀐 클래스 섹션만 갱신한다 (전체 재작성 금지, 안 바뀐 섹션은 그대로 둠).
5. UGV 등 스코프 밖 코드는 삭제하지 않고 비활성화/스텁으로 대응한다. 실제 삭제 여부 판단은 Phase 6(통합 검증)에서.

---

## Phase 3a. 로그인/게스트 분기 (카메라 무관)

RPi를 아직 쓸 수 없어서 원래 순서(Phase 0부터)를 못 밟는 상황이라, 카메라 의존이 없는 이 작업을 먼저 진행한다. 배경은 `docs/roadmap.md`의 "3.5 진행 순서 변경" 참고.

### 대상 (코드 조사로 확정)

- 핵심 수정: `mainwindow_auth.cpp`, `login_screen.h/.cpp`
- `app_state.h/.cpp`: 수정 없음 — `isGuestMode` 같은 신규 필드 불필요. 게스트 진입은 `AuthService::login(...)`을 안 부르는 것뿐이라 기존 `isAuthenticated=false`가 게스트 상태를 그대로 표현함 (설계 원칙 3과 일치). `isAuthenticated`를 참조하는 곳은 `mainwindow_auth.cpp`의 WS 이벤트 처리 2곳(`WsClient::connected`, `jsonMessageReceived`)뿐이고 둘 다 guard라 게스트 상태에서 안전하게 스킵됨.
- `mainwindow_runtime.cpp`, `mainwindow_navigation.cpp`, `app_state.h`: 수정 없음 — 확인 결과 `createRuntimeScreens(...)`, `showScreen(Main)`, `activeChannelsForScreen(Main)`, `ChannelSessionManager::applyActiveChannels(...)` 모두 빈 `gridCells`/`selectedChannelContexts`에 안전. `setupUi()`에서 로그인 전에도 `createRuntimeScreens`가 이미 한 번 호출되므로(런타임 화면이 로그인 전부터 미리 생성됨) 이 경로는 사실상 이미 검증되어 있음.
- 삭제/비활성화 후보: 없음. UGV 등 스코프 밖 항목은 이번 Phase에서 건드리지 않는다.

### 작업 (실제 막는 지점 3곳 확인됨)

채널 0개로 Main 진입을 막는 지점이 코드상 3곳 있음:

1. `login_screen.cpp` `DeviceCheckScreen`의 "VMS 시작" 클릭 핸들러 — `selected.isEmpty()`면 팝업 띄우고 `startRequested` 자체를 emit 안 함
2. `mainwindow_auth.cpp`의 `startRequested` 핸들러 — `normalized.isEmpty()`면 팝업 띄우고 즉시 return (여기서 막히면 `createRuntimeScreens`/`showScreen(Main)`이 호출조차 안 됨)
3. `mainwindow_auth.cpp`의 `finalize()` 내부 — `resolvedContexts.isEmpty()`면 "채널 RTSP 조회에 실패했습니다" 팝업 후 return. 원래는 "선택은 했는데 RTSP 조회가 다 실패한 경우"용 가드인데, "애초에 0개 선택"도 같은 경로를 타서 걸림

구체 작업:

- `LoginScreen`에 "게스트로 시작" 버튼 + `guestRequested()` 시그널 추가 (회원가입 버튼 아래 보조 버튼)
- `mainwindow_auth.cpp`에 `guestRequested` 연결 추가 — `AuthService::login(...)` 호출 없이 곧장 `showScreen(DeviceCheck)` + 300ms 딜레이 `refreshDevices()` (기존 로그인 성공 경로와 동일 패턴, 토큰/인증 상태는 안 건드림)
- 위 1번 가드 제거 — 0개여도 `startRequested` emit
- 위 2번 가드(조기 return) 제거 — 제거해도 `pending<=0`이라 `finalize()`가 바로 호출됨
- 위 3번 `finalize()`의 `resolvedContexts.isEmpty()` 분기를 분리: `normalized`가 애초에 비어있었으면(0개 선택) 에러 팝업 없이 정상 진행, `normalized`는 있었는데 RTSP 조회가 다 실패했으면 기존처럼 에러 팝업 + return
- 로그인 경로(`loginRequested` 핸들러, 서버 켜진 상태)는 무수정. 위 가드 완화는 게스트 전용 분기가 아니라 공통 로직이지만, 실제 채널이 있는 로그인 사용자는 `normalized`/`resolvedContexts`가 비지 않으므로 동작 변화 없음
- `mainwindow_auth.cpp`의 `configureLoginScreenState()` 기본 오류 문구에 깨진 한글이 남아 있는 것으로 파악됨(`docs/core_classes.md` 5.2절 참고) — 이번 Phase 필수 작업 아님, 손대지 않음

### 완료 기준

- [x] 게스트로 로그인 없이, 카메라 0개 상태에서 Main 화면까지 진입 성공
- [x] 로그인 경로는 기존과 동일하게 동작 (회귀 없음)
- [x] 게스트 상태에서 Main 진입 후 크래시/에러 팝업 없음

### 게이트 테스트

- [x] 앱 실행 → 게스트 진입 → DeviceCheck(0채널) → "VMS 시작" → Main까지 크래시 없이 도달 (사용자 수동 확인, 2026-09-03)
- [x] 기존 로그인 경로 스모크 테스트 재통과 (사용자 수동 확인, 2026-09-03)
- [x] 의도하지 않은 에러 팝업 없음 (공통 게이트)

**상태: Phase 3a 완료 (2026-09-03).** 검증은 실제 앱 실행을 통한 사용자 수동 확인으로 진행함 (이 환경에서 GUI 자동화가 불안정해 스크린샷/클릭 자동 검증은 포기).

---

## 다음 Phase 메모

- Phase 3b(로컬 자격증명 캐시), Phase 0/1(RPi 테스트베드/카메라 도메인 입구)은 각각 진입 시점에 이 문서에 섹션을 이어서 추가한다.
- RPi 확보되면 Phase 0으로 복귀.

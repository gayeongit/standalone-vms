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

### 대상

- 핵심 수정: `mainwindow_auth.cpp`, `login_screen.h/.cpp` (게스트 진입 시그널/버튼)
- 연쇄 수정:
  - `app_state.h/.cpp` — 게스트 상태를 구분할 필드가 필요한지 확인 (예: `isGuestMode` 또는 `isAuthenticated=false`로도 충분한지)
  - `mainwindow_runtime.cpp` — `createRuntimeScreens(...)`가 빈 `selectedChannelContexts`/`gridCells`로도 정상 생성되는지
  - `screens.h` (`DeviceCheckScreen`) — 채널 0개 상태에서 "VMS 시작" 버튼이 막혀 있는지 확인
- 삭제/비활성화 후보: 없음. UGV 등 스코프 밖 항목은 이번 Phase에서 건드리지 않는다.

### 작업

- 로그인 화면에 "게스트로 시작" 진입 경로 추가 — `AuthService::login(...)` 호출 없이 `DeviceCheck` 화면으로 전환
- `DeviceCheckScreen`에서 선택 채널이 0개여도 "VMS 시작" 진행이 가능한지 확인, 막혀 있으면 최소한으로 완화
- `AppState.selectedChannelContexts` / `gridCells`가 빈 상태로 `createRuntimeScreens(...)` → `showScreen(Main)`까지 정상 도달하는지 검증
- 로그인 경로(서버 켜진 상태)는 기존 로직 그대로 유지 — 게스트 분기가 기존 인증 흐름을 침범하지 않도록 분리
- `mainwindow_auth.cpp`의 `configureLoginScreenState()` 기본 오류 문구에 깨진 한글이 남아 있는 것으로 파악됨(`docs/core_classes.md` 5.2절 참고) — 이번 Phase에서 게스트 진입 UX 다듬는 김에 같이 정리할지 여부만 결정, 필수 작업은 아님

### 완료 기준

- 게스트로 로그인 없이, 카메라 0개 상태에서 Main 화면까지 진입 성공
- 로그인 경로는 기존과 동일하게 동작 (회귀 없음)
- 게스트 상태에서 Main 진입 후 크래시/에러 팝업 없음

### 게이트 테스트

- 앱 실행 → 게스트 진입 → DeviceCheck(0채널) → "VMS 시작" → Main까지 크래시 없이 도달
- 기존 로그인 경로 스모크 테스트 재통과 (로그인 → DeviceCheck → Main)
- 의도하지 않은 에러 팝업 없음 (공통 게이트)

---

## 다음 Phase 메모

- Phase 3b(로컬 자격증명 캐시), Phase 0/1(RPi 테스트베드/카메라 도메인 입구)은 각각 진입 시점에 이 문서에 섹션을 이어서 추가한다.
- RPi 확보되면 Phase 0으로 복귀.

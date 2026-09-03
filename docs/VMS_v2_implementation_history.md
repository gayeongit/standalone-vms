# VMS v2 Phase 8.5~10-P1-9 구현 이력

## 문서 목적

이 문서는 [`VMS_v2_post_phase8_execution_plan.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_post_phase8_execution_plan.md) 의 `Phase 8.5~10-P1-9` 계획 항목을 실제 구현 기준으로 정리한 구현 이력 문서다.

계획 문서는 "무엇을 할 것인가"를 다루고, 이 문서는 아래를 남기는 것을 목적으로 한다.

- 실제로 어떤 코드/계측을 넣었는가
- 구현 중 무엇이 병목이었고, 어떻게 원인을 분리했는가
- 어떤 실험을 채택/롤백했는가
- 무엇을 Phase 10 이후로 이월했는가

즉, 이 문서는 `Phase 8.5~10-P1-9`의 성능 안정화/운영 정합성/구조 정리 로그이자, 이후 회귀 방지용 참조 문서다.

---

## 전체 요약

Phase 8.5~10-P1-9에서 실제로 정리된 핵심은 아래와 같다.

1. `clip_total_ms`, `render_fps`, `screen_transition_ms` 최소 baseline 계측을 도입했다.
2. 클립 저장 경로를 `PNG sequence` 중심에서 `ffmpeg stdin pipe(rawvideo)` 기본 경로로 전환했다.
3. `EncodeResult` 체계는 유지하면서 `pathTag(stdin/png_fallback)` 기반으로 경로 추적이 가능해졌다.
4. `StreamPlayer::setPaused()`의 UI thread blocking (`gst_element_get_state(..., 200ms)`)을 제거했다.
5. `ChannelSessionManager::applyActiveChannels()`를 diff 기반으로 바꿔 불필요한 pause/resume 호출을 줄였다.
6. 로그아웃/401 경로에서 스트림 세션 hard cleanup을 공통화했다.
7. 멀티뷰 품질 프로파일을 `Normal / QuadGrid / DenseGrid`로 재정의하고, `Normal` 경로를 단순화했다.
8. hidden 상태의 불필요한 draw/update를 억제하고, visible 기준 render dispatch로 CPU 낭비를 줄였다.
9. `first_frame_after_transition_ms`, `first_live_frame_after_transition_ms` 계측을 추가해 "UI 전환 시간"과 "실제 라이브 첫 프레임 도착 시간"을 분리했다.
10. `Phase 9`는 체크포인트 기준 종료했고, `Main -> CCTV first_live` 잔여 이슈는 Known residual로 이월했다.
11. `10-P0`에서 UGV WS 운영 계약을 `error=즉시 실패`, `*.ack=실제 처리 완료` 기준으로 코드/문서 일치 상태로 맞췄다.
12. `screens.h`, `MainWindow`, `common_ui`를 단계적으로 분리해 P1 구조 정리를 안전하게 시작했다.
13. CCTV 타깃 해석 로직을 공통 helper로 통합해 계측과 실제 bind 경로가 같은 규칙을 쓰도록 고정했다.
14. `common_ui`는 `channel_context_dnd`, `feedback_ui`, `capture_storage` helper로 쪼개고 umbrella 유지 전략으로 호출부 churn을 최소화했다.
15. UGV 화면에는 read-only 상태 패널과 방향키 주행 1차를 추가했고, 동시에 트리/DnD/직접 진입 정책을 `이벤트 상세 출동 시작` 중심으로 정리했다.
16. `10-P1-5`는 코드 기준 완료이고, 서버 미기동 상태라 UGV 출동 성공-path E2E만 blocked로 남겨뒀다.
17. `10-P1-6`에서 UGV 세션 상태 전이와 명령 실패를 분리하고, 서버 `error` 스키마를 공통 helper로 정리했다.
18. `10-P1-7`에서 `PlaybackScreen`을 메인 TU / 타임라인 / export 구현으로 분리하고, 공용 helper를 별도 헤더로 정리했다.
19. `10-P1-8`에서 `AppState` 멀티뷰 셀 상태를 `gridCells` 단일 구조로 통합해 병렬 배열을 제거했다.
20. `10-P1-9`에서 프로젝트/타이틀/산출물 표기를 `VMS_v2` 기준으로 정리하되, `QSettings` 및 프로토콜 키는 정책 변경을 피하기 위해 유지했다.

---

## 검증 범위

이 문서의 구현/개선 내용은 아래 검증 기준을 전제로 한다.

- 빌드/런타임 검증은 로컬 수동 테스트 기준
- 인앱 계측(`qInfo`) + PerfMon + 수동 체감 확인을 병행
- 클립 저장 성능의 공식 Before/After 비교값은 수동 측정 기준으로 통일
- `clip_total_ms`는 경로/프레임/코드 확인용 보조 지표로 사용
- 자동 테스트(단위/통합/E2E) 기반 정량 검증은 포함하지 않음

즉, 이 문서는 "코드 변경 이력 + 수동 체크포인트 검증" 중심 기록이다.

---

## Phase 8.5. 성능 baseline 계측

### 8.5-1. 목표

Phase 9 최적화 전에 최소 baseline 지표를 넣고, 이후 `Before/After` 비교가 가능하도록 계측 기반을 만드는 것이 목표였다.

핵심 지표:

- `clip_total_ms`
- `render_fps`
- `screen_transition_ms`

---

### 8.5-2. 구현 내용

#### 8.5-2-1. `clip_total_ms` 계측 추가

`ClipCaptureManager::encodeSnapshot()` 경로에 공통 계측을 넣어, 성공/실패/취소 경로에서 총 처리 시간을 로그로 남기도록 정리했다.

로그 필드:

- `path` (`stdin`, `png_fallback`, `n_a`)
- `frames`
- `ok`
- `code`

적용 파일:

- 수정: [`clip_capture_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)
- 수정: [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)

#### 8.5-2-2. `render_fps` 계측 추가

`VideoRenderWidget`의 실제 렌더 루프(`paintGL`) 기준으로 1초 단위 FPS 로그를 남기도록 정리했다.

적용 파일:

- 수정: [`video_render_widget.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)
- 수정: [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)

#### 8.5-2-3. `screen_transition_ms` 계측 추가

`MainWindow::showScreen()` 전체 구간에 전환 시간 계측을 추가해, 화면 전환 함수 기준 시간을 일관되게 기록하도록 정리했다.

적용 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

#### 8.5-2-4. 측정 문서 정리

- baseline/체크포인트/After를 한 문서에서 추적하도록 구조를 정리했다.
- `Playback export`는 현 시점 체감상 즉시 다운로드되어 기본 성능 추적 대상에서 제외했다.

적용 파일:

- 수정: [`VMS_v2_performance_tracking.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_performance_tracking.md)
- 수정: [`VMS_v2_post_phase8_execution_plan.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_post_phase8_execution_plan.md)

---

### 8.5-3. 구현 중 실제 문제와 해결

#### 문제 1. 클립 Before/After 측정 기준 혼합

원인:

- Before는 인앱 계측 도입 전 수동값이고, After는 인앱 로그와 혼합될 여지가 있었다.

해결:

- 공식 비교값은 수동 측정으로 통일
- `clip_total_ms`는 내부 동작 확인용 보조 지표로 분리

---

### 8.5-4. 종료 시점 Known issue / 후속 과제

- [P2] `frame drop rate`는 최소 계측 범위에서 제외(`N/A`)
- [P2] 성능 최종 `After`는 Phase 11 종료 후 동일 시나리오 재측정으로 확정

---

## Phase 9. 성능 안정화 스프린트

### 9-1. 목표

Phase 9의 목표는 다음 세 가지였다.

- `9-A`: 클립 저장 경로 병목 제거
- `9-B`: 스트림 전환/로그아웃 안정화
- `9-C`: 멀티뷰/전환 병목 완화와 원인 분리

---

### 9-2. 구현 내용

#### 9-2-1. `9-A` 클립 인코딩 경로 개선

핵심 변경:

- 기본 경로를 `ffmpeg stdin pipe(rawvideo bgra)`로 전환
- `PNG sequence` 경로는 `png_fallback`으로 격리
- partial write/backpressure 루프 추가
- 프레임 정규화(포맷/해상도/짝수 보정) 추가
- fallback은 "초기 진입 실패" 상황에서만 제한적으로 허용

구조:

- `encodeSnapshotViaStdinPipe(...)`
- `encodeSnapshotViaPngSequence(...)`
- `pathTag` (`stdin` / `png_fallback`)

적용 파일:

- 수정: [`clip_capture_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)
- 수정: [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)

결과:

- 채널 클립 저장 시간은 체크포인트 기준으로 큰 폭 단축
- `path:stdin`이 정상 동작하고 `png_fallback`은 예외 경로로 격리됨

#### 9-2-2. `9-B` 스트림 리소스 정리 강화

핵심 변경:

1. `setPaused()` 비동기화
- UI thread blocking 구간 제거

2. active channel diff 처리
- 이전 active set 대비 변경된 채널만 pause/resume 적용
- `active_apply_ms` 계측 추가

3. logout/401 hard cleanup 공통화
- 인증 해제 시 스트림 세션 shutdown 경로 고정
- 런타임 화면 재생성/정리 정책 보강

4. 빠른 전환 시 잔여 렌더 레이스 방어
- owner guard, retiring 가드, stale queued update 방어 보강

적용 파일:

- 수정: [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 수정: [`channel_session_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.h)
- 수정: [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

결과:

- `Main <-> CCTV/Playback` 함수 전환 시간과 프리징 체감이 완화
- 로그아웃/재로그인 시 강종 재현 빈도가 낮아짐

#### 9-2-3. `9-C` 렌더링 병목 완화 (단계적)

`9-C-1` 프로파일 재정의:

- `4-view -> QuadGrid`, `6/9-view -> DenseGrid`
- `Normal` 경로는 불필요한 `videoscale/videorate` 제거

`9-C-2` CPU 부담 완화:

- visible target 기준 dispatch
- hidden 상태 update throttling
- `MediumGrid` 미사용 상태 정리

`9-C-3` 원인 분리 계측:

- `first_frame_after_transition_ms` 도입
- `first_live_frame_after_transition_ms` 도입
- `cctv_entry_warm` 도입
- fullscreen 내부 채널 전환 경로에서 active set 재적용을 보강해, 실험 중 드러난 경로/상태 동기화 문제를 정상화

적용 파일:

- 수정: [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- 수정: [`video_render_widget.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)
- 수정: [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- 수정: [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)

결과:

- 멀티뷰 steady-state FPS는 프로파일 상한 근처로 안정화
- 병목이 `screen_transition_ms` 자체보다 `main->cctv`의 `first_live` 지연임을 분리 확인

---

### 9-3. 구현 중 실제 문제와 해결

#### 문제 1. `main->cctv` 체감 지연과 계측 지표 불일치

원인:

- `screen_transition_ms`는 짧은데 체감은 느려, 전환 함수 시간과 라이브 프레임 도착 시간이 분리되어 있었다.

해결:

- `first_frame_after_transition_ms` + `first_live_frame_after_transition_ms`를 추가해 원인을 분리

#### 문제 2. fullscreen 내부 채널 전환이 늦거나 막히는 현상

원인:

- `showScreen()`을 타지 않는 경로에서 active set이 이전 채널 기준으로 유지될 수 있었다.

해결:

- `refreshStream()`에서 현재 채널 기준 `applyActiveChannels()`를 재적용

#### 문제 3. 동적 프로파일 매칭 실험의 정책 부작용

원인:

- `main grid` 기반 동적 진입 + `900ms` 후 `Normal` 승격은 초기 진입은 빨라지지만 재로딩 멈칫이 발생했다.

해결:

- 실험 패치는 원인 검증 용도로만 사용
- 제품 정책(`fullscreen=CCTV는 Normal`) 기준으로 관련 동작 롤백
- 분석/안정성 코드만 유지

---

### 9-4. Phase 9 종료 시점 Known issue / 후속 과제

- [P1] `Main -> CCTV`에서 `first_live_frame_after_transition_ms`가 채널/스트림 조건에 따라 크게 튈 수 있음
- [P2] 해당 이슈는 Phase 9 blocker로 보지 않고, 서버/카메라 GOP/IDR 정책 및 fullscreen 정책 검토 항목으로 이월
- [P3] `CCTV` 타깃 해석 로직 일부 중복은 동작상 문제는 없으나, Phase 10 P1 구조 정리에서 공통 helper로 정리 예정

현재 판단:

- `9-A`, `9-B`, `9-C`는 체크포인트 기준 종료
- `Phase 9`는 closed, 잔여 이슈는 carry-over 관리

---

## Phase 10. 운영 정합성 및 구조 정리

### 10-1. 목표

Phase 10 초반부의 목표는 두 갈래였다.

- `10-P0`: UGV WS/ACK/종료 정책을 운영 계약 기준으로 먼저 정렬
- `10-P1-1 ~ 10-P1-9`: 큰 파일과 중복 로직을 안전하게 분리하고, UGV 운영 정책과 Playback/AppState/프로젝트 표기를 현재 구조에 맞게 정리

즉, 이 구간은 "성능 최적화"가 아니라 "운영 정합성 확보 + 구조 분해 + UX 정책 고정"에 집중한 단계였다.

---

### 10-2. 구현 내용

#### 10-2-1. `10-P0` UGV 운영 정합성 우선 작업

핵심 변경:

1. fabricated ACK 제거
- 서버가 `request.conn/disconn.ugv`, `cmd.drive`, `cmd.ptz`에 대해 즉시 성공 ACK를 만들어 보내지 않도록 정리
- gateway/UGV에서 실제 처리 완료 후 올라온 ACK만 클라이언트로 재전달

2. `error = 즉시 실패`, `*.ack = 실제 처리 완료` 계약 고정
- route/mailbox unavailable
- `INVALID_ARGUMENT`
- `unsupported type`
같은 즉시 실패는 가능하면 `error(msgId, requestType)`로 즉시 반환

3. stale ACK / error 처리 정교화
- timeout 뒤 늦게 온 ACK는 stale로 무시
- `error.msgId`가 있으면 해당 pending만 정리
- `msgId`가 없으면 `requestType` 기준 유일 후보만 정리
- ambiguous하면 로그만 남기고 오정리 방지

4. WS send 동시성 범위 축소
- 전역 `static` mutex 대신
- 클라이언트 connection-local
- gateway session-local
mutex로 범위를 줄임

5. disconnect fallback 정책 정리
- 정상 경로는 ACK 대기
- timeout/즉시 error/로그아웃/401/종료 시 강제 shutdown fallback 허용
- disconnect 중에는 `Error` 고착보다 정리 완료를 우선

6. DnD 메타 손실 수정
- `application/x-qabstractitemmodeldatalist` 우선 파싱으로 `channelId/deviceId/deviceType` 유실 방지

7. 테스트용 UGV 주입 코드 정리
- 기본 동작에서 제거

적용 파일:

- 수정: [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`login_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp)
- 수정: [`VMS_Server/src/ugv-api/UgvService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvService.cpp)
- 수정: [`VMS_Server/src/ugv-api/UgvController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvController.cpp)
- 수정: [`VMS_Server/include/UgvController.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/include/UgvController.h)

결과:

- UGV ACK/에러/종료 정책이 문서와 코드에서 같은 의미를 가지게 됨
- 운영 중 `즉시 실패 vs 처리 완료` 해석 혼선을 크게 줄임
- `10-P0`는 blocker 없이 종료 가능 상태까지 올라감

#### 10-2-2. `10-P1-1` `screens.h` 분리

핵심 변경:

- `screens.h`를 개별 화면 헤더를 모으는 umbrella header로 전환
- 화면 구현 파일들이 `screens.h` 대신 자기 화면 헤더를 직접 include하도록 재배선
- `MainWindow`도 개별 화면 헤더를 직접 include하게 정리

적용 파일:

- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`login_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- 수정: [`ugv_screen_map.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_map.cpp)
- 수정: [`ugv_screen_commands.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_commands.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)
- 추가: [`login_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.h)
- 추가: [`main_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.h)
- 추가: [`cctv_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.h)
- 추가: [`ugv_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.h)
- 추가: [`playback_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.h)

결과:

- 화면 단위 include 경계가 분명해졌고
- 이후 `MainWindow`/`common_ui` 분리를 진행하기 쉬운 기반이 생김

#### 10-2-3. `10-P1-2` `MainWindow` 구현 분리

핵심 변경:

- `mainwindow.cpp`를 책임별 TU로 분리
  - `mainwindow.cpp`: ctor/dtor, 초기 helper, `setupUi`, `initializeState`
  - `mainwindow_auth.cpp`: 인증/로그아웃/401/연결 setup
  - `mainwindow_runtime.cpp`: 런타임 화면 생성/파괴/재구성
  - `mainwindow_navigation.cpp`: 화면 전환, 윈도우 정책, closeEvent
- helper 선언은 [`mainwindow_internal.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_internal.h) 로 모으고, 구현 정의는 기존 TU에 유지

적용 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 추가: [`mainwindow_auth.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_auth.cpp)
- 추가: [`mainwindow_runtime.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_runtime.cpp)
- 추가: [`mainwindow_navigation.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_navigation.cpp)
- 추가: [`mainwindow_internal.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_internal.h)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

결과:

- `MainWindow`의 책임 분포가 더 읽기 쉬워졌고
- 이후 auth/runtime/navigation 단위 수정 시 충돌 범위를 줄일 수 있게 됨

#### 10-2-4. `10-P1-3` CCTV 타깃 해석 helper 공통화

핵심 변경:

- `MainWindow`의 warm/log 경로와 `CctvScreen`의 실제 bind 경로가 같은 CCTV 타깃 해석 규칙을 쓰도록 공통 helper화
- `activeCctvChannelId` direct path 우선
- `activeChannel` exact fallback
- `first CCTV fallback`
순서를 동일하게 고정

후속 보강:

- helper가 단순 조회가 아니라 `AppState` normalize를 포함한다는 점을 드러내기 위해
  `resolveAndNormalizeActiveCctvTarget(...)` 명시 이름을 도입
- 기존 `resolveActiveCctvTarget(...)`은 호환 alias로 유지

적용 파일:

- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`mainwindow_navigation.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_navigation.cpp)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`mainwindow_internal.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_internal.h)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)

결과:

- 계측(`warm`)과 실제 진입 타깃이 같은 규칙을 써서 분석 신뢰도가 올라감
- CCTV 진입 정책 변경 시 수정 지점이 줄어듦

#### 10-2-5. `10-P1-4` `common_ui` 단계 분리

`P1-4-1` 채널/컨텍스트 + DnD 분리:

- 신규 helper: [`channel_context_dnd_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.h), [`channel_context_dnd_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.cpp)
- 이동:
  - `DroppedChannelInfo`
  - channel id/display/rtsp helper
  - `resolveAndNormalizeActiveCctvTarget`
  - `extractDroppedChannel*`

`P1-4-2` 상태/토스트 UI 분리:

- 신규 helper: [`feedback_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.h), [`feedback_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.cpp)
- 이동:
  - `showActionStatus`
  - `showToastMessage`
  - `showPersistentStatusMessage`
  - stream/channel state helper

`P1-4-3` 캡처/저장 경로 분리:

- 신규 helper: [`capture_storage_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.h), [`capture_storage_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.cpp)
- 이동:
  - save directory helper
  - snapshot/clip 저장 helper
  - encode failure 처리

공통 원칙:

- [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)는 umbrella 유지
- 호출부 include churn 최소화
- 각 단계마다 build + 주요 화면 스모크 확인
- `applyNativeDarkTitleBar()`는 후순위 분리로 남김

적용 파일:

- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 추가: [`channel_context_dnd_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.h)
- 추가: [`channel_context_dnd_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.cpp)
- 추가: [`feedback_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.h)
- 추가: [`feedback_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.cpp)
- 추가: [`capture_storage_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.h)
- 추가: [`capture_storage_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.cpp)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

결과:

- `common_ui`의 응집도가 개선됐고
- 이후 helper 단위 수정/리뷰가 쉬워짐
- umbrella 유지 덕분에 호출부 회귀를 최소화할 수 있었음

#### 10-2-6. `10-P1-5` UGV 진입 정책 변경

핵심 정책:

- 트리에서 `UGV` 제거
- UGV 상태 read-only 패널 추가
- 멀티뷰/DnD에서 `UGV` 타입 차단
- 이벤트 상세 `출동 시작`으로만 UGV 전체화면 진입
- 디버그/관리자용 direct path는 기본 비활성 상태이며, 별도 옵션은 현재 미구현(후속 검토)

세부 구현:

1. UGV 화면 read-only 상태 패널 추가
- 연결
- 대상
- GW/UGV ID
- 속도
- RSSI
- 마지막 피드백

2. 방향키 주행 1차 추가
- `↑` 전진
- `↓` 후진
- `←` 좌회전
- `→` 우회전
- 연결 중일 때만 처리
- release/비연결 상태에서 stale key 상태 정리

3. 방향키 bypass 가드
- 트리/슬라이더/스핀박스/버튼 계열에서는 주행 키 가로채기 방지
- 다만 복합 위젯 내부 editor 포커스는 차후 runtime 확인이 필요한 low로 남김

4. 공용 트리 정책 변경
- [`SidebarWidget::populateChannelTree()`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)는 이제 `CCTV`만 노출
- 구조는 `모델명 -> 채널명`
- 같은 모델이 여러 장비면 `모델명 (deviceId)` 형태로 중복 구분

5. UGV 직접 진입 경로 정리
- `Main/CCTV/Playback` 채널 트리 더블클릭에서 `UGV` 진입 제거
- 멀티뷰 셀 더블클릭에서도 `UGV`는 직접 진입하지 않고 안내 메시지만 표시
- 멀티뷰 DnD는 `CCTV`만 허용
- `openUgvRequested()`는 이벤트 상세 `출동 시작` 경로만 유지

적용 파일:

- 수정: [`common_widgets.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.h)
- 수정: [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- 수정: [`ugv_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.h)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

결과:

- 코드 기준으로는 `P1-5` 정책이 모두 반영됨
- 서버 미기동 상태라 UGV 출동 성공-path E2E는 아직 확인하지 못함
- 현재 표기는 `code complete / E2E blocked`가 맞음

#### 10-2-7. `10-P1-6` UGV error 계약 공통화 및 상태 정책 정리

핵심 정책:

- 세션 상태 전이는 `conn/disconn`, WS 종료, route unavailable, auth(401) 등 연결 축에서만 발생
- `cmd.drive/cmd.ptz` 실패와 timeout은 기본적으로 명령 실패로만 처리하고 세션은 `ConnectedUgv` 유지
- 단, `UNAUTHORIZED`, `ROUTE_UNAVAILABLE`, `SOCKET_CLOSED`, `DEVICE_OFFLINE` 같은 연결 붕괴형 에러는 예외적으로 세션 전이를 허용
- 이 예외 전이는 `cmd.*`에 대해 현재 매칭된 pending이 있을 때만 허용
- `error.msgId`가 있으면 exact 매칭만 허용하고, `msgId`가 없을 때만 `requestType` fallback을 허용

구현:

1. 서버 error 스키마 공통화
- [`UgvWsMessages.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/include/UgvWsMessages.h) 에 공통 `makeUgvErrorMessage(...)` helper 추가
- `UgvService` / `UgvController`가 같은 스키마를 사용하도록 통일

2. 클라이언트 error 처리 정밀화
- `error.msgId` 기반 pending 정리
- `msgId`가 비어 있는 경우에만 `requestType` 기준 유일 후보 fallback
- stale/late `cmd.* error`가 새 pending을 잘못 잡는 경로 차단

3. 세션 상태 정책 보강
- 일반 `cmd.drive/cmd.ptz` 실패는 세션을 내리지 않음
- 연결 붕괴형 error만 예외적으로 세션 전이
- `UGV` 상태 라벨/토스트는 유지

적용 파일:

- 수정: [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- 수정: [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- 추가: [`VMS_Server/include/UgvWsMessages.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/include/UgvWsMessages.h)
- 수정: [`VMS_Server/src/ugv-api/UgvService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvService.cpp)
- 수정: [`VMS_Server/src/ugv-api/UgvController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvController.cpp)

결과:

- `error` / `ack` / pending 정리 규칙이 문서 계약과 거의 일치하는 상태가 됨
- 명령 실패와 세션 붕괴를 구분할 수 있게 되어 UGV UX가 덜 과민해짐
- 코드 기준으로는 `P1-6` 종료 가능 상태까지 올라감

#### 10-2-8. `10-P1-7` `playback_screen.cpp` 구현 분리

핵심 변경:

1. 메인 TU 슬림화
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)는 생성자/UI 구성, `setPlaybackService()`, `showEvent()`, `resizeEvent()`, snackbar 관련만 유지

2. 타임라인/재생 구현 분리
- [`playback_screen_timeline.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_timeline.cpp)에
  - `startPlaybackForChannel`
  - `loadTimelineForChannel`
  - `applyTimelineResult`
  - `requestPlaybackStream`
  - `applyPlaybackCapabilities`
  - `refreshTimelineUi`
  - `rebuildEventMarkers`
  를 이동

3. export 구현 분리
- [`playback_screen_export.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_export.cpp)에
  - `isExportBusy`
  - `cancelExportOperations`
  - `openExportDialog`
  - `pollExportStatus`
  - `startExportDownload`
  - `updateExportUiState`
  를 이동

4. 공용 helper 정리
- [`playback_screen_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_helpers.h)를 추가해
  - `playbackRates`, `rateText`
  - timestamp / slider / marker / source display helper
  를 공통화

적용 파일:

- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- 추가: [`playback_screen_timeline.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_timeline.cpp)
- 추가: [`playback_screen_export.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_export.cpp)
- 추가: [`playback_screen_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_helpers.h)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

결과:

- Playback 화면 구현이 `UI glue / timeline / export` 축으로 나뉘어 리뷰/수정 난이도가 낮아짐
- helper 중복이 제거돼 이후 드리프트 가능성이 낮아짐
- 코드 구조 기준으로는 `P1-7` 종료 가능 상태가 됨

#### 10-2-9. `10-P1-8` `AppState` 병렬 배열 개선

핵심 변경:

1. 셀 구조체 도입
- [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)에 `MainGridCellState` 추가
- `displayName + channelId + deviceId`를 한 단위로 묶음

2. 셀 조작 helper 공통화
- `setGridCell(index, ...)`
- `clearGridCell(index)`
- `clearAllGridCells()`

3. 읽기/쓰기 마이그레이션
- [`mainwindow_auth.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_auth.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
에서 셀 읽기/쓰기 기준을 `gridCells`로 전환

4. 병렬 배열 제거
- 기존 `cellChannels/cellChannelIds/cellDeviceIds`는 코드에서 제거
- `activeChannel`, `activeCctvChannelId`, `activeUgv*` 브리지는 이번 단계에서는 유지

적용 파일:

- 수정: [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`mainwindow_auth.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_auth.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)

결과:

- 멀티뷰 셀 상태가 단일 구조로 정리돼 배열 간 불일치 리스크가 제거됨
- 셀 write path가 helper로 통일돼 이후 유지보수성이 좋아짐
- 코드 기준으로는 `P1-8` 종료 가능 상태가 됨

#### 10-2-10. `10-P1-9` 프로젝트명/타이틀 `v2` 전환

핵심 변경:

1. 윈도우 타이틀 정리
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp) 의 타이틀을 `VMS v2`로 변경

2. 빌드/산출물 이름 정리
- [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt) 의
  - `project`
  - `qt_add_executable/add_executable`
  - `target_link_libraries`
  - `install`
  - `qt_finalize_executable`
  를 `VMS_v2` 기준으로 통일

3. 의도적으로 유지한 항목
- `QSettings` 키(`TeamClue/VMS_v1`)와 일부 프로토콜/문자열(`vms.*.v1`)은 정책 변경을 피하기 위해 이번 단계에서 건드리지 않음

결과:

- 빌드 산출물과 윈도우 표기가 실제 프로젝트 버전과 일치
- 실행 스크립트/수동 테스트 커맨드는 `VMS_v2.exe` 기준으로 후속 정리 필요
- 코드 기준으로는 간단 패치 범위 내에서 완료

---

### 10-3. 구현 중 실제 문제와 해결

#### 문제 1. UGV ACK 의미가 즉시 응답/처리 완료 ACK 사이에서 흔들리던 문제

원인:

- 서버가 fabricated ACK를 만들어 보내는 경로와
- gateway/UGV 처리 완료 ACK가 섞여 있었다.

해결:

- `error = 즉시 실패`, `*.ack = 실제 처리 완료` 계약으로 문서/코드 동시 정렬
- `msgId/requestType` 기반 stale/error 처리까지 함께 고정

#### 문제 2. 큰 단일 파일들 때문에 구조 정리가 위험하게 느껴지던 문제

원인:

- `screens.h`, `mainwindow.cpp`, `common_ui.cpp`가 한 번에 너무 많은 책임을 들고 있었다.

해결:

- `umbrella 유지 + 구현 우선 분리` 전략으로 진행
- 호출부 churn을 최소화하면서 TU/helper 단위로 분리

#### 문제 3. CCTV 타깃 해석 규칙이 경로마다 달라질 수 있던 문제

원인:

- `MainWindow` warm 계측용 로직과 `CctvScreen` 실제 bind 로직이 따로 진화하고 있었다.

해결:

- 공통 helper로 묶고
- normalize 성격까지 이름에 드러내도록 정리

#### 문제 4. UGV 진입 경로가 여러 군데에 남아 정책이 흔들리던 문제

원인:

- 트리
- 멀티뷰 셀
- 이벤트 상세
가 동시에 UGV fullscreen 진입 경로가 될 수 있었다.

해결:

- 기본 UX에서는 이벤트 상세 `출동 시작`만 남기고
- 나머지 direct path는 제거 또는 차단

#### 문제 5. UGV 명령 실패가 세션 전체 실패처럼 보이던 문제

원인:

- `cmd.drive/cmd.ptz`의 즉시 실패와 timeout도 넓게 `SessionState::Error`로 해석될 수 있었다.

해결:

- `P1-6`에서 연결 축과 명령 축을 분리
- 일반 명령 실패는 command-level failure로만 처리
- 연결 붕괴형 error만 예외적으로 세션 전이를 허용

#### 문제 6. `PlaybackScreen`이 화면/UI와 재생/export 구현을 한 파일에 모두 들고 있던 문제

원인:

- `playback_screen.cpp`가 UI 구성과 타임라인, export, 다운로드까지 함께 담고 있어 변경 범위가 넓었다.

해결:

- `P1-7`에서 메인 TU / timeline / export / helper로 구현을 분리
- 기존 인터페이스와 UI 동작은 유지

#### 문제 7. `AppState` 멀티뷰 셀 상태가 병렬 배열이라 불일치 위험이 있던 문제

원인:

- `displayName`, `channelId`, `deviceId`가 별도 배열로 존재해, 일부 경로에서 한쪽만 갱신될 여지가 있었다.

해결:

- `P1-8`에서 `MainGridCellState` 구조체와 `gridCells` 배열로 통합
- 읽기/쓰기 경로를 단계적으로 전환하고 병렬 배열을 제거

---

### 10-4. Phase 10 현재 상태 / Known issue

- `10-P0`: 종료 가능
- `10-P1-1`: 종료
- `10-P1-2`: 종료
- `10-P1-3`: 종료
- `10-P1-4`: 종료
- `10-P1-5`: 코드 기준 종료, E2E는 서버 미기동으로 blocked
- `10-P1-6`: 코드 기준 종료, UGV 서버 미기동으로 runtime/E2E는 후속 확인 필요
- `10-P1-7`: 코드 분리/빌드 반영 기준 종료 (Playback 수동 스모크 기준 마감)
- `10-P1-8`: 종료 (병렬 배열 제거 + `gridCells` 전환, 빌드/스모크 확인 완료)
- `10-P1-9`: 간단 패치 기준 완료

잔여 항목:

- [Low] UGV 방향키 bypass는 `QSpinBox` 내부 editor 같은 복합 포커스 상황을 runtime에서 한 번 더 확인 필요
- [Ops] `VMS_v2.exe` 기준으로 실행 스크립트/측정 커맨드 정리 필요

---

### 10-5. `10-P1-9` 이후 후속 반영 메모

`10-P1-9` 종료 이후에는 구조 분해 자체보다, 실제 사용자 흐름 기준의 UI/운영 보정이 연속적으로 들어갔다. 이 구간은 별도 Phase 번호로 재정의하지 않고, 기존 Phase 10 결과물을 운영 가능한 수준으로 다듬는 후속 정리로 관리했다.

핵심 후속 반영은 아래와 같다.

1. 디자인/리소스 공통화
- `v1_theme.qss`/`v2_theme.qss` 이중 적용 구조를 정리하고, 실사용 테마를 [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss) 단일 파일로 통합했다.
- topbar는 화면별 텍스트 타이틀(`VMS`, `CCTV 전체화면`, `UGV 전체화면`, `이전영상`)을 제거하는 방향으로 최소화했고, 창 자체 아이콘과 중복되는 topbar 로고도 제거했다.
- 리소스 경로는 [`resources.qrc`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/resources.qrc) 기준으로 재정리했고, `clue_logomark.svg`를 앱/메인 윈도우 아이콘으로 연결했다.
- Windows 네이티브 타이틀바도 [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)의 `applyNativeDarkTitleBar(...)`를 통해 앱 톤과 같은 다크 캡션 색으로 맞췄다.

2. DeviceCheck/메인 사이드바 구조 통일
- 장치확인 화면은 `CCTV`/`UGV` 섹션 라벨 아래 `모델 (.옥텟) -> Channel n` 구조로 고정했다.
- 메인 사이드바도 같은 계층과 표기 규칙으로 맞췄고, `(#deviceId)` 표기는 제거하고 `모델명 (.마지막 옥텟)`을 공통 규칙으로 채택했다.
- 장치확인 창 폭/내부 레이아웃도 `560 x 380` 기준으로 재정리해 과도한 가로 공백을 줄였다.

3. 멀티뷰/전체화면 표현 일관성 보정
- 멀티뷰 셀 선택 표현은 QSS selector 대신 코드 기반 selection bar로 정리해 어느 셀에서도 동일하게 주황 테두리가 보이게 했다.
- 멀티뷰 OSD는 투명 오버레이 실험 대신 상단 얇은 바 구조로 정리했고, 채널명/상태/X 버튼 배치를 안정화했다.
- 사이드바와 콘텐츠 사이 경계선은 멀티뷰/CCTV/UGV/Playback 모두 공통 separator 한 줄만 쓰도록 정리했다.
- CCTV 전체화면은 불필요한 내부 패널/보더를 제거해 멀티뷰와 더 비슷한 "영상 우선" 구조로 정리했다.

4. UGV 운영 정책/컨트롤 UX 정리
- 메인 멀티뷰에서는 `UGV`를 자동 배치/직접 진입/드래그앤드롭 대상에서 제외하고, 이벤트 상세의 `출동 시작` 경로에서만 UGV 화면으로 진입하도록 정책을 고정했다.
- UGV 화면은 사이드바 read-only 정보 영역을 공통 사이드바 스타일로 되돌리고, 필요한 정보는 영상 근처 정보 패널로 재배치했다.
- 드라이브 컨트롤은 `pressed/released` 기반 hold 동작으로 정리했고, 이후 PTZ 방향키도 같은 방식(누르는 동안 반복 전송, release 시 stop)으로 맞췄다.
- 연결 전 상태에서는 드라이브/팬틸트 컨트롤이 비활성화되고, 활성화 상태와 구분되도록 disabled 스타일도 별도로 보강했다.

5. Playback 안정 복구와 타임라인 책임 분리
- `PlaybackScreen`은 구조 분리 이후 다시 `PB-1/PB-2` 관점으로 안정화했다.
- `sliderReleased -> requestPlaybackStream(...)` 자체는 제거하지 않고, 문제의 핵심이던 `24h range / playable range / current position / wallclock fallback` 혼합 상태를 단계적으로 분리했다.
- 초기 단계에서는 marker overlay 마우스 간섭 제거, wallclock 추정 단순화, `requestPlaybackStream()` 성공 전에 타임라인 기준 시각을 바꾸던 흐름 제거를 우선 적용했다.
- 이후 타임라인은 `24시간 전체 축`, `재생 가능 구간`, `현재 재생 절대 시각`을 분리하는 방향으로 재정리했고, 재생 시작 후 시간 라벨/핸들이 실제로 계속 진행되도록 복구했다.
- 임의 슬라이더 seek는 정책상 보류하고, 트리 클릭(`from -> to`)과 이벤트 마커 클릭(`event ts -> to`)만 확실한 진입 경로로 유지했다.

6. Playback 마커 성능/가시성 보정
- 타임라인 조회 응답 후 이벤트 마커가 과도하게 많아 UI가 멈추는 문제가 확인되어, playback 전용으로 `같은 채널 + 같은 이벤트 타입 + 3분 이내` 기준 마커 클러스터링을 추가했다.
- 이 조정으로 `타임라인 조회 중...`에서 응답 없음이 뜨던 케이스를 완화했고, 마커 클릭 재생 기능은 유지했다.
- playable range는 envelope 형태의 단일 오렌지 bar로 유지하고, 이벤트 마커는 얇은 빨간 선으로 정리했다.

7. 팝업/토스트/상태라벨 정책 정리
- [`feedback_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.cpp)와 각 화면 호출부를 기준으로 `PopupManager`, `showToastMessage`, `showActionStatus` 사용 정책을 정리했다.
- 메인 제한 안내는 토스트, CCTV Zoom/Focus 실패는 상태라벨, UGV 정보성 안내는 상태라벨, Playback 타임라인 실패는 상태라벨 중심으로 내렸다.
- Export는 `검증/중복/일반 실패`는 상태라벨, `경로/URL/실파일 저장 실패`는 팝업으로 선별 유지했다.
- 이 정책과 최신 호출 현황은 [`VMS_v2_popup_toast_status_matrix.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_popup_toast_status_matrix.md)에 별도 정리했다.

8. DeviceCheck 첫 진입 로딩 안정화
- 로그인 직후 `DeviceCheck` 첫 진입에서 장치 일부만 뜨거나 `Operation canceled`가 발생하던 문제를 보정했다.
- `showScreen(DeviceCheck)` 이후 약간의 지연을 두고 `refreshDevices()`를 시작하도록 조정했고, 장치 목록 조회는 `Operation canceled`에 한해 1회 자동 재시도하도록 보강했다.
- 장치별 채널 fan-out도 동시 개수를 제한하고 1회 재시도를 허용해, 첫 진입 불안정성을 줄였다.
- 네트워크 응답 처리에서는 닫힌 `QNetworkReply`를 무조건 읽지 않도록 [`rest_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/rest_client.cpp)도 방어했다.

9. 문자열/리소스 정리
- `mainwindow_auth.cpp`, `capture_storage_helpers.cpp`, `playback_screen_export.cpp` 등에서 남아 있던 한글 인코딩 깨짐 문자열을 복구했다.
- popup/button/icon 관련 리소스명도 실제 파일 기준으로 정리해, `clue_logomark.svg`/`clue_logo.png` 혼용으로 인한 빌드 오류를 줄였다.

10. 모달 UX/밀도 재정렬 (설정/알림센터/이벤트검색/이벤트상세)
- 설정 모달은 `560x380`(DeviceCheck와 동일)로 맞추고, 탭/내부 여백/버튼 밀도를 축소해 작은 창에서도 정보가 눌리지 않게 정리했다.
- 설정 > 장치관리의 `ONVIF 검색` 경로는 미노출 처리하고, 수동 등록 장치 관리 흐름으로 단순화했다.
- 알림센터는 필터 버튼/하단 닫기 버튼을 제거하고, 최근 24시간 고정 + 헤더 정렬형(`시간 | 채널 | 이벤트 종류`) 목록으로 정리했다.
- 이벤트검색은 모달 크기/필터 행/결과 리스트를 알림센터와 같은 표형 UX로 통일했다.
  - 필터: 1행 배치 (`시작/종료/타입/채널/검색`)
  - 결과: `QTreeWidget` 헤더 정렬형 (`시간 | 채널(모델명(.옥텟)) | 이벤트 종류`)
- 이벤트 타입 매핑에 `IvaArea -> 영역 침입 감지`를 반영하고, 이벤트검색/알림센터/이벤트뷰 표시를 같은 번역 규칙으로 맞췄다.
- 이벤트상세 모달은 현재 `520x440` 기준으로 재정렬했다.
  - 노출 정보는 `이미지 + 날짜 + 채널 + 이벤트`만 유지
  - `요약`, `Bestshot ID`, 하단 `닫기` 버튼 제거
  - 닫기는 타이틀바 `X` 기준
  - `UGV 출동` 버튼은 이벤트뷰 경로(`showDispatchButton=true`)에서만 표시하며, 이미지 폭과 맞춘 full-width red 버튼으로 통일했다

이 후속 반영들은 구조를 다시 뒤엎기보다, 실제 테스트 과정에서 드러난 운영 UX 문제를 빠르게 줄이는 쪽에 집중했다. 따라서 본 문서의 Phase 8.5~10 본문을 대체하지 않고, "구조 정리 이후 운영 보정이 어떻게 이어졌는가"를 기록하는 보조 이력으로 남긴다.

---

## Phase 8.5~10-P1-9 동안 추가/수정/삭제된 주요 파일 요약

### 신규 추가

- [`login_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.h)
- [`main_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.h)
- [`cctv_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.h)
- [`ugv_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.h)
- [`playback_screen.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.h)
- [`mainwindow_auth.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_auth.cpp)
- [`mainwindow_runtime.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_runtime.cpp)
- [`mainwindow_navigation.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_navigation.cpp)
- [`mainwindow_internal.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_internal.h)
- [`channel_context_dnd_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.h)
- [`channel_context_dnd_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_context_dnd_helpers.cpp)
- [`feedback_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.h)
- [`feedback_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/feedback_ui_helpers.cpp)
- [`capture_storage_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.h)
- [`capture_storage_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/capture_storage_helpers.cpp)
- [`VMS_Server/include/UgvWsMessages.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/include/UgvWsMessages.h)
- [`playback_screen_timeline.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_timeline.cpp)
- [`playback_screen_export.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_export.cpp)
- [`playback_screen_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_helpers.h)

### 삭제

- 없음

### 수정 범위가 컸던 핵심 파일

- [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- [`mainwindow_auth.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_auth.cpp)
- [`mainwindow_runtime.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_runtime.cpp)
- [`mainwindow_navigation.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_navigation.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
- [`settings_dialog.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.cpp)
- [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)
- [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- [`playback_screen_timeline.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_timeline.cpp)
- [`playback_screen_export.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_export.cpp)
- [`VMS_Server/src/ugv-api/UgvService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvService.cpp)
- [`VMS_Server/src/ugv-api/UgvController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/VMS_Server/src/ugv-api/UgvController.cpp)
- [`VMS_v2_performance_tracking.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_performance_tracking.md)
- [`VMS_v2_post_phase8_execution_plan.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_post_phase8_execution_plan.md)
- [`VMS_v2_popup_toast_status_matrix.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_popup_toast_status_matrix.md)
- [`troubleshooting_multiview_streaming.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/troubleshooting/troubleshooting_multiview_streaming.md)

---

## 결론

Phase 8.5~10-P1-9는 "측정 가능성 확보 -> 병목 제거 -> 운영 계약 정리 -> 구조 분해 -> 상태 모델 정리 -> v2 표기 정리" 순으로 진행된 구간이었다.

핵심 변화:

- baseline 계측 체계를 먼저 세워 개선을 정량 추적할 수 있게 만들었다.
- 클립 인코딩 병목(`PNG sequence`)을 `stdin pipe`로 전환해 큰 폭의 체감 개선을 만들었다.
- 스트림 전환/정리 정책을 보강해 프리징/강종 리스크를 낮췄다.
- 멀티뷰 프로파일과 hidden dispatch 보강으로 steady-state 안정성을 개선했다.
- `first_frame`/`first_live` 계측으로 잔여 병목을 정확히 분리했다.
- UGV WS/ACK/error/disconnect 정책을 문서와 코드에서 같은 의미로 맞췄다.
- `screens.h`, `MainWindow`, `common_ui`를 단계적으로 분리해 이후 변경 비용을 낮췄다.
- UGV 진입 정책을 트리/멀티뷰 direct path에서 이벤트 상세 dispatch path 중심으로 재정렬했다.
- Playback 구현을 `main / timeline / export / helper` 단위로 분리해 이후 수정 범위를 줄였다.
- `AppState` 멀티뷰 셀 상태를 `gridCells`로 통합해 병렬 배열 불일치 리스크를 제거했다.
- 설정/알림센터/이벤트검색/이벤트상세 모달을 공통 톤/밀도 기준으로 재정렬해 운영 UX 일관성을 높였다.
- 프로젝트 타이틀/타깃/산출물 이름을 `VMS_v2` 기준으로 맞췄다.

현재 상태:

- `Phase 9`는 체크포인트 기준 종료
- `10-P0`는 운영 계약 기준 종료
- `10-P1-1 ~ 10-P1-9`는 대부분 코드 기준 종료 상태까지 정리
- `10-P1-5`의 UGV 출동 성공-path E2E는 서버 미기동으로 blocked
- `10-P1-6`은 UGV 서버 실환경 검증이 남아 있음
- `10-P1-7`, `10-P1-8`은 빌드/스모크 기준으로 마감 완료
- 모달 UX 후속 정리(설정/알림센터/이벤트검색/이벤트상세)는 코드 반영 완료
- residual 이슈(`Main -> CCTV first_live` 튐)는 Phase 11 이후 정책/서버 조건 검토 항목으로 관리

즉, Phase 8.5~10-P1-9는 이후 `Phase 10 후반~11`의 남은 구조 정리, UGV 운영 UX, 최종 After 측정을 위한 안정적 기반을 만든 단계다.

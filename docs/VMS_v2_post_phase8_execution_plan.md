# VMS v2 후속 개발 실행 계획 (Post-Phase8)

기준:
- 리뷰 기준 문서: `vms_v2_phase8_final_review.md`
- 기존 통합 계획: `VMS_v2_dev_execution_plan.md`

문서 목적:
- Phase 1~8 이후 남은 작업을 실행 중심으로 분리
- 성능/정합성/구조/UI 개선을 순차적으로 진행
- 서버 환경 의존으로 막힌 E2E 검증 항목을 별도 트랙으로 관리

---

## 0. 현재 기준선

- 구현 상태: Phase 1~8 기능 구현 완료
- 검증 상태:
  - Playback export 통합 검증(`7-B-4`): Blocked (서버 export 최신 코드 배포 대기)
  - UGV success-path E2E(`gateway /ugv/ws`): Blocked (gateway 미연결/미구현)
- 핵심 기술 부채:
  - 앱 무거움(디코드/색공간 변환/프레임 복사 비용)
  - 클립 저장 지연(PNG 직렬 저장 기반)
  - UGV ACK 계약/WS 송신 동시성 리스크
  - `mainwindow.cpp`, `screens.h`, `common_ui.cpp` 비대화

---

## 1. 실행 원칙

1. 성능 -> 정합성/구조 -> UI/UX 순서로 진행한다.
2. Blocked 검증 항목은 구현과 분리해서 별도 검증 트랙으로 관리한다.
3. 각 단계는 "코드 반영 + 수동 검증 체크리스트"까지 완료해야 닫는다.
4. 서버/클라이언트 계약 변경은 메시지 스키마를 먼저 확정한 뒤 구현한다.

---

## 2. 로드맵 개요

- Phase 8.5 (선행): 성능 baseline 계측/기록
- Phase 9 (1주): 성능 안정화
- Phase 10 (1주): 운영 정합성 + 구조 정리
- Phase 11 (1~2주): UI/UX 개선
- Verification Track (환경 준비 시): Blocked 항목 통합 검증

---

## Phase 8.5. 성능 baseline 계측

목표:
- Phase 9 최적화 전에 `Before` 기준선을 확보한다.
- 발표/공유용 성능 수치를 재현 가능한 방식으로 기록한다.

대상:
- 인앱 계측: `clip_capture_manager.cpp`, `stream_player.cpp`, `video_render_widget.cpp`, `mainwindow.cpp`
- 외부 측정: Windows `PerfMon` 또는 동등 도구
- 기록 문서: `VMS_v2_performance_tracking.md`

작업:
- 인앱 baseline 계측은 최소 범위로 시작
  - `clip_capture_manager.cpp`
    - 클립 저장 `start -> complete` 전체 시간만 `QElapsedTimer`로 계측
    - 기록 지표: `clip_total_ms`
  - `stream_player.cpp` / `video_render_widget.cpp`
    - 멀티뷰 진입 후 초당 rendered frame count만 집계
    - 기록 지표: `render_fps`
  - `mainwindow.cpp`
    - 주요 화면 전환(`login->main`, `main->cctv`, `main->playback`) 시작/완료 시각만 계측
    - 기록 지표: `screen_transition_ms`
- 출력 방식은 우선 `qInfo()` 기반 경량 로그로 고정
  - 정식 로그 포맷(`jsonl`, 필드 체계화)은 Phase 9 이후 After 측정 시 필요하면 보강
- 외부 측정 기준 고정
  - 기본 도구는 Windows `PerfMon`
  - baseline 단계에서는 프로세스 CPU / RAM 위주로 본다
- 측정 조건 고정
  - 동일 스트림/동일 해상도/동일 뷰 수
  - 최소 3회 반복 평균
  - Debug/Release 구분
  - 가능하면 발표용 수치는 Release 기준으로 분리 기록

완료 기준:
- `VMS_v2_performance_tracking.md`에 baseline 수치가 채워짐
- `clip_total_ms`, `render_fps`, `screen_transition_ms`의 baseline이 확보됨
- Phase 9 이후 `Before / After` 비교가 가능한 최소 계측 포인트가 확보됨

---

## Phase 9. 성능 안정화 스프린트

목표:
- 체감 성능 저하(무거움, 프리징, 저장 지연)를 먼저 줄인다.

### 9-A. 클립 인코딩 경로 개선

대상:
- 핵심 수정: `clip_capture_manager.cpp/.h`
- 연쇄 수정: 클립 버튼 처리부(`main_screen.cpp`, `cctv_screen.cpp`, `ugv_screen.cpp`)

작업:
- `ffmpeg stdin pipe` 방식 PoC/본적용 검토
- 현행 `PNG 직렬 저장` 경로 제거 또는 fallback 경로로 격리
- 인코딩 취소/실패 코드를 기존 `EncodeResult` 체계에 맞춰 유지
- `stdin pipe` 적용이 막히는 경우에만 FPS/버퍼 상한 조정 또는 JPEG 임시 경로를 fallback으로 검토

완료 기준:
- 동일 길이 클립 저장 시간이 현행 대비 유의미하게 단축
- 취소/실패 UX 회귀 없음

### 9-B. 스트림 리소스 정리 강화

대상:
- 핵심 수정: `mainwindow.cpp`, `channel_session_manager.cpp`
- 연쇄 수정: `stream_player.cpp`, 화면 전환 경로(`showScreen`, 로그인/로그아웃/401)

작업:
- `9-B-1. setPaused() 비동기화`
  - `StreamPlayer::setPaused()`에서 `gst_element_get_state(..., 200ms)` 대기 제거
  - `gst_element_set_state()` 호출 후 즉시 return, 상태 확인은 bus/poll 경로에 맡김
  - 적용 직후 `screen_transition_ms`와 체감 프리징만 먼저 확인
- `9-B-2. active channel 적용 diff 처리`
  - `ChannelSessionManager`에 마지막 active set을 저장하고 변경된 채널만 `setPaused()` 호출
  - 세션 전체 loop는 유지하되, pause/resume 호출은 변경분 기준으로 축소
  - `showScreen` 연속 호출 debounce는 후순위로 두고, diff 적용 후 필요 시만 검토
- `9-B-3. logout/401 hard cleanup 공통화`
  - `clearAuthenticationState()`에 `ChannelSessionManager::shutdown()` 호출을 공통화
  - 로그아웃/401 후 재로그인 시 runtime screen 재바인딩과 세션 재생성 정상 동작 확인
- 측정 로그:
  - `perf metric=screen_transition_ms` 기존 유지
  - `perf metric=render_fps` 기존 유지
  - `perf metric=active_apply_ms context=changed:<N>` 추가

완료 기준:
- `Main <-> CCTV/Playback` 왕복 시 전환 지연 체감 감소
- 멀티뷰 복귀 직후 프리징 완화
- 로그아웃/401 후 스트림 세션/파이프라인 잔존 감소
- 재로그인 후 불필요 리소스 잔류와 재생 회귀 없음

### 9-C. 렌더링 병목 완화 (단계적)

대상:
- 핵심 수정: `stream_player.h/.cpp`, `main_screen.cpp`, `video_render_widget.cpp`

작업:
- `9-C-1. 멀티뷰 프로파일 재정의`
  - `StreamQualityProfile`에 `QuadGrid`를 추가하고, `단일화면=Normal`, `4-view=QuadGrid`, `6/9-view=DenseGrid`로 재매핑
  - `Normal` 경로에서는 불필요한 `videoscale/videorate`를 제거해 decode -> convert -> appsink 최소 경로로 단순화
  - `QuadGrid`는 보수적으로 `854x480 @ 10fps` 수준부터 시작하고, `DenseGrid`는 현재처럼 더 낮은 해상도/fps를 유지
  - 1차에서는 하드웨어 디코더 전환까지 넣지 않고, 현재 구조 안에서 멀티뷰 입력 비용을 먼저 낮춘다
- `9-C-2. CPU copy / draw 부담 완화`
  - `publishSampleFrame()`의 `QImage` 복사 횟수와 hidden 상태 draw/update를 줄이는 방향 검토
  - `render_fps` 로그는 visible 상태 기준으로만 남기도록 정리해 측정 노이즈 감소
  - 필요 시 `first_frame_after_transition_ms` 계측을 추가해 “전환 함수”와 “첫 프레임 도착”을 분리 측정
- `9-C-3. first-frame 병목 완화 및 고도화`
  - `9-C-3-a`: `CctvScreen::showEvent()`의 중복 `refreshStream()` 호출을 정리해 fullscreen 진입 지터를 먼저 축소
  - `9-C-3-b`: `first_live_frame_after_transition_ms` 계측을 추가해 “캐시 프레임 표시”와 “실제 라이브 첫 프레임 도착”을 분리 측정
  - `9-C-3-c`: 필요 시 cached frame 즉시 표시를 적용해 검은 화면/무반응 체감을 완화 (UX 보조 카드)
  - `9-C-3-d`: 카메라/서버 GOP 및 IDR 정책 확인(예: GOP 2s -> 0.5~1s, resume 시 IDR 요청 가능 여부)
  - `9-C-3-e`: 위 단계 후에도 부족할 때만 texture 업로드(`glTexSubImage2D`) 또는 HW decode(`d3d11h264dec`)를 후속 검토
- 주의:
  - 현재 더 큰 병목은 `paint`보다 `decode + videoconvert + appsink/QImage copy` 쪽이므로, 렌더러 전면 교체보다 프로파일 재정의와 파이프라인 경량화를 우선한다

완료 기준:
- `Main <-> CCTV/Playback` 체감 전환이 추가 개선됨
- 멀티뷰 FPS 하강 구간(1~3fps) 빈도가 감소함
- 로그아웃/재로그인/빠른 전환 회귀 없음
- 크래시/검은화면 회귀 없음

### Phase 9 종료 판단

- `9-A`, `9-B`, `9-C`는 체크포인트 기준으로 종료한다.
- 클립 저장 경로, 전환/리소스 정리, 멀티뷰 병목 완화는 목표 수준까지 안정화되었고, `Before/Checkpoint` 근거도 확보되었다.
- 잔여 이슈는 `Main -> CCTV` fullscreen 진입 시 `first_live_frame_after_transition_ms`가 채널/스트림 조건에 따라 크게 튈 수 있다는 점이다.
- 이 이슈는 현재 구조에서 blocker로 보지 않고, 서버/카메라 GOP·IDR 정책 확인 및 이후 fullscreen 진입 정책 검토 항목으로 이월한다.
- `CCTV` 타깃 해석 로직(`mainwindow.cpp` warm 로그용 / `cctv_screen.cpp` 실제 bind용) 중복은 현재 동작상 문제를 만들지 않으므로 Phase 10 구조 정리(P1)에서 공통 helper로 정리한다.

---

## Phase 10. 운영 정합성 + 구조 정리

목표:
- 실서비스 관점 리스크(P0) 우선 해소
- 유지보수 난이도를 높이는 구조 문제(P1) 정리

### 10-P0. 운영 정합성 우선 작업

대상:
- 서버: `VMS_Server/src/ugv-api/UgvController.cpp`, `VMS_Server/src/ugv-api/UgvService.cpp`
- 클라이언트: `ugv_service.cpp`, `common_ui.cpp`, `mainwindow.cpp`, `login_screen.cpp`

작업:
1. UGV ACK 계약 확정
- `error = 즉시 실패`, `*.ack = 실제 처리 완료` 의미를 문서와 코드에서 통일
- 클라이언트가 받는 `*.ack`는 서버의 즉시 수신 응답이 아니라, gateway/UGV에서 올라온 처리 완료 ACK를 서버가 재전달한 것으로 정의
- `request.conn.ugv`, `request.disconn.ugv`, `cmd.drive`, `cmd.ptz`에 대해 fabricated ACK를 만들지 않고 실제 처리 완료 ACK만 전달
- `request.conn/disconn` ACK timeout 기준은 `5000ms`로 고정
- gateway route/mailbox unavailable 등 즉시 실패 조건은 timeout으로 넘기지 않고 `error(msgId, requestType)`로 즉시 반환
- `INVALID_ARGUMENT`, `unsupported type` 등 tracked request의 즉시 실패도 가능하면 동일하게 `error(msgId, requestType)`를 포함해 반환

2. WS send 동시성 보호
- 동일 소켓 `sendText` 경로를 connection/session 단위로 동기화
- 다중 UGV 세션이 전역 mutex 하나로 직렬화되지 않도록 범위를 최소화

3. DnD direct path 메타 손실 수정
- `application/x-qabstractitemmodeldatalist` 우선 파싱
- `hasText` 조기 return 경로 때문에 `channelId/deviceId/deviceType`가 유실되던 흐름 보정

4. stale ACK 가드 강화
- timeout 후 늦게 도착한 ACK는 상태 전환에 사용하지 않음
- pending 없는 `conn/disconn` 지연 ACK는 로그만 남기거나 조용히 무시하고, 현재 in-flight 요청의 pending은 건드리지 않음
- `cmd.drive/cmd.ptz`의 late ACK도 동일하게 현재 in-flight 요청을 덮어쓰지 않고 stale로 처리
- 서버 `error(msgId/requestType)`가 오면 해당 pending만 정리하고, ambiguous한 경우는 로그만 남기고 오정리하지 않음
- 클라이언트 `error` 처리도 `msgId` exact 매칭 우선, 없으면 `requestType` 기준 유일 후보만 정리하고 ambiguous한 경우는 로그만 남김

5. 로그아웃/401 hard shutdown 정책 확정
- 스트림/UGV/이벤트 자원 정리 순서 명확화

6. UGV disconnect handshake 정책 정리
- 정상 경로는 `request.disconn.ugv` 전송 후 `5000ms` timeout 대기
- timeout/즉시 error/로그아웃/401/종료 시에는 강제 shutdown fallback 허용
- `error`가 와도 disconnect 중이면 `Error` 고착보다 정리 완료를 우선

7. 테스트용 UGV 주입 코드 정리
- 기본 제거 또는 개발자 옵션으로 격리

완료 기준:
- 운영 리스크 항목(P0) 재현/회귀 체크 통과
- ACK/종료/자원 정리 정책이 문서와 코드에서 일치

### 10-P1. 구조 정리

대상:
- `mainwindow.cpp`, `screens.h`, `main_screen.cpp`, `cctv_screen.cpp`
- `app_state.h`, `playback_screen.cpp`, `common_ui.h`, `common_ui.cpp`
- `ugv_service.cpp`, `VMS_Server/src/ugv-api/UgvController.cpp`, `VMS_Server/src/ugv-api/UgvService.cpp`

작업:
1. `screens.h` 분리
- `login/main/cctv/ugv/playback` 개별 헤더 생성
- `screens.h`는 단계적으로 umbrella header 역할만 유지

2. `MainWindow` 구현 분리
- `auth/runtime/navigation` 단위로 구현 파일을 분리
- public 시그니처와 화면 전환 동작은 유지

3. CCTV 타깃 해석 helper 공통화
- `mainwindow.cpp`/`cctv_screen.cpp` 중복 로직을 공통 helper로 정리
- `activeCctvChannelId` direct path 기준 + 동일 fallback 경로로 고정

4. `common_ui` 단계 분리 (구현 우선, 호출부 최소 변경)
- `P1-4-1` 채널/컨텍스트 + DnD helper 분리 (`channel_context_dnd_helpers.*`)
  - 이동: `DroppedChannelInfo`, channel id/display/rtsp helper, `resolveActiveCctvTarget`, `extractDroppedChannel*`
  - 원칙: `common_ui.h`는 umbrella 유지, 호출부 include 변경은 최소화
  - 주의: `resolveActiveCctvTarget`는 조회 전용이 아니라 state normalize 포함 helper임을 유지
- `P1-4-2` 상태/토스트 UI helper 분리 (`feedback_ui_helpers.*`)
  - 이동: `showActionStatus`, `showToastMessage`, `showPersistentStatusMessage`, stream/channel state helper
  - `applyNativeDarkTitleBar`는 후순위 유지(필요 시 window/platform helper로 별도 분리)
- `P1-4-3` 캡처/저장 경로 helper 분리 (`capture_storage_helpers.*`)
  - 이동: save directory, snapshot/clip 저장, encode failure 처리
- 각 단계마다 `build + 주요 화면 스모크` 확인 후 다음 단계 진행

5. UGV 진입 정책 변경
- 트리에서 UGV 제거
- UGV 상태 read-only 패널 추가
- 멀티뷰/DnD에서 UGV 타입 차단
- 이벤트 상세 `출동 시작`으로만 UGV 전체화면 진입
- 디버그/관리자용 direct path는 기본 비활성 상태로 두고, 별도 옵션은 현재 미구현(후속 검토)
- 상태(2026-03-17): 코드 기준 완료, UGV 서버 미기동으로 출동 성공-path E2E는 blocked

6. UGV error 계약 공통화 및 상태 정책 정리
- `10-P0` 잔여 low 2건 이월 항목 처리
- 서버 `error` JSON 생성 경로를 공통 helper화해서 스키마 드리프트 방지
- `SessionState` 전이는 `conn/disconn`, WS 종료, route unavailable, auth(401) 등 연결 축에서만 발생하도록 고정
- `cmd.drive/cmd.ptz` 즉시 실패/timeout은 기본적으로 명령 실패로만 처리하고 세션은 `ConnectedUgv` 유지
- 단, `UNAUTHORIZED`, `ROUTE_UNAVAILABLE`, `SOCKET_CLOSED` 등 연결 붕괴형 에러 코드는 예외적으로 세션 전이를 허용
- `cmd.drive/cmd.ptz` 실패 UX는 토스트 + UGV 상태 라벨(짧은 자동 해제) 기준으로 정리
- `disconnect` fallback, stale ACK 무시, `error(msgId/requestType)` pending 정리 규칙은 유지
- 상태(2026-03-17): 코드 기준 완료, UGV 서버 미기동으로 E2E는 blocked

7. `playback_screen.cpp` 분리 (3단계)
- `10-P1-7-1` 메인 TU 슬림화
  - `playback_screen.cpp`는 생성자/UI 구성 + 화면 lifecycle glue만 유지
  - `setPlaybackService()`, `showEvent()`, `resizeEvent()`, snackbar(`show/clear/place`) 포함
- `10-P1-7-2` 재생/타임라인 + export 구현 분리
  - `playback_screen_timeline.cpp`
    - `startPlaybackForChannel`, `loadTimelineForChannel`, `applyTimelineResult`
    - `requestPlaybackStream`, `applyPlaybackCapabilities`, `refreshTimelineUi`, `rebuildEventMarkers`
    - 시간/슬라이더/마커 계산 등 timeline 전용 helper 이동
  - `playback_screen_export.cpp`
    - `isExportBusy`, `cancelExportOperations`
    - `openExportDialog`, `pollExportStatus`, `startExportDownload`, `updateExportUiState`
    - export 전용 helper 이동
  - `playbackRates`, `rateText`처럼 공용 성격 helper는 사용 위치 기준으로 유지하거나 공용 위치로 분리
- `10-P1-7-3` 통합 마감
  - `CMakeLists.txt` 반영
  - 기존 인터페이스/시그니처 유지
  - `build + Playback 수동 스모크` 통과
- 완료 기준:
  - 기능/정책 변경 없이 `build + Playback 수동 스모크` 통과
  - 채널 선택/타임라인 이동/재생/배속/export/poll/download/취소 회귀 없음
- 상태(2026-03-17): 코드 분리/빌드 반영 완료, Playback 수동 스모크 기준으로 마감

8. `AppState` 병렬 배열 개선
- `10-P1-8-1` `AppState` 셀 구조체 도입
  - `displayName + channelId + deviceId`를 한 단위로 묶은 셀 상태 구조체 추가
  - 기존 `cellChannels/cellChannelIds/cellDeviceIds` 병렬 배열 불일치 리스크 제거가 목표
  - 정책/UX 변경 없이 데이터 구조만 정리
- `10-P1-8-2` 셀 조작 공통 helper 도입
  - `setGridCell(index, ...)`, `clearGridCell(index)`, `clearAllGridCells()` 같은 공통 함수로 쓰기 경로 통일
  - 다중 파일에서 개별 배열을 직접 만지는 패턴 제거
- `10-P1-8-3` 단계적 마이그레이션
  - 1차: `mainwindow.cpp`, `mainwindow_auth.cpp`, `main_screen.cpp`의 읽기/쓰기를 구조체 기반으로 전환
  - 2차: 병렬 배열 필드 제거 및 잔여 참조 정리
  - 주의: `activeChannel`/`activeCctvChannelId`/`activeUgv*` 브리지는 이번 단계에서 유지
- 완료 기준:
  - `build + Main/CCTV/UGV/Playback` 기본 전환 스모크 통과
  - 로그인 초기 배치, DnD 배치/해제, 멀티뷰 셀 더블클릭 진입 회귀 없음
  - 상태 동기화 코드에서 병렬 배열 직접 갱신 경로 제거
- 상태(2026-03-17): `gridCells` 전환 및 병렬 배열 제거 완료, 빌드/스모크 기준 마감

9. 프로젝트명/타이틀 v2 전환
- 윈도우 타이틀, 문구, 빌드 산출물 표기 정리
- 상태(2026-03-17): `mainwindow` 타이틀과 `CMake` 타깃/산출물 표기를 `VMS_v2` 기준으로 전환 완료
  - `QSettings` 키(`TeamClue/VMS_v1`)와 `vms.*.v1` 프로토콜 문자열은 운영 호환성 때문에 유지

완료 기준:
- 핵심 대형 파일 복잡도/변경 영향도 감소
- 기능 회귀 없이 빌드/수동 테스트 통과

---

## Phase 11. UI/UX 디자인 스프린트

목표:
- 현재 기능을 유지하면서 시각 품질/조작 일관성을 개선

대상:
- `styles/v2_theme.qss`
- 화면 파일(`login_screen`, `main_screen`, `cctv_screen`, `ugv_screen`, `playback_screen`)
- 공통 위젯(`common_widgets.*`)

작업:
1. 디자인 토큰 정의
- spacing/radius/panel/button/status 규칙

2. 프로토타입 기반 반영
- HTML/Figma 시안을 레퍼런스로 사용
- 런타임은 Qt/QSS/공통 위젯으로 구현

3. 화면별 적용 순서
- Login/Signup -> Main -> CCTV/UGV -> Playback

4. Playback UI/동작 분리 복구
- Playback는 디자인 보강 전에 동작 안정화를 먼저 수행
- 우선순위:
  - `11-PB-1` 안정 복구
    - `playback_screen`의 재생/seek/이벤트 마커 클릭/자동 재생 경로를 먼저 정상화
    - `sliderReleased -> requestPlaybackStream(...)` 구조는 유지
    - `24시간 트랙`, `재생 가능 구간`, `현재 재생 위치`, `wallclock fallback`이 한 UI 계산 경로에 섞여 있는 상태를 단순화
    - `m_markerOverlay`의 마우스 간섭을 최소화해서 seek/클릭 동작을 흔들지 않도록 정리
    - 목표: 검은 화면/응답 없음/타임라인 오동작 없이 Playback 기본 기능 복구
  - `11-PB-2` 타임라인 책임 분리
    - 내부 상태를 `전체 날짜 범위(0~24h)`, `재생 가능 구간`, `현재 재생 절대 시각` 축으로 분리
    - 좌표 변환/표시 계산 helper를 정리해 seek 로직과 시각 표현 로직의 결합을 낮춤
  - `11-PB-3` 표현 재적용
    - `24시간 회색 트랙`
    - `재생 가능 구간` 오렌지 오버레이
    - 현재 위치 핸들
    - 얇은 이벤트 마커
    - 위 순서대로 하나씩 적용하고 단계별 스모크 확인
- 원칙:
  - 표현 변경이 seek/replay/reload 동작을 흔들지 않도록 Playback는 동작과 표현을 분리해서 진행
  - seek 재요청은 서버 timestamp 기반 playback 구조에 맞는 핵심 동작으로 유지

5. UGV 진입 정책 UX 반영
- Phase 10에서 정책이 확정/구현된 뒤, read-only 상태 패널/출동 전용 진입 UX를 시각적으로 정리

완료 기준:
- 화면 간 스타일/상태 피드백 규칙 일관성 확보
- 인라인 스타일 의존 추가 없이 적용 완료
- Playback는 `build + 수동 스모크` 기준으로 재생/seek/이벤트 마커 클릭/자동 재생이 안정 상태를 회복한 뒤 시각 보강을 적용
- `11-PB-1` 스모크 기준:
  - 트리 클릭 재생 정상
  - 이벤트 마커 클릭 재생 정상
  - 슬라이더 seek 재생 정상
  - 응답없음/검은 화면 고착 없음

---

## Verification Track (환경 준비 시 실행)

### V-1. Playback export E2E (`7-B-4`)

선행 조건:
- 서버 `/playback/export*` 최신 코드 배포

검증:
- `POST /playback/export` -> `GET /playback/export/{jobId}` -> 다운로드 저장
- 실패/timeout/invalid uri/취소 경로 UX 확인

종료 기준:
- 문서상 Blocked 해제

### V-2. UGV success-path E2E (`8-B blocked`)

선행 조건:
- gateway `/ugv/ws` 준비

검증:
- `request.conn.ugv.ack`
- `telemetry.gps/rssi` 수신 + UI 반영
- `cmd.drive/cmd.ptz` ack
- `request.disconn.ugv.ack`

종료 기준:
- 문서상 Blocked 해제

---

## 최종 종료 조건

- Phase 9~11 완료
- Verification Track V-1/V-2 Blocked 해제
- 핵심 게이트:
  - 성능 체감 개선(멀티뷰/전환/클립 저장)
  - 운영 정합성(ACK/종료/자원 정리)
  - UI/UX 일관성 확보

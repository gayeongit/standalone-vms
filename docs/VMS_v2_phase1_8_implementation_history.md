# VMS v2 Phase 1~8 구현 이력

## 문서 목적

이 문서는 [`VMS_v2_dev_execution_plan.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/VMS_v2_dev_execution_plan.md)의 계획 항목을 실제 구현 기준으로 다시 정리한 구현 이력 문서이다.

계획 문서는 "무엇을 할 것인가"를 기준으로 관리하고, 이 문서는 아래를 남기는 것을 목적으로 한다.

- 실제로 어떤 구조를 도입했는가
- 구현 과정에서 어떤 문제가 발생했는가
- 어떤 파일을 추가/수정/삭제했는가
- 어떤 설계 판단을 했는가
- 무엇을 Known issue 또는 후속 Phase로 넘겼는가

즉, 이 문서는 `Phase 1~8`의 개발 로그이자, 이후 작업의 회귀 방지용 참조 문서이다.

---

## 전체 요약

Phase 1~8에서 실제로 정리된 핵심은 아래와 같다.

1. 스트리밍 렌더 경로를 `appsink -> QImage -> VideoRenderWidget(QOpenGLWidget)` 구조로 교체했다.
2. `native_capture(BitBlt)` 경로를 제거하고, 캡처 소스를 화면 합성과 최신 프레임 기반으로 재구성했다.
3. 멀티뷰에서 발생하던 OOM/프리징 문제를 `pending dispatch guard`와 `4/6/9` 품질 프로파일 정책으로 1차 완화했다.
4. 인증 흐름을 더미 emit 구조에서 `RestClient/AuthService/MainWindow/AppState` 기반 구조로 교체했다.
5. `DeviceCheck`를 서버 장치/채널 트리 기반으로 전환하고, 선택 단위를 문자열에서 `SelectedChannelContext` 기반으로 전환했다.
6. `Topbar`, 알림센터 셸, 알림센터 전용 모달, 공통 상태 슬롯을 도입했다.
7. `v2_theme.qss`를 추가하고 `Topbar/알림센터/상태 라벨/OSD` 일부를 QSS 기반으로 정리했다.
8. `Main` 이벤트뷰 토글을 단순 show/hide에서 상태 기반 레이아웃 제어로 바꾸고, `CCTV`의 우측 이벤트뷰를 제거해 정책을 정리했다.
9. `Phase 3.5`에서 `SettingsDialog`, config/theme loader, 런타임 화면 재생성 경로, 이벤트/알림 유틸을 분리해 `mainwindow.cpp`와 `common_ui.cpp`를 줄였다.
10. `Phase 4`에서 `WsClient`와 `EventService`를 도입하고, 이벤트 UI 데이터 소스를 `DummyData::events()`에서 실제 서비스 캐시 기반으로 전환했다.
11. `Phase 5`에서 `CctvControlService`를 추가하고, `CctvScreen`의 Zoom/Focus UI를 실제 REST 제어 경로로 연결했다.
12. `Phase 6`에서 Playback을 `/playback/*` API 기반으로 전환하고, `timeline -> stream -> export` 흐름과 서버 playback 계약을 맞췄다.
13. `Phase 7`에서 클립 캡처 상태/실패/취소 정책을 정리하고, Playback export를 polling/direct download 운영 기준으로 다듬었다.
14. `Phase 8`에서 UGV를 `/gw/ws` 기반 `UgvService + UgvScreen` 구조로 전환하고, direct path/ACK 검증/맵/종료 정책까지 정리했다.

이 문서의 범위는 `Phase 1`, `Phase 2`, `Phase 3`, `Phase 3.5`, `Phase 4`, `Phase 4.5`, `Phase 5`, `Phase 6`, `Phase 7`, `Phase 8`까지이다.

---

## 검증 범위

이 문서의 구현/문제 해결 내용은 아래 검증 기준을 전제로 한다.

- 빌드/런타임 검증은 로컬 수동 테스트 기준
- 자동 테스트(단위/통합/E2E) 기반 정량 검증은 포함하지 않음
- 성능/안정화 평가는 정량 벤치마크보다 체감 안정화(프리징/강종/응답성) 기준
- `Phase 4`는 코드/로컬 동작 기준 검증까지 완료했고, WS 이벤트 실데이터 E2E 검증은 서버 구현 완료 시점에 추가 확인 필요
- `Phase 8`은 클라이언트 구현과 `/gw/ws` 실패 경로 확인까지 완료했고, 실제 `gateway /ugv/ws` 성공 경로 E2E는 환경 준비 전까지 `Blocked`

즉, 이 문서는 "개발/수정 이력 + 수동 검증 결과"를 중심으로 기록한다.

---

## Phase 1. 스트리밍 / 캡처 / 멀티뷰 안정화

### 1-1. 목표

Phase 1의 목표는 아래 세 가지였다.

- 스트리밍 렌더 경로를 v2 구조로 교체
- BitBlt 기반 캡처 제거
- 메인 멀티뷰에서 최소한 "사용 가능한 수준"까지 성능/안정성 확보

이 단계는 기능 확장보다도 "기반 경로 교체"와 "치명적 병목 제거"가 핵심이었다.

---

### 1-2. 초기 상태

초기 v1 계열 구현은 아래 특성을 갖고 있었다.

- 스트리밍은 위젯 바인딩 구조가 단일 대상 중심이었다.
- 캡처/스냅샷/클립은 `native_capture`의 Windows BitBlt 경로에 의존했다.
- 메인 멀티뷰는 채널 수가 많아질수록 렌더 적체와 메모리 사용량이 급증했다.
- `QOpenGLWidget`를 쓰더라도 실제 paint 경로는 CPU `QPainter::drawImage()` 중심이었다.
- 멀티뷰 8채널 이상에서 OOM/프리즈에 가까운 상태가 재현됐다.

즉, Phase 1은 단순 기능 개발보다 "v2 구조로 갈 수 있는 최소 기반"을 만드는 단계였다.

---

### 1-3. 구현 내용

#### 1-3-1. `appsink -> QImage -> VideoRenderWidget` 경로 도입

기존 스트리밍 경로를 정리하고, GStreamer `appsink`에서 프레임을 읽어 `QImage`로 변환한 뒤 렌더 위젯에 전달하는 구조로 바꿨다.

핵심 변경:

- `StreamPlayer`가 `appsink` 기반으로 동작
- `GstSample`에서 프레임을 읽어 `QImage`로 변환
- `VideoRenderWidget(QOpenGLWidget)` 추가
- 정지/재바인딩 시 프레임 clear 처리

관련 파일:

- 수정: [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 신규: [`video_render_widget.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)
- 신규: [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

#### 1-3-2. 멀티 위젯 fan-out 구조

초기 구현은 마지막 바인딩 위젯 하나만 기억하는 구조였기 때문에, 멀티셀/전체화면 전환에서 동일 채널을 여러 위젯에 안정적으로 뿌릴 수 없었다.

이를 아래 구조로 바꿨다.

- `StreamPlayer`에 다중 렌더 타깃 바인딩
- `bindRenderWidget(QWidget*)`
- `unbindRenderWidget(QWidget*)`
- `clearRenderWidgets()`
- 프레임 publish 시 전체 타깃 fan-out

추가로, 위젯별 deep copy 비용을 줄이기 위해 `QSharedPointer<const QImage>` 기반 공유 전달 구조로 바꿨다.

관련 파일:

- 수정: [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 수정: [`channel_session_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.h)
- 수정: [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- 수정: [`video_render_widget.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)
- 수정: [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)

#### 1-3-3. host/render owner 충돌 방어

멀티 바인딩 구조로 넘어가면서 같은 host에 다른 `StreamPlayer`가 잘못 붙거나, 이전 render widget이 남아 있는 문제가 생길 수 있었다.

이를 방지하기 위해 아래를 도입했다.

- host property: `_vms_host_owner`
- render widget property: `_vms_render_owner`
- 다른 세션이 점유 중인 경우 바인딩 거부
- `errorOccurred(...)`로 원인 전달
- 소멸자 및 정리 경로에서 owner/property 해제

관련 파일:

- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)

#### 1-3-4. `native_capture` 제거

초기 캡처 구조는 `native_capture(BitBlt)` 기반이었고, 이는 v2 설계와 맞지 않을 뿐 아니라 화면/OS 별 의존성이 컸다.

이를 아래 두 갈래 구조로 교체했다.

- `Main` 멀티뷰: `QWidget::grab()` 기반 합성 캡처
- `CCTV/UGV/Playback`: 최신 프레임 기반 캡처

결과적으로 `native_capture`는 완전히 제거했다.

관련 파일:

- 삭제: [`native_capture.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/native_capture.h)
- 삭제: [`native_capture.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/native_capture.cpp)
- 수정: [`clip_capture_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)
- 수정: [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

#### 1-3-5. 캡처 안정성/저장 경로 정리

실사용 테스트 중 아래 문제가 발생했다.

- 저장 경로가 v1 테스트 시 쓰던 바탕화면 경로를 계속 따라감
- 클립 저장 중 렉이 심함
- `libpng Invalid IHDR` / `QImage::save failed` 오류가 간헐 발생

대응:

- 기본 경로를 `Pictures/snapshot`, `Videos/videoclip`로 분리
- 경로 자동 생성
- `QSettings("TeamClue", "VMS_v2")`에 경로 저장
- v1 `paths/saveDir`에서 1회 마이그레이션
- 프레임 유효성 검증 추가
- 불량 프레임 skip
- 인코딩 중 재진입 방지

관련 파일:

- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`clip_capture_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)
- 수정: [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

#### 1-3-6. 멀티뷰 렉/OOM 대응 1차

실제 테스트 중 `8채널` 이상에서 다음 증상이 발생했다.

- 메인 진입 직후 입력 불가 수준의 렉
- 프레임 dispatch 적체
- 메모리 급증
- OOM/프리즈

1차 대응:

- 위젯당 queued render dispatch 1개만 허용
- `pending dispatch guard` 도입
- `4/6/9` 레이아웃별 품질 프로파일 도입
  - `4 View`: 품질 유지
  - `6 View`: `854x480 @ 10fps`
  - `9 View`: `480x270 @ 6fps`
- 레이아웃 변경 시 프로파일이 재전달되도록 수정
- stale host profile 정리 오류 수정
- 같은 host/profile 재호출 최적화

관련 파일:

- 수정: [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- 수정: [`channel_session_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.h)
- 수정: [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)

트러블슈팅 문서:

- [`docs/troubleshooting/troubleshooting_multiview_streaming.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/troubleshooting/troubleshooting_multiview_streaming.md)

---

### 1-4. 구현 중 실제 문제와 해결

#### 문제 1. `gst_app_sink_set_callbacks(..., nullptr, ...)` CRITICAL

원인:

- destroy 시 callbacks를 `nullptr`로 해제하려 했으나, GStreamer AppSink는 `callbacks != NULL` 전제를 가짐

해결:

- 빈 `GstAppSinkCallbacks` 구조체를 넘기도록 수정

#### 문제 2. 멀티 위젯 fan-out에서 프레임 deep copy 과다

원인:

- 위젯 수만큼 `QImage` deep copy가 반복

해결:

- `QSharedPointer<const QImage>` 공유 전달로 변경

#### 문제 3. 인코딩 중 재진입으로 버퍼 변형

원인:

- `stopAndEncode()` 중 `stop()` 가드 충돌로 타이머가 계속 돌고 있었음

해결:

- 인코딩 진입 전에 타이머를 직접 stop
- `m_isEncoding` 가드 구조 재정리

#### 문제 4. 메인 멀티뷰 렉

원인:

- deep copy + UI thread paint + 8채널 동시 고품질 렌더

해결:

- pending dispatch guard
- 4/6/9 품질 프로파일

남은 한계:

- `frame.copy()`
- `QPainter::drawImage()` 기반 CPU paint

즉, Phase 1에서는 병목을 제거한 것이 아니라 1차로 억제한 상태이다.

---

### 1-5. Phase 1 종료 시점 Known issue / 후속 과제

- [P1] 멀티뷰는 이전보다 안정화됐지만, 여전히 근본 병목은 `QImage deep copy + CPU paint`에 남아 있음
- [P2] `VideoRenderWidget` GPU 경로 재작성 여부는 후속 검토
- [P2] 멀티뷰 OSD 갱신 비용은 완전히 제거되지 않음
- [P1] 캡처/Export의 완전 비동기화는 Phase 7로 이관

---

## Phase 2. 인증 / 장치조회 / DeviceCheck 서버 연동

### 2-1. 목표

Phase 2의 목표는 아래와 같았다.

- 더미 인증 흐름 제거
- 서버 로그인/회원가입 구조 도입
- 서버 장치/채널 목록 기반 `DeviceCheck` 전환
- 선택 결과를 서버 컨텍스트 기반으로 바꾸기
- Main 진입 시 채널별 RTSP 확보 구조 도입

즉, `QStringList` 중심의 v1 임시 흐름을 `서버 식별자 + 컨텍스트` 구조로 바꾸는 단계였다.

---

### 2-2. 구현 내용

#### 2-2-1. 인증 인프라 도입

신규 서비스 계층:

- [`rest_client.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/rest_client.h)
- [`rest_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/rest_client.cpp)
- [`auth_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/auth_service.h)
- [`auth_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/auth_service.cpp)

핵심:

- base URL / timeout / bearer token 주입
- `requestTag`
- `suppressUnauthorized`
- `unauthorizedDetected(QString requestTag)` signal
- 로그인/회원가입 요청 body 구성
- token/userId/error parsing

#### 2-2-2. 외부 설정 파일 도입

하드코딩을 줄이기 위해:

- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

도입 내용:

- `apiBaseUrl`
- `auth.loginPath`
- `auth.signupPath`
- 이후 `device.devicesPath`, `device.deviceChannelsPath`, `device.channelDetailPath`까지 확장

또한:

- 빌드 후 실행 파일 폴더로 복사
- install 규칙 포함

관련 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

#### 2-2-3. 로그인/회원가입 화면을 서비스 호출 구조로 전환

기존:

- 버튼 클릭 -> 더미 성공 emit

변경:

- `LoginScreen::loginRequested(username, password)`
- `SignupScreen::signupRequested(name, username, password)`
- `MainWindow -> AuthService -> RestClient`

또한:

- 진행중 상태 표시
- 오류 라벨 표시
- 입력값 reset/clear 메서드 추가

관련 파일:

- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`login_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp)
- 수정: [`mainwindow.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.h)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)

#### 2-2-4. 인증 상태 정리 정책

초기에는 `QSettings("TeamClue", "VMS_v2")` 기준의 토큰 저장/복구와 시작 시 probe 흐름까지 검토했다.

하지만 실제 구현과 테스트를 거치면서 인증 정책은 아래처럼 단순화됐다.

- 로그인 성공 시 메모리의 인증 상태(`AppState.isAuthenticated`, `AppState.accessToken`, `AppState.currentUserId`)를 우선 사용
- `RestClient`에도 현재 세션의 bearer token만 메모리 기준으로 주입
- 로그아웃 또는 `401` 발생 시 메모리 인증 상태를 즉시 정리
- 자동 로그인/영구 토큰 복구는 최종 정책으로 채택하지 않음

즉, Phase 2 종료 시점의 최종 상태는 `QSettings` 영구 토큰 저장/복구 중심이 아니라, 메모리 세션 중심 구조에 가깝다.

#### 2-2-5. `/devices` 서비스 도입

신규 파일:

- [`device_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/device_service.h)
- [`device_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/device_service.cpp)

도입 내용:

- `fetchDevices()`
- 이후 확장:
  - `fetchDeviceChannels(deviceId, ...)`
  - `fetchChannelDetail(channelId, ...)`

응답 모델:

- `DeviceSummary`
- `DeviceChannelSummary`
- `DeviceServiceResult`
- `DeviceChannelsResult`
- `ChannelDetailResult`

파싱 특징:

- `{"ok":true,"data":[...]}`
- `{"data":[...]}`
- `{"devices":[...]}`
- `{"items":[...]}`
- 단일 객체 fallback

#### 2-2-6. DeviceCheck를 서버 장치/채널 트리로 전환

기존:

- `DummyData::devices()` 기반 체크박스 목록
- 선택 결과 = `QStringList`

변경:

- `type -> model -> name` 트리
- `/devices` 후 각 장치별 `/device/{deviceId}/channels`
- leaf에 `deviceId`, `channelId`, `channelNo`, `displayName`, `deviceType`, `model`, `online`, `health` 저장
- 선택 결과 = `QVector<SelectedChannelContext>`

관련 파일:

- 신규: [`selected_channel_context.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/selected_channel_context.h)
- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`login_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp)

#### 2-2-7. Main 진입 전 RTSP 확보 단계

`VMS 시작` 시 바로 `displayName`만 들고 Main에 들어가던 구조는 이후 스트리밍 연계에 한계가 있었다.

Phase 2 후반에 아래로 바꿨다.

- 선택 채널들의 `channelId`로 `/channel/{channelId}` 조회
- RTSP 확보 성공 채널만 런타임 상태에 반영
- 결과는 `AppState.channelRtspByName`에 저장
- 이후 `rtspUrlForChannel(name)`가 메모리 맵을 우선 사용

관련 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)

#### 2-2-8. 문자열 상태에서 컨텍스트 원본 저장으로 전환

기존 상태:

- `selectedDevices`
- `cellChannels`
- `activeChannel`

변경:

- `AppState.selectedChannelContexts`
- `common_ui`, `common_widgets`, `Main/CCTV/UGV/Playback`가 점진적으로 `selectedChannelContexts`를 기준으로 동작
- 기존 `cellChannels/activeChannel`은 호환층으로만 유지

또한:

- `normalizeSelectedContextsForRuntime()` 도입
- displayName 중복 시 고유 라벨 생성

관련 파일:

- 수정: [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)

---

### 2-3. 구현 중 실제 문제와 해결

#### 문제 1. 로그인 body key 불일치

초기에는 아래 키로 요청을 보냈다.

- 로그인: `username`, `password`
- 회원가입: `name`, `username`, `password`

실제 서버 스펙은 아래였다.

- `name`, `id`, `pw`

해결:

- [`auth_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/auth_service.cpp)에서 요청 body key 수정

#### 문제 2. `app_config.json` 포트 설정 실수

포트 `8080` 적용 과정에서 `apiBaseUrl`이 아니라 `requestTimeoutMs`를 `8080`으로 바꾸는 실수가 있었다.

해결:

- `apiBaseUrl`에 `http://192.168.0.51:8080`
- timeout은 별도 필드로 유지

#### 문제 3. `/devices`가 로그인 전에 호출되어 401

초기엔 `DeviceService` 주입 직후 자동 조회를 했기 때문에 토큰 없이 `/devices`를 호출했다.

해결:

- `DeviceCheckScreen::setDeviceService()`에서 자동 조회 제거
- 로그인 성공 후 `setAccessToken()` 다음에 `refreshDevices()` 호출

#### 문제 4. DeviceCheck 재조회 stale callback

원인:

- `/devices -> /channels` fan-out 중 이전 요청 콜백이 늦게 도착해 최신 트리를 덮을 수 있었음

해결:

- `m_reloadGeneration` 기반 세대 번호 도입
- 이전 generation 콜백 무시

#### 문제 5. RTSP 조회 실패 채널이 선택 컨텍스트에 남음

원인:

- `/channel/{channelId}` 실패 채널도 `selectedChannelContexts`에 남아 UI가 선택된 것처럼 보였음

해결:

- 성공한 채널만 `resolvedContexts`로 추려 최종 상태 저장

---

### 2-4. Phase 2 종료 시점 Known issue / 후속 과제

- [P1] `rtspUrlForChannel()`는 아직 이름 기반 브리지 성격이 남아 있음
- [P1] `cellChannels/activeChannel` 호환층이 아직 존재
- [P1] `channelId`는 이후 Playback/API에서 더 적극적으로 써야 함
- [P2] UGV 테스트를 위해 `UGV Test 1` 임시 컨텍스트 추가가 들어가 있음

---

## Phase 3. 공통 이벤트 UX / 알림센터 / 메인 레이아웃 / QSS 정리

### 3-1. 목표

Phase 3의 목표는 아래와 같았다.

- 공통 `Topbar` / 알림센터 진입점 도입
- 알림센터 전용 모달 구현
- Main 이벤트뷰 토글 안정화
- 공통/화면별 인라인 스타일을 `v2_theme.qss`로 이동

즉, `Phase 3`는 기능보다도 공통 UX 골격과 스타일 토대를 만드는 단계였다.

---

### 3-2. 구현 내용

#### 3-2-1. `TopbarWidget` 확장

도입 내용:

- 알림 버튼
- unread 배지
- 공통 상태 슬롯
- `notificationCenterClicked()`
- `setNotificationUnread(bool)`
- `setGlobalStatusMessage(...)`
- `clearGlobalStatusMessage()`

관련 파일:

- 수정: [`common_widgets.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.h)
- 수정: [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)

#### 3-2-2. 알림센터 셸과 전용 모달

`3-A-1`에서는 셸과 진입점만 만들고, `3-A-2`에서 아래를 구현했다.

- 알림센터 전용 모달
- 필터: `최근 12시간 / 1일 / 3일`
- 최신순 목록
- event detail 연결
- 검색 모달과 분리

관련 파일:

- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 3-2-3. 사이드바 로컬 액션 상태 라벨

기존에는 스냅샷/클립/내보내기 피드백을 오버레이 토스트로 띄우던 방향이 있었으나, 최종적으로는 사이드바 내부 상태 슬롯으로 정리했다.

구조:

- `SidebarWidget` 내부에 `actionStatusLabel`
- `showActionStatus(...)`
- `clearActionStatus()`

이 방식은 화면 액션과 문맥이 맞고, 이후 Topbar 전역 상태와도 역할을 분리하기 쉬운 구조이다.

관련 파일:

- 수정: [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- 수정: [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 3-2-4. Main 이벤트뷰 토글 안정화

초기 `Main` 이벤트뷰 토글은 `show()/hide()` 중심이었고, unread 정책과 레이아웃 collapse가 산발적으로 얽혀 있었다.

이를 아래 구조로 정리했다.

- `eventViewContainer`
- 상태 기반 `setEventViewVisible(bool)` 역할의 함수
- 토글 버튼 텍스트/레이아웃 폭/ unread clear를 한 곳에서 처리
- 숨김 상태에서만 unread 증가

관련 파일:

- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)

#### 3-2-5. `CCTV` 정책 정리

`CCTV`는 우측 이벤트뷰를 제거하고, 알림센터 모달만 사용하는 정책으로 정리했다.

이 변경으로:

- `CCTV` 본문 레이아웃 단순화
- `Main`만 이벤트뷰 토글 가능
- `UGV/Playback`은 이벤트뷰 없음 + 알림센터 모달만 사용

관련 파일:

- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)

#### 3-2-6. `v2_theme.qss` 도입

신규 파일:

- [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)

적용 방식:

- 기존 `v1_theme.qss` 위에 merge 로드
- `loadMergedThemeFromRelativePaths()` 추가

초기 정리 대상:

- `topbarGlobalStatus`
- `deviceStatusLabel[state=...]`
- `sidebarActionStatus[state=...]`
- `notificationCenterDialog`
- `notificationCenterTitle`
- `notificationCenterSummary`
- `notificationCenterList`

관련 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- 신규: [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)

#### 3-2-7. 화면별 OSD / 인라인 스타일 QSS 전환

`3-A-3-2`에서는 아래를 objectName/property 기반으로 바꿨다.

- `Main` 멀티뷰 셀
  - 선택 테두리
  - 채널명
  - 상태 점
  - 닫기 버튼
- `CCTV`
  - timestamp / connection / channel OSD
- `UGV`
  - viewport
  - timestamp / RSSI / channel overlay
  - pan/tilt pad
  - drive box / drive buttons
  - `missionEndButton`

관련 파일:

- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)

---

### 3-3. 구현 중 실제 문제와 해결

#### 문제 1. `Qt::UniqueConnection` + lambda runtime assert

원인:

- `Qt::UniqueConnection`은 멤버 함수 포인터 슬롯에만 안전하게 쓸 수 있는데, 람다 연결에도 붙였다가 runtime assert 발생

해결:

- 멤버 함수 직접 연결만 `Qt::UniqueConnection`
- 람다 연결은 일반 `connect(...)`

관련 파일:

- 수정: [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 문제 2. `v2_theme.qss` 로더 미적용/재귀 위험

원인:

- `loadMergedThemeFromRelativePaths()` fallback 재귀
- `setupUi()`가 여전히 구 로더를 호출

해결:

- fallback을 `loadThemeFromRelativePaths()`로 수정
- `setupUi()`가 `loadMergedThemeFromRelativePaths()` 호출하도록 수정

#### 문제 3. Main 이벤트뷰 hidden 상태 unread 점등 불안정

원인:

- 더미 이벤트 기반 unread 로직이 실제 이벤트 lifecycle과 다르게 임시 구현되어 있었음

해결:

- 완전 해결 대신 `Known issue`로 문서화
- 최종 unread lifecycle은 `Phase 4 EventService` 연동 시 재정의

#### 문제 4. `CCTV`의 UGV 출동 UX 개념 혼선

과정 중 확인된 점:

- `UGV 화면 진입`
- `UGV 출동`

은 같은 행위가 아니며, 기존 `EventView` 중심 UX는 이후 도메인 정책 재정리가 필요했다.

판단:

- 기한상 즉시 구조 변경은 하지 않고
- `Phase 8` UGV 정책 정리 항목으로 보류

---

### 3-4. Phase 3 종료 시점 Known issue / 후속 과제

- [P1] `Main` 이벤트뷰 hidden 상태 unread 배지 자동 점등은 더미 이벤트 기준으로 불안정
- [P1] `applyNotificationUnreadState()`는 `3-A-2` 임시 정책으로 `unreadCount`를 표시하지 않고 배지만 토글
- [P2] `UGV 화면 진입`과 `UGV 출동` 개념 분리는 `Phase 8`에서 정책 확정 필요
- [P2] `common_ui.cpp`는 기능이 커지고 있어 이후 선택적 분리 후보

---

## Phase 3.5. Phase 4 진입 전 최소 구조 정리

### 3.5-1. 목표

Phase 3.5의 목표는 기능 추가가 아니라 아래 네 가지 최소 구조 정리였다.

- `openSettingsDialog()` 대형 UI 블록 분리
- config / theme loader 책임 분리
- 런타임 화면 재생성 경로 단일화
- 이벤트/알림 UI 유틸을 `common_ui.cpp`에서 분리 시작

중요한 전제는 "대수술 금지"였다. `AppState` 재설계, `StreamPlayer` 구조 변경, 도메인 재설계는 이 단계에서 하지 않았다.

---

### 3.5-2. 구현 내용

#### 3.5-2-1. `SettingsDialog` 분리

기존 [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp) 안에는 설정 다이얼로그 UI와 장치 CRUD, 저장 경로/보존 기간 탭 구성이 한 함수에 길게 들어가 있었다.

이를 아래 파일로 분리했다.

- 신규: [`settings_dialog.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.h)
- 신규: [`settings_dialog.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.cpp)

결과적으로:

- `MainWindow::openSettingsDialog()`는 얇은 래퍼가 되었고
- 설정 UI/버튼/탭 로직은 `SettingsDialog` 내부로 이동했다
- 장치 CRUD와 저장 경로/정책 변경 후 결과 반영은 기존 `MainWindow` 경로와 호환되도록 유지했다

#### 3.5-2-2. `AppConfig` / theme loader 분리

`mainwindow.cpp` 안에 있던 설정 파일 파싱과 QSS 병합 로더를 별도 파일로 옮겼다.

- 신규: [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- 신규: [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- 신규: [`theme_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/theme_loader.h)
- 신규: [`theme_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/theme_loader.cpp)

이로 인해 `MainWindow`는:

- 설정 파일 구조를 "정의"하는 책임
- 상대 경로 QSS를 병합 로드하는 책임

을 직접 갖지 않게 됐다.

#### 3.5-2-3. 런타임 화면 재생성 경로 통합

초기 생성, `DeviceCheck` 이후 진입, 설정 변경 후 재생성에서 `Main/CCTV/UGV/Playback` 생성/삽입/연결 코드가 중복되어 있었다.

이를 아래 두 함수로 정리했다.

- [`destroyRuntimeScreens()`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- [`createRuntimeScreens(...)`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

최종적으로:

- 초기 구동
- 장치 선택 후 Main 진입
- 설정 변경 후 재생성

모두 같은 생성 경로를 사용하게 됐다.

#### 3.5-2-4. 이벤트/알림 유틸 분리 시작

`common_ui.cpp` 안에 섞여 있던 이벤트 검색/상세/알림센터 관련 구현을 별도 파일로 분리 시작했다.

- 신규: [`event_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.h)
- 신규: [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)

이 단계에서는 호환성을 위해 `common_ui` 쪽 forwarding wrapper를 유지했고, 실제 구현은 `event_ui_helpers`로 위임하도록 정리했다.

---

### 3.5-3. 구현 중 실제 문제와 해결

#### 문제 1. `SettingsDialog` 분리 직후 버튼 클릭 크래시

원인:

- 생성자 내부 로컬 변수를 `&` 참조 캡처한 람다가 설정창 생명주기보다 오래 살아 있었다

해결:

- 공유 상태는 `std::shared_ptr`로 올리고
- 위젯 포인터와 값은 값 캡처로 바꿨다

관련 파일:

- 수정: [`settings_dialog.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.cpp)

#### 문제 2. 런타임 화면 signal 중복 연결

원인:

- `createRuntimeScreens()`에서 이미 connect 했는데, 초기 구동 경로에서 한 번 더 `connectRuntimeScreens()`가 호출되었다

해결:

- 초기 구동 시 중복 연결 1줄 제거
- 생성/삽입/연결 경로를 `createRuntimeScreens()`로 일원화

관련 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

---

### 3.5-4. Phase 3.5 종료 시점 Known issue / 후속 과제

- [P2] `common_ui.h/.cpp` forwarding wrapper는 호환성 유지용으로 남아 있으며, `Phase 4`에서 직접 `EventUiHelpers` 호출로 점진 전환 예정
- [P3] 윈도우 타이틀/프로젝트명에 `v1` 흔적이 일부 남아 있으나 기능 우선순위는 낮음

---

## Phase 4. WS 인프라 / EventService / 이벤트 UI 데이터 소스 전환

### 4-1. 목표

Phase 4의 목표는 아래와 같았다.

- `WsClient`를 transport-only 인프라로 도입
- `EventService`가 이벤트 캐시/unread/REST fallback을 소유
- 이벤트 UI 경로에서 `DummyData::events()` 제거
- 로그아웃/401/재연결 시 이벤트 상태 정리와 복구를 일관되게 맞춤

즉, `Phase 4`는 "더미 이벤트 표시"를 "실제 서비스 캐시 기반 이벤트 표시"로 바꾸는 단계였다.

---

### 4-2. 구현 내용

#### 4-2-1. `WsClient` 도입

신규 파일:

- [`ws_client.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.h)
- [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)

주요 역할:

- `QWebSocket` 래퍼
- `Authorization: Bearer <token>` 포함 연결
- `Sec-WebSocket-Protocol: vms.events.v1` 설정
- heartbeat `30s ping / 60s timeout`
- reconnect backoff `1s -> 30s cap`
- raw text/json 메시지 전달

중요한 설계 판단:

- `ack/nack`, `msgId`, 이벤트 타입 해석은 `WsClient`에 넣지 않았다
- 이 해석은 `EventService`에 미뤘다

#### 4-2-2. `EventService` 코어 추가

신규 파일:

- [`event_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.h)
- [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)

주요 API:

- `ingestWsMessage(QJsonObject)`
- `fetchRecentEvents(...)`
- `reset()`
- `markAllRead()`
- `events()`
- `unreadCount()`
- `lastEventId()`

내부 상태:

- `QVector<EventInfo> m_events`
- `int m_unreadCount`
- `QString m_lastEventId`
- dedupe용 `QSet<QString>` / fallback key 맵
- in-flight REST 응답 무시용 `m_epoch`

지원한 메시지 shape:

- 단일 root object event
- `{ "event": {...} }`
- `{ "data": {...} }`
- `{ "data": [...] }`
- `{ "events": [...] }`
- `{ "items": [...] }`

#### 4-2-3. `MainWindow` 오케스트레이션 연결

`MainWindow`는 이제 아래만 담당한다.

- 로그인 성공 시:
  - `WsClient connect`
  - `EventService reset`
  - `fetchRecentEvents()`
- WS connected 시:
  - reconnect recovery 용 `fetchRecentEvents()` 재호출
- 로그아웃/401 시:
  - `EventService reset`
  - `WsClient disconnect`

또한 `WsClient::jsonMessageReceived`는 `MainWindow` 람다에서:

- `m_eventService` 존재 여부
- `AppState::isAuthenticated`

를 확인한 뒤에만 `EventService::ingestWsMessage()`로 넘기도록 했다.

#### 4-2-4. 이벤트 UI 데이터 소스 전환

이 단계에서 이벤트 UI 경로는 `DummyData::events()` 대신 `EventService` 캐시를 읽도록 바뀌었다.

주요 변경:

- [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
  - `setEventService()`
  - `currentEvents()`
  - `currentUnreadCount()`
  - 알림센터/이벤트 검색/상세모달이 서비스 캐시를 읽도록 전환
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
  - `EventViewWidget`가 `EventService` 이벤트 목록을 구독
  - unread는 `EventService::unreadChanged` 기준으로 반영
  - 이벤트뷰 visible 상태에서는 `markAllRead()`
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
  - Topbar unread를 `EventService` 기준으로 반영
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
  - 더미 이벤트 타이머 제거
  - unread/알림센터 clear를 서비스 기준으로 전환
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
  - 더미 스낵바 타이머 제거
  - unread/알림센터 clear를 서비스 기준으로 전환

#### 4-2-5. `/events`, `/event/{eventId}`, `/events/ws` 역할 분리

Phase 4 후반에는 이벤트 API의 역할을 아래처럼 명확히 정리했다.

- `GET /events`
  - 이벤트뷰 / 알림센터 / 이벤트 검색 목록의 기본 데이터 소스
  - 앱 진입 시점이나 WS 재연결 직후 "지금 DB에 저장되어 있는 이벤트 목록"을 seed로 채우는 역할
- `GET /event/{eventId}`
  - 상세 모달 전용 보강 호출
  - 목록에서 선택한 단일 이벤트의 `summary`, `bestshotId` 같은 상세 정보를 보강하는 역할
- `WS /events/ws`
  - 실시간 신규 이벤트 push
  - 목록 맨 앞 추가, unread 증가, Topbar 배지 갱신 담당

처음 구현에서는 `/events`와 WS가 모두 "이벤트를 준다"는 수준까지만 맞춰져 있었고, `/event/{eventId}`는 디버그 확인용으로만 연결돼 있었다. 이후 실제 서버 응답을 확인하면서 다음처럼 정리했다.

- `/events`
  - 초기 응답은 `eventId`, `eventTime`, `eventType` 중심으로 파싱
  - 이후 서버 반영 계획에 맞춰 `channelId`를 목록 단계에서 함께 받는 방향으로 정렬
- `/event/{eventId}`
  - 실제 응답에서 `summary`, `bestshotId`를 확인
  - 상세 모달은 이 API를 기준으로 보강하도록 확정
- WS
  - 실시간 unread 증가와 이벤트 append만 담당
  - 초기 DB 목록을 unread로 계산하지 않도록 정책 분리

최종적으로 이벤트 UI는 아래 정책을 따르게 됐다.

- 이벤트뷰 / 알림센터 / 검색 목록:
  - `/events` + `/events/ws`
- 상세 모달:
  - `/event/{eventId}`

이 정리로 인해 "목록용 API"와 "상세용 API"가 분리되었고, 향후 `channelId`가 `/events`, `/event/{eventId}`에 모두 반영되면 채널명 매핑과 상세 정보 보강을 깔끔하게 이어갈 수 있는 상태가 됐다.

#### 4-2-6. `app_config`와 WS 명세 정합화

`app_config.json`과 loader에 아래 항목이 들어갔다.

- `event.wsUrl`
- `event.wsSubprotocol`
- `event.eventsPath`

실제 서버 명세와 맞춘 값:

- WS URL: `/events/ws`
- subprotocol: `vms.events.v1`
- REST 복구: `/events`
- 상세 조회: `/event/{eventId}`

---

### 4-3. 구현 중 실제 문제와 해결

#### 문제 1. WS handshake 404

원인:

- 초기에는 WS endpoint path와 subprotocol 설정이 실제 서버 명세와 맞지 않았다

해결:

- `event.wsUrl`을 `/events/ws` 기준으로 수정
- `vms.events.v1` subprotocol 추가

관련 파일:

- 수정: [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)
- 수정: [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- 수정: [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

#### 문제 2. heartbeat timeout이 실제로 죽은 연결을 못 잡을 수 있음

원인:

- `ping` 전송 시점마다 timeout을 연장하면 `pong`이 없어도 연결이 살아있는 것처럼 남을 수 있었다

해결:

- timeout은 `pong` 또는 incoming message 수신 시에만 연장
- `ping` 송신 자체는 timeout reset 트리거로 쓰지 않음

관련 파일:

- 수정: [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)

#### 문제 3. 로그아웃/401 직후 늦게 도착한 `/events` 응답 재유입

원인:

- 로그인 시 시작한 `fetchRecentEvents()` 응답이 늦게 돌아오면, 로그아웃/401 후에도 이벤트 캐시가 다시 채워질 수 있었다

해결:

- `EventService::m_epoch` 가드 추가
- `reset()` 시 epoch 증가
- 이전 epoch 응답은 무시

관련 파일:

- 수정: [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)

#### 문제 4. 로그아웃 직후 WS 지연 메시지 재유입 가능성

원인:

- `WsClient::jsonMessageReceived`를 `EventService`에 직접 연결하면, 로그아웃 직후 경계 상황에서 메시지가 다시 캐시를 채울 수 있었다

해결:

- `MainWindow` 람다에서 `AppState::isAuthenticated` 확인 후에만 `ingestWsMessage()`

관련 파일:

- 수정: [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

#### 문제 5. `UGV/Playback` 더미 이벤트 경로 잔존

원인:

- 이벤트 UI 데이터 소스를 바꾼 뒤에도 더미 스낵바/타이머 멤버와 include가 일부 남아 있었다

해결:

- `UgvScreen::m_dummyEventTimer` 제거
- `PlaybackScreen::m_dummySnackbarTimer` 제거
- 관련 `dummy_data.h` include 제거

관련 파일:

- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 문제 6. 초기 `/events` 복구 결과를 unread로 세면 수천 건이 바로 누적됨

원인:

- `/events`는 DB에 이미 저장된 과거 이벤트 목록인데, 초기 구현에서는 WS 신규 이벤트와 같은 규칙으로 unread를 증가시켰다
- 로그인 직후 초기 pull + WS connected recovery가 겹치면 unread가 더 크게 튀어 보일 수 있었다

해결:

- `/events` merge는 캐시만 채우고 unread는 증가시키지 않도록 정책 분리
- unread는 WS로 들어온 신규 이벤트에 대해서만 증가

관련 파일:

- 수정: [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)

#### 문제 7. 실제 서버 `/events`와 `/event/{eventId}` 응답 shape가 초기 가정보다 얇았음

확인 결과:

- `/events`
  - 실제 응답에는 `eventId`, `eventTime`, `eventType` 중심 필드만 들어왔다
- `/event/{eventId}`
  - 실제 응답에는 `summary`, `bestshotId`가 들어왔다
  - 당시 응답에는 `deviceId`, `channelId`, `channelName`은 없었다

이로 인해 초기에 예상했던 "목록에서 곧바로 채널명까지 완전 표시"는 바로 성립하지 않았다.

판단과 정리:

- 목록은 `/events` 중심
- 상세는 `/event/{eventId}` 중심
- 실시간 추가는 WS 중심
- 이후 서버 반영으로 `/events`, `/event/{eventId}` 모두에 `channelId`를 추가하기로 합의

관련 파일:

- 수정: [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)
- 수정: [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
- 수정: [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

---

### 4-4. Phase 4 종료 시점 Known issue / 후속 과제

- [P2] 로그인 직후 `fetchRecentEvents()`가 초기 pull + WS connected recovery로 2회 호출될 수 있으나, dedupe로 기능 영향은 없고 당시 기준으로 허용
  - 이후 `Phase 5`에서 `m_skipNextWsRecovery`를 도입해 1회 호출로 정리함
- [P2] `WsClient` / `EventService` 임시 디버그 로그는 API 정렬 확인 후 제거 완료. 정식 로그 정책은 필요 시 별도 로깅 레벨로 재도입
- [P2] `event_ui_helpers`의 전역 `EventService*` 포인터는 단순한 임시 DI 방식이며, 향후 더 명시적인 주입 구조로 정리 가능
- [P2] `common_ui`의 이벤트/알림 forwarding wrapper는 호환성 목적으로 남아 있으며, 이후 호출부 정리 시 직접 `EventUiHelpers` 호출로 축소 가능
- [P1] WS/이벤트 서버 구현 완료 전에는 재연결/복구 경로의 최종 E2E 검증이 제한됨

---

## Phase 4.5. 성능 안정화 / 타입 의존 정리

### 4.5-1. 목표

Phase 4.5는 새 기능 추가가 아니라, Phase 1~4.5까지의 구현 과정에서 남아 있던 두 가지 실질 이슈를 짧게 정리하는 단계였다.

- 화면 전환 후 보이지 않는 live 파이프라인이 계속 도는 비용 줄이기
- 이벤트/채널 공용 타입을 `dummy_data.h`에서 분리해 서비스 계층 의존을 정리하기

즉, 이 단계는 "전체 최적화"가 아니라 `Phase 5` 진입 전에 사용감과 타입 경계를 조금 더 안전하게 만드는 스프린트였다.

---

### 4.5-2. 구현 내용

#### 4.5-2-1. 비활성 화면 파이프라인 suspend/resume

핵심 아이디어는 `showScreen()` 기준으로 현재 화면에서 필요한 채널만 active로 두고, 나머지 live 채널은 `PAUSED` 상태로 내리는 것이었다.

적용 내용:

- [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
  - `applyActiveChannels(const QSet<QString>&)` 추가
  - active 집합에 없는 세션은 `setPaused(true)`
  - active 집합에 있는 세션은 `setPaused(false)`
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
  - `activeChannelsForScreen(ScreenId)` helper 추가
  - `showScreen()` 끝에서 현재 화면 기준 active 채널 집합을 계산해 `ChannelSessionManager`에 전달

화면별 정책:

- `Main`
  - `cellChannels`에 올라온 멀티뷰 채널만 active
- `CCTV`
  - 현재 CCTV 채널 1개만 active
- `UGV`
  - 현재 UGV 채널 1개만 active
- `Playback`, 로그인, 장치확인
  - live 채널 전부 paused

추가 보강:

- [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
  - `setPaused()`는 같은 상태 재적용 시 조기 return
  - 파이프라인이 아직 없을 때도 `m_paused` 의도는 먼저 저장

이 정리로 인해 화면 전환 시 보이지 않는 라이브 파이프라인이 계속 `PLAYING` 상태로 남는 비용을 줄일 수 있는 기반이 생겼다.

#### 4.5-2-2. `EventInfo` / `ChannelStatus` 타입 분리

초기에는 이벤트/채널 관련 공용 타입이 `dummy_data.h` 안에 섞여 있어서, 실제 서비스 계층도 더미 헤더를 include하는 모양이 남아 있었다.

이를 아래처럼 분리했다.

- 신규: [`event_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_types.h)
  - `EventInfo`
- 신규: [`channel_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_types.h)
  - `ChannelStatus`

그 결과:

- [`event_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.h)
- [`event_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.h)
- [`common_widgets.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.h)
- [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)

는 더 이상 `dummy_data.h`를 타입 정의 용도로 끌어오지 않아도 되게 됐다.

`dummy_data.h`는 이제:

- 더미 데이터 공급 함수
- 더미 `DeviceInfo`

수준만 소유하고, 공용 이벤트/채널 타입 정의는 별도 헤더가 담당한다.

---

### 4.5-3. 구현 중 실제 문제와 해결

#### 문제 1. `setPaused()`가 같은 상태에도 매번 GStreamer state 전환을 수행

원인:

- 화면 전환이 일어날 때 active 채널 집합을 다시 적용하면서, 이미 paused 상태인 세션에도 `gst_element_set_state + get_state(200ms)`가 반복될 수 있었다

해결:

- `paused == m_paused`면 바로 return
- 파이프라인 미생성 상태에서도 pause intent는 먼저 기록

관련 파일:

- 수정: [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)

#### 문제 2. `EventInfo`만 분리하고 `ChannelStatus`는 남겨두면 `common_ui.h` 연쇄 include 오류 발생

원인:

- `common_ui.h`가 이벤트 타입 분리 과정에서 `dummy_data.h`를 끊었는데, 당시 `ChannelStatus`는 아직 더미 헤더 쪽에 남아 있었다

해결:

- `ChannelStatus`도 별도 [`channel_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_types.h)로 분리
- 이후 `common_ui.h`가 `event_types.h`, `channel_types.h`를 직접 include하도록 정리

관련 파일:

- 신규: [`channel_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_types.h)
- 수정: [`dummy_data.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/dummy_data.h)
- 수정: [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)

---

### 4.5-4. Phase 4.5 종료 시점 Known issue / 후속 과제

- [P2] live 파이프라인 suspend/resume는 1차 state 전환 기반이며, `READY` 하향이나 근본 렌더 경로 최적화는 아직 하지 않음
- [P2] active 채널 판정은 아직 문자열 채널명 브리지(`cellChannels`, `activeChannel`)를 일부 사용하며, `Phase 6`에서 `channelId` 중심으로 더 정리 필요
- [P3] 멀티뷰 체감 성능의 본체는 여전히 `QImage` copy + CPU paint 경로이며, 이 단계는 비용을 완화한 것이지 근본 해결은 아님

---

## Phase 5. CCTV 제어

### 5-1. 목표

Phase 5의 목표는 CCTV 화면에 남아 있던 임시 Zoom/Focus UI를 실제 REST 제어 경로로 바꾸고, Phase 4에서 도입된 이벤트 서비스 경로를 CCTV 제어 진입 전에 조금 더 안전하게 정리하는 것이었다.

- `CctvControlService`를 추가해 CCTV 제어를 서비스 계층으로 분리
- `CctvScreen`의 Zoom/Focus 조작을 실제 `/channel/{channelId}/zoom|focus` 왕복으로 연결

즉, 이 단계는 새 화면을 추가하는 작업이 아니라, 기존 CCTV 화면의 임시 제어 UI를 실제 서버 왕복 구조로 바꾸는 단계였다.

---

### 5-2. 구현 내용

#### 5-2-1. `EventService` 선행 정리

Phase 5 초반에는 CCTV 제어 자체보다 먼저, Phase 4에서 남겨뒀던 이벤트 서비스 보강 항목을 정리했다.

- [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)
  - 이벤트 시간 파싱을 ISO8601, 공백 구분 timestamp, timezone offset까지 허용하는 다중 포맷 기준으로 정리
  - 내부 저장 timestamp는 UTC ISO 문자열로 정규화
  - `eventId`가 없을 때 dedupe fallback 키를 `timestamp|channelId|channel|type|deviceId`로 보강
  - 정렬 비교를 문자열 비교 대신 timestamp sort key 기준으로 전환
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
  - 로그인 직후 `/events` 초기 pull과 `WsClient::connected` 시점 recovery가 2회 중복 호출되지 않도록 1회 skip 플래그 추가
- [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
  - `/event/{eventId}` 상세 조회를 직접 REST 호출이 아니라 `EventService::fetchEventDetail()` 경유로 통일

#### 5-2-2. `CctvControlService` 추가

기존 `CctvScreen`은 Zoom/Focus 슬라이더만 있고 실제 서버 호출 경로는 없었다. 이를 아래 구조로 정리했다.

- 신규: [`cctv_control_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_control_service.h)
- 신규: [`cctv_control_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_control_service.cpp)

도입 내용:

- `RestClient*` 주입형 서비스 계층 추가
- `zoomStep(channelId, value, ...)`
- `focusStep(channelId, value, ...)`
- 엔드포인트:
  - `POST /channel/{channelId}/zoom`
  - `POST /channel/{channelId}/focus`
- 허용값:
  - `-100, -10, -1, 1, 10, 100`
- 허용되지 않는 값은 서비스에서 요청 자체를 차단
- 응답의 `error.code`, `message`, `data.result`를 파싱해 화면 계층에서 그대로 사용할 수 있는 결과 구조 제공

설정 파일도 함께 확장했다.

- [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

추가 키:

- `cctv.zoomPath`
- `cctv.focusPath`

#### 5-2-3. `CctvScreen` 실제 제어 연동

`CctvControlService`를 만든 뒤에는 이를 CCTV 화면에 실제로 연결했다.

- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
  - `CctvScreen::setCctvControlService(...)` 추가
  - Zoom/Focus 슬라이더/스핀박스, in-flight 상태, 최근 commit 값/step을 멤버로 승격
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
  - 런타임 화면 생성 시 `m_cctvControlService`를 `CctvScreen`에 주입
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
  - `sliderReleased`, `editingFinished`에서만 제어 요청이 나가도록 변경
  - `activeChannel -> channelId` 해석은 `selectedChannelIdForDisplayName(..., "CCTV")` 헬퍼를 사용
  - 절대 위치가 아니라 "마지막 commit 값 대비 delta" 기준으로 step 값을 계산
  - Zoom/Focus 각각 in-flight 플래그로 중복 요청 차단
  - 같은 상태 재전송을 줄이기 위한 최근 step/commit 값 가드 추가
  - 요청 시작 시 progress 상태 표시
  - 성공 시 success 상태로 commit 값 갱신
  - 실패 시 error 상태 + 팝업 표시 후 이전 commit 값으로 UI 롤백

즉, `Phase 5` 종료 시점의 CCTV 화면은 더 이상 "값만 움직이는 UI"가 아니라, 실제 REST 왕복 기반 제어 화면이 되었다.

---

### 5-3. 구현 중 실제 문제와 해결

#### 문제 1. 이벤트 시간 문자열을 단순 비교하면 timezone/공백 포맷에서 최신순 정렬이 흔들릴 수 있음

원인:

- `Phase 4`까지는 timestamp를 거의 문자열 그대로 다뤘기 때문에, `2026-03-11 10:00:00`, `2026-03-11T01:00:00Z`, `2026-03-11T10:00:00+09:00` 같은 값이 섞이면 최신순 판단이 불안정할 수 있었다

해결:

- 다중 포맷 timestamp 파서를 추가
- 내부 저장은 UTC ISO 문자열로 정규화
- 정렬 비교는 millisecond sort key 기반으로 변경

관련 파일:

- 수정: [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)

#### 문제 2. CCTV 슬라이더의 절대 위치를 그대로 보내면 API 의미(step 제어)와 맞지 않음

원인:

- 화면의 Zoom/Focus UI는 절대 위치처럼 보이지만, 서버 API는 절대값이 아니라 step 명령(`-100/-10/-1/1/10/100`)을 받는다

해결:

- 현재 값 자체가 아니라 "마지막 commit 값 대비 delta"를 계산
- delta 크기에 따라 허용 step 중 하나로 매핑
- 같은 상태 재전송과 동시 중복 요청을 막기 위해 최근 step/in-flight 가드 추가

관련 파일:

- 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- 수정: [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)

---

### 5-4. Phase 5 종료 시점 Known issue / 후속 과제

- [P2] Zoom/Focus 실패 응답(`DEVICE_OFFLINE`, `DEVICE_TIMEOUT`, `CGI_ERROR`)의 세부 UX는 1차 팝업/상태 라벨 기준으로만 검증되었고, 에러 유형별 메시지 세분화는 후속 보강 가능
- [P2] `activeChannel` 기반 화면 상태는 여전히 문자열 브리지를 사용하며, `Phase 6` 이후 `channelId` 중심 런타임 판정으로 더 정리 가능
- [P3] `editingFinished`는 포커스 이동으로도 호출될 수 있으므로, 실제 사용 중 오동작이 보이면 dirty flag 조합으로 추가 보강 가능
- [P3] `focus/reset` 제어 API는 이번 범위에서 의도적으로 제외했으며, 서버/요구사항 확정 시 후속 확장 항목으로 처리

---

## Phase 6. Playback

### 6-1. 목표

Phase 6의 목표는 Playback 기능을 더미 데이터/로컬 파일 기반 화면에서 실제 서버 Playback API + MediaMTX playback 경로 기반 구조로 바꾸는 것이었다.

- `PlaybackService`를 추가해 `/playback/dates/{date}/channels`, `/playback/timeline`, `/playback/stream`을 서비스 계층으로 분리
- Playback 트리를 `멀티뷰 / CCTV / UGV / Playback` 화면에서 공통 데이터 소스로 통일
- Playback 화면의 채널 선택, 타임라인 조회, 이벤트 마커 클릭 흐름을 실제 서버 API 기반으로 연결
- MediaMTX `/get` URL 기반 playback 재생을 실제 `StreamPlayer` 경로로 연결
- Export API 계약을 `/playback/export`, `/playback/export/{jobId}` 비동기 job 방식으로 정리하고, 서버/클라이언트 양쪽 흐름을 맞춤

즉, `Phase 6`은 "Playback 화면을 실제 서비스/서버 playback 구조로 전환"하는 단계였고, 문자열 채널명/더미 데이터/로컬 파일 fallback을 줄이면서 `channelId`, `date`, `ts` 중심 구조로 옮겨가는 단계였다.

---

### 6-2. 구현 내용

#### 6-2-1. `PlaybackService` 추가

Playback 도메인을 별도 서비스 계층으로 분리했다.

- 신규: [`playback_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.h)
- 신규: [`playback_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.cpp)

도입 내용:

- `fetchAvailableChannels(date, ...)`
- `fetchTimeline(channelId, date, ...)`
- `requestStream(channelId, ts, ...)`
- 이후 export 연동을 위해
  - `requestExport(channelId, start, end, format, ...)`
  - `fetchExportStatus(jobId, ...)`
  추가

DTO 정리:

- `PlaybackChannelSummary`
- `PlaybackTimeRange`
- `PlaybackMarker`
- `PlaybackChannelsResult`
- `PlaybackTimelineResult`
- `PlaybackStreamResult`
- `PlaybackExportStartResult`
- `PlaybackExportStatusResult`

파싱/보강:

- `/playback/dates/{date}/channels` 응답은 `data`, `channels`, `items` 배열 shape를 모두 허용
- `/playback/stream` 응답의 상대 `uri`는 `apiBaseUrl` 기준 절대 URL로 보정
- `ts` 쿼리 파라미터는 `%2B`, `%3A`까지 포함해 percent-encode

#### 6-2-2. Playback 설정 경로 추가

Playback 관련 API 경로를 설정 파일로 외부화했다.

- [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

추가/정리된 키:

- `playback.channelsByDatePathTemplate`
- `playback.timelinePath`
- `playback.streamPath`
- `playback.exportPath`
- `playback.exportStatusPathTemplate`

또한 `channelsByDatePath` 구키를 `channelsByDatePathTemplate`로 정리하고, loader에서는 신키 우선 + 구키 fallback을 유지했다.

#### 6-2-3. `PlaybackScreen` 실제 API 기반 전환

Playback 화면을 로컬 파일/더미 기반에서 실제 playback API 기반으로 전환했다.

- 핵심 수정: [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- 연쇄 수정: [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)

구현 내용:

- Playback 트리 항목 더블클릭 시 `channelId`, `date`, `channelName`을 읽어 `fetchTimeline()` 호출
- timeline 결과의 `availableRanges`, `gaps`, `eventMarkers`를 화면 상태에 저장
- 채널 첫 클릭 시 `availableRanges.first().from`으로 기본 재생 시작 시각 계산
- 마커 클릭 시 해당 `marker.ts` 시각으로 `requestStream()` 재호출
- 응답으로 받은 playback URL을 `StreamPlayer::setSource()`로 연결
- timeline 응답 역전 방지를 위해 `m_timelineLoadGeneration` 가드 추가
- stream 요청은 `m_streamRequestInFlight`로 중복 차단

추가 정리:

- `PlaybackScreen` 내부 더미/로컬 파일 기반 마커/최신 파일 탐색 경로 제거
- 기존 로컬 ffmpeg export 경로 제거
- playback source 표시를 실제 서버 playback URL 기준으로 변경

#### 6-2-4. Playback 트리 공통화

Playback 트리를 각 화면이 따로 만들지 않고 공통 `SidebarWidget` 기준으로 통일했다.

- [`common_widgets.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.h)
- [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

도입 내용:

- 최근 3일 기준 `/playback/dates/{date}/channels` 호출
- 날짜 아래 `CCTV / UGV` 그룹 공통 구성
- `멀티뷰 / CCTV / UGV / Playback` 모두 같은 playback 트리를 사용
- 트리는 항상 펼침 상태 유지
- `playbackTargetDate` 상태를 추가해 어느 화면에서 선택하든 같은 날짜 맥락으로 Playback 화면 진입

#### 6-2-5. `StreamPlayer` playback capability 정리

Playback 재생 포맷 대응을 위해 `StreamPlayer`를 보강했다.

- [`stream_player.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)

도입 내용:

- `http://`, `https://` playback URL을 `uridecodebin` 기반으로 재생 가능하게 보강
- `canSeek()`
- `canChangePlaybackRate()`

다만 실제 Playback seek/rate는 HLS/MediaMTX 응답 특성상 안정성 검증이 안 끝나서, capability API만 추가하고 화면에서는 보수적으로 제한하는 방향으로 정리했다.

#### 6-2-6. `channelId` 중심 active 판정 보강

`Phase 4.5`까지 남아 있던 문자열 채널명 의존을 Playback 진입과 함께 일부 정리했다.

- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

추가 helper:

- `findSelectedContextByChannelId(int)`
- `displayNameForChannelId(int)`
- `firstSelectedChannelIdByType(QString)`

적용 효과:

- `activeChannelsForScreen()`가 `displayName`만 믿지 않고 `channelId` 기반으로 active 채널을 계산
- `Main`, `CCTV`, `UGV` 화면의 active 판정이 `selectedChannelContexts.channelId + deviceType` 기준으로 더 안정화

#### 6-2-7. Export API 계약 정리 및 클라이언트 연동

Playback export는 2~3분 구간 이상도 고려해 비동기 job 방식으로 정리했다.

- API 문서 수정: [`docs/통신 API 설계 - RESTful API.csv`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/통신%20API%20설계%20-%20RESTful%20API.csv)

추가된 API:

- `POST /playback/export`
- `GET /playback/export/{jobId}`

클라이언트 연동:

- `PlaybackScreen`의 기존 로컬 ffmpeg export 제거
- export 요청 시 서버 job 생성
- 1초 간격 polling으로 상태 조회
- `DONE`이면 export URL을 앱이 직접 다운로드
- 저장 경로는 `clipSaveDirectory()` 우선, 경로가 유효하지 않으면 저장 대화상자 fallback

#### 6-2-8. 서버 Playback / MediaMTX 코드 보강

이번 Phase에서는 클라이언트만이 아니라 서버 쪽 playback API 코드도 같이 손봤다.

- [`server/vmsapi/VmsPlaybackController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackController.cpp)
- [`server/vmsapi/VmsPlaybackService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackService.cpp)
- [`server/mediaMTX/MediaMTXController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/mediaMTX/MediaMTXController.cpp)

핵심 정리:

- `timeline`은 merged `availableRanges` 기준으로 계산
- `stream`은 단일 segment가 아니라 `ts`가 속한 현재 merged range 기준으로 `endTs = range.to`를 반환
- `MediaMTXController::getPlaybackUrl()`은 `start/end` 차이로 `duration`을 계산해 mediaMTX `/get?path=...&start=...&duration=...` URL을 직접 생성
- playback public URI 재작성 시 query string 유실 방지
- export는 mediaMTX direct URL + polling 구조로 서버 응답 계약 정리

즉 `Phase 6`에서는 client/server/playback engine(MediaMTX) 3축 계약을 같이 맞추는 작업까지 포함됐다.

#### 6-2-9. Playback UX 정책 확정 및 이름 기반 브리지 추가 축소

Phase 6 후반에는 Playback UX를 실제 운영 기준에 맞게 다시 정리했다.

- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

정리한 정책:

- 자유 seek는 이번 Phase 범위에서 제거
- Playback 슬라이더는 표시 전용으로 유지
- 채널 첫 클릭은 `availableRanges.first().from -> current range.to`
- 이벤트 마커 클릭은 `marker.ts -> current range.to`
- 배속은 이번 Phase 범위에서 제외

구현 내용:

- `PlaybackScreen`에서 자유 seek 경로와 `seekToMs()` fallback 제거
- 슬라이더는 현재 재생 위치 표시만 담당하고, 마커 클릭 시에만 서버 `/playback/stream` 재요청
- 마커 overlay가 정상 클릭되도록 slider enable 정책 재정리
- `AppState`에 `channelRtspById`를 추가하고, RTSP 조회를 `displayName -> channelId -> rtsp` 우선 구조로 변경
- `rtspUrlForDisplayName()`, `rtspUrlForChannelId()` helper를 추가해 기존 `rtspUrlForChannel(name)` 의존을 fallback 수준으로 축소
- `Main / CCTV / UGV / channel_session_manager` 호출부도 가능한 범위까지 `channelId` 중심 해석 경로로 정리

---

### 6-3. 구현 중 실제 문제와 해결

#### 문제 1. `/playback/stream` 요청의 `ts`에 timezone `+`가 인코딩되지 않아 `INVALID_ARGUMENT`

원인:

- 쿼리스트링에서 `+09:00`의 `+`가 안전하게 인코딩되지 않으면 서버 파서에서 공백으로 깨질 수 있었다.

해결:

- `PlaybackService::buildStreamQueryPath()`에서 `ts`를 percent-encode 하도록 수정
- 결과적으로 `2026-03-11T11%3A41%3A26%2B09%3A00` 형태로 전송되게 변경

관련 파일:

- [`playback_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.cpp)

#### 문제 2. playback 트리가 화면마다 달라 더미 데이터/실데이터가 섞여 보임

원인:

- `PlaybackScreen`과 공통 sidebar가 각자 다른 방식으로 playback tree를 그리고 있었다.

해결:

- playback tree 생성/로딩을 공통 `SidebarWidget` 기준으로 통일
- `PlaybackScreen` 전용 트리 로딩 코드 제거

관련 파일:

- [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 문제 3. timeline 요청 역전으로 이전 더블클릭 응답이 최신 상태를 덮을 수 있음

원인:

- 연속 더블클릭 시 여러 timeline 요청이 동시에 나가고, 늦게 도착한 이전 응답이 최신 선택 상태를 덮을 수 있었다.

해결:

- `m_timelineLoadGeneration`를 두고 callback에서 최신 generation만 반영

관련 파일:

- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

#### 문제 4. 서버 `timeline`과 `stream` 계약이 달라 first range가 짧게 끊겨 보이거나 duration이 어긋남

원인:

- 초기 서버 코드는 merged range가 아니라 단일 archive segment 기준으로 lookup하는 구조라, `timeline.availableRanges`와 실제 `/playback/stream` 길이가 안 맞을 가능성이 컸다.

해결:

- 서버 `VmsPlaybackService`에서 merged range를 다시 계산하고, `ts`가 속한 range의 `to`를 `endTs`로 반환하도록 변경
- `MediaMTXController::getPlaybackUrl()`도 `start/end` 기반 `duration` 계산으로 정리

관련 파일:

- [`server/vmsapi/VmsPlaybackService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackService.cpp)
- [`server/mediaMTX/MediaMTXController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/mediaMTX/MediaMTXController.cpp)

#### 문제 5. export를 브라우저로만 열면 저장 경로를 앱이 제어할 수 없음

원인:

- `QDesktopServices::openUrl()`은 브라우저/OS 기본 앱에 넘기는 역할만 하므로 저장 위치를 클라이언트가 정할 수 없었다.

해결:

- export `DONE` 후 앱이 직접 `QNetworkAccessManager`로 다운로드
- `clipSaveDirectory()`를 기본 저장 경로로 사용
- 경로가 유효하지 않으면 저장 대화상자 fallback

관련 파일:

- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

---

### 6-4. Phase 6 종료 시점 Known issue / 후속 과제

- [P1] export API는 polling + direct download 흐름까지 붙었지만, 실제 서버 배포 환경 반영 후 `jobId -> DONE -> uri 다운로드`까지의 운영 검증은 추가 확인이 필요하다.
- [P2] 서버 Playback 패치가 실제 배포 서버에 반영되기 전에는, 마커 클릭 시 `clickedTs -> range.to` 정책이 응답 `uri.start`에 정확히 반영되는지 최종 확인이 필요하다.
- [P2] `rtspUrlForChannel(name)` 같은 문자열 브리지는 크게 줄였지만 완전 제거는 아니며, 이후 phase에서 `SelectedChannelContext`/registry 중심 구조로 더 밀어야 한다.
- [P3] playback gap stitch 정책은 현재 range 단위 기준이며, 여러 available range를 자동 이어붙일지 여부는 서버 정책 확정 후 추가 보강 가능하다.

---

## Phase 7. 클립 캡처/다운로드 운영 안정화

### 7-1. 목표

Phase 7의 목표는 두 가지였다.

- `7-A`: 클립 캡처 운영 안정화(상태/실패/취소/경로 검증)
- `7-B`: Playback export 운영 안정화(대기/polling/다운로드/취소 정책/UX)

즉, 새 기능 추가보다 운영 중 실패/중단/경계 케이스를 정리하는 단계로 진행했다.

---

### 7-2. 구현 내용

#### 7-2-1. `7-A` 클립 캡처 운영 정리

적용 파일:

- [`clip_capture_manager.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)
- [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

정리 내용:

- `State(Idle/Recording/Encoding)`를 단일 상태 소스로 도입
- `EncodeResult(code/message/outputPath)` 기반으로 실패 코드화
- `prepareEncoding()`과 `encodeSnapshot()`을 분리해 메인 스레드 스냅샷 + worker 인코딩 구조로 1차 정리
- 로그아웃/종료 시 클립 저장은 `discard()`로 즉시 취소하는 정책으로 통일
- 전역 ffmpeg 경고 토스트 제거, 클립 경로에서만 ffmpeg 진단/안내 유지
- 취소를 일반 실패와 분리(`EncodeError::Cancelled`)

#### 7-2-2. `7-B-1` Export polling 안정화

적용 파일:

- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

정리 내용:

- `m_exportGeneration`, `m_exportStatusInFlight` 추가
- `requestExport()`와 `fetchExportStatus()` 모두 generation 가드 적용
- polling 중복 요청 차단
- 상태 분기를 `QUEUED/PROCESSING/DONE/FAILED`로 고정하고, 그 외 상태는 오류 처리

#### 7-2-3. `7-B-2` 다운로드 안정화

적용 파일:

- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

정리 내용:

- `QFile`에서 `QSaveFile`로 전환해 원자적 저장 적용
- invalid URI(`http/https`가 아니거나 invalid URL) 사전 차단
- 다운로드 중 재요청 차단 강화
- write 실패 시 `abort()`, 실패 시 `cancelWriting()`, 성공 시 `commit()` 처리
- 기본 저장 경로가 유효하지 않을 때 fallback 전 안내 추가

#### 7-2-4. `7-B-3` UX/종료 정책 마감

적용 파일:

- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

정리 내용:

- `isExportBusy()` 기준으로 export 버튼 비활성/복구
- `cancelExportOperations()`로 polling 중단 + generation invalidate + download abort + `QSaveFile::cancelWriting()`를 통합
- 로그아웃/종료 확인 문구에 export 진행 상태 반영
- 로그아웃은 취소 안내 포함, 종료는 조용한 취소 정책으로 정리

---

### 7-3. Known issue / 후속 과제

- `7-B-4` 통합 검증은 서버 export 코드 배포 대기 상태로 `Blocked`
  - 사유: 서버 `/playback/export*` 최신 코드가 실제 실행 서버에 아직 반영되지 않아 E2E 검증이 불가
- 검증 필요 항목:
  - `jobId -> DONE -> 다운로드 저장` E2E
  - `FAILED`, `timeout`, `invalid uri` UX
  - 로그아웃/종료 중 export abort 시 부분 파일 미생성 확인

즉 구현은 `7-A`, `7-B-1~3`까지 완료됐고, `7-B-4`는 서버 배포 후 통합 검증만 남아 있다.

---

## Phase 8. UGV

### 8-1. 목표

Phase 8의 목표는 더미 상태였던 UGV 화면을 실제 `REST + WS(/gw/ws)` 연동 구조로 전환하는 것이었다.

핵심 목표:

- `UgvService`를 통해 `/gw/ws + Authorization Bearer + vms.gw.v1` 경로를 고정
- `UGV 화면 진입`과 `출동(request.conn.ugv)`을 분리
- `cmd.drive`, `cmd.ptz`, `telemetry.gps`, `telemetry.rssi` 경로를 실제 UI에 연결
- `displayName` 브리지 대신 `gatewayId(deviceId) + ugvId(channelId)` direct path로 정리
- 종료/로그아웃/화면 전환 시 해제 정책을 일관화

---

### 8-2. 구현 내용

#### 8-2-1. `8-A` UgvGatewayService 계층 추가

적용 파일:

- [`ugv_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.h)
- [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- [`ws_client.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.h)
- [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)
- [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

정리 내용:

- UGV 전용 `UgvService`를 추가하고 클라이언트는 `/gw/ws`만 사용하도록 고정했다.
- config 키를 `ugv.gatewayWsUrl`, `ugv.gatewayWsSubprotocol(vms.gw.v1)`로 정리했다.
- `request.conn.ugv`, `request.disconn.ugv`, `cmd.drive`, `cmd.ptz` 송신 구조를 추가했다.
- `telemetry.gps`, `telemetry.rssi`, 각종 `*.ack`를 서비스 계층에서 파싱하도록 정리했다.
- `msgId` 기준 pending command, ACK timeout, telemetry auto-ack 골격을 추가했다.
- `WsClient`의 auto reconnect는 UGV 정책과 충돌하지 않도록 기본 비활성으로 두었다.
- 이후 리팩터링으로 `UgvService`가 `AppState`를 직접 읽지 않고 `setAccessToken()` 주입 구조를 사용하도록 바꿨다.

#### 8-2-2. `8-B` UgvScreen 실제 연동

적용 파일:

- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`screens.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)

정리 내용:

- `UgvService`를 `UgvScreen`에 주입하고, 연결 전에는 drive/PTZ를 비활성화하도록 정리했다.
- `출동 시작` 버튼을 명시 액션으로 두고 `request.conn.ugv`를 보내도록 구성했다.
- `QGraphicsScene/QGraphicsView` 기반 기존 맵 구조를 유지한 채 GPS marker / heading / speed / 최근 경로 누적을 반영했다.
- `telemetry.rssi`를 OSD overlay에 반영했다.
- drive 버튼은 press/release 모두 `cmd.drive`로, PTZ는 dpad/slider release 기준으로 `cmd.ptz`로 연결했다.
- `mission 종료`, `hideEvent`, `logout`, `close`에서 `request.disconn.ugv` 또는 `shutdown()`이 중복 없이 정리되도록 가드를 넣었다.

현재 상태:

- 구현은 완료
- `/gw/ws` 연결과 `DEVICE_OFFLINE` 실패 경로까지는 확인
- 실제 `gateway /ugv/ws` 미구현/미연결로 `request.conn.ugv.ack`, telemetry, `cmd.drive/cmd.ptz ack`, `request.disconn.ugv.ack` 성공 경로 E2E는 `Blocked`

#### 8-2-3. `8-C` direct path / 컨텍스트 정책 고정

적용 파일:

- [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- [`common_ui.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.h)
- [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)

정리 내용:

- `activeUgvGatewayId`, `activeUgvChannelId`를 추가해 UGV 화면의 direct path 상태를 별도로 유지했다.
- Main 셀에도 `cellChannelIds`, `cellDeviceIds`를 추가해 UGV 진입 시 문자열 채널명을 재해석하지 않도록 정리했다.
- 채널 트리 leaf에 `channelId/deviceId/displayName/deviceType` 메타데이터를 저장하게 바꿨다.
- `displayName`이 맞지 않아도 “첫 UGV”를 골라 들어가던 fallback 경로를 제거하고, direct 식별자가 없으면 진입을 막도록 바꿨다.
- 이벤트뷰 UGV 출동 경로도 `activeUgvChannelId -> selectedCell channelId -> firstSelected UGV` 순으로 direct path 우선 정책으로 정리했다.

#### 8-2-4. `8-D` 운영 안정화

적용 파일:

- [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- [`ugv_screen_map.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_map.cpp)
- [`ugv_screen_commands.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_commands.cpp)
- [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)
- [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)

정리 내용:

- `cmd.drive.ack`, `cmd.ptz.ack`도 `msgId` 기준 pending 매칭과 `gatewayId/ugvId` 대상 검증을 수행하도록 강화했다.
- 검증 통과 후에만 pending을 제거하고 `commandAck`를 emit 하도록 바꿨다.
- 중복/지연 `cmd.*.ack`는 노이즈를 줄이기 위해 조용히 무시하고, unknown ACK는 pending을 건드리지 않도록 정리했다.
- `main_screen`의 UGV 진입 경로도 direct metadata를 우선 사용하고, direct 식별자가 없으면 진입 차단 + 안내로 정리했다.
- UGV 오류 UX를 `DEVICE_OFFLINE / ACK timeout / telemetry ack 실패 / 기타`로 분류해 상태 라벨/스낵바 강도를 나눴다.
- `prepareForShutdown()`를 추가해 로그아웃/종료/unauthorized에서 `disconnect/shutdown` 정책을 1회성으로 고정했다.
- `ugv_screen.cpp`의 책임을 `ugv_screen_feedback.cpp`, `ugv_screen_map.cpp`, `ugv_screen_commands.cpp`로 분리했다.
- 외부 지도 엔진은 도입하지 않고 현재 Qt 맵 구조를 유지했다.

---

### 8-3. 구현 중 실제 문제와 해결

#### 문제 1. `/gw/ws`는 붙지만 `출동 시작` 시 `DEVICE_OFFLINE`이 발생

원인:

- 클라이언트와 서버 `/gw/ws`는 정상 연결됐지만, 서버 뒤쪽 gateway `/ugv/ws`가 아직 미구현/미연결 상태였다.

해결/판단:

- 클라이언트는 `/gw/ws` 핸드셰이크와 실패 경로 표시까지만 확인했다.
- 현재는 `UgvScreen`이 `DEVICE_OFFLINE: failed to connect gateway websocket`를 정상적으로 사용자에게 표시하는 실패 경로까지 확인된 상태로 정리했다.

#### 문제 2. UGV direct path 정책을 넣었는데 일부 경로에서 다시 이름 기반 fallback으로 오염될 수 있었음

원인:

- UGV 진입 전 `selectedChannelIdForDisplayName(..., "UGV")`를 먼저 호출하면 이름 불일치 시 “첫 UGV” fallback이 타버릴 수 있었다.

해결:

- 트리 leaf와 Main 셀에 `channelId/deviceId/deviceType` 메타데이터를 추가
- direct 식별자가 없는 UGV 경로는 진입을 차단
- UGV 여부도 문자열이 아니라 `deviceType == UGV` 기준으로 판정

#### 문제 3. `cmd.drive.ack`, `cmd.ptz.ack`를 느슨하게 처리하면 잘못된 ACK에도 성공 UI가 노출될 수 있었음

원인:

- pending 제거/emit 순서가 느슨하고, 대상 검증 없이 ACK를 성공처럼 소비할 수 있었다.

해결:

- `msgId`, `requestType`, `gatewayId`, `ugvId`를 모두 검증
- 검증 통과 후에만 pending 제거
- 유효 ACK만 `commandAck` emit

#### 문제 4. `ugv_screen.cpp`가 맵/명령/오류/세션 UI까지 모두 포함해 빠르게 비대해짐

원인:

- 8-B, 8-D를 진행하면서 UGV 화면 파일 하나에 telemetry/map/command/feedback 로직이 계속 누적됐다.

해결:

- `ugv_screen_feedback.cpp`
- `ugv_screen_map.cpp`
- `ugv_screen_commands.cpp`
로 분리해 `ugv_screen.cpp`는 orchestration 중심으로 정리했다.

---

### 8-4. Phase 8 종료 시점 Known issue / 후속 과제

- [P1] UGV success-path E2E(`request.conn.ugv.ack`, `telemetry.gps/rssi`, `cmd.drive/cmd.ptz ack`, `request.disconn.ugv.ack`)는 `gateway /ugv/ws` 미연결 상태로 `Blocked`
- [P2] `main_screen` 이벤트뷰의 `UGV 출동` 경로는 direct path 우선으로 정리됐지만, 정책상 기본 대상(활성 UGV/선택 셀/첫 UGV) 우선순위는 운영 피드백 후 확정 여지 있음
- [P3] `UgvScreen`은 책임 분리(`feedback/map/commands`)를 완료했지만, 장시간 운용 기준의 map route 상한/샘플링 튜닝은 후속 성능 점검 항목으로 남김
- [P3] 외부 지도 엔진 도입 여부는 의도적으로 보류했으며, 현재 Qt 맵 유지 정책이 Phase 8 완료의 필수 조건을 충족함

---

## Phase 1~8 동안 추가/수정/삭제된 주요 파일 요약

### 신규 추가

- [`video_render_widget.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)
- [`video_render_widget.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- [`rest_client.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/rest_client.h)
- [`rest_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/rest_client.cpp)
- [`auth_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/auth_service.h)
- [`auth_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/auth_service.cpp)
- [`device_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/device_service.h)
- [`device_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/device_service.cpp)
- [`selected_channel_context.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/selected_channel_context.h)
- [`app_config.json`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config.json)
- [`styles/v2_theme.qss`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/styles/v2_theme.qss)
- [`settings_dialog.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.h)
- [`settings_dialog.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.cpp)
- [`app_config_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.h)
- [`app_config_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_config_loader.cpp)
- [`theme_loader.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/theme_loader.h)
- [`theme_loader.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/theme_loader.cpp)
- [`event_ui_helpers.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.h)
- [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
- [`ws_client.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.h)
- [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)
- [`event_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.h)
- [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)
- [`cctv_control_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_control_service.h)
- [`cctv_control_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_control_service.cpp)
- [`playback_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.h)
- [`playback_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.cpp)
- [`ugv_service.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.h)
- [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- [`ugv_screen_feedback.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_feedback.cpp)
- [`ugv_screen_map.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_map.cpp)
- [`ugv_screen_commands.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen_commands.cpp)
- [`event_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_types.h)
- [`channel_types.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_types.h)
- [`docs/troubleshooting/troubleshooting_multiview_streaming.md`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/troubleshooting/troubleshooting_multiview_streaming.md)

### 삭제

- [`native_capture.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/native_capture.h)
- [`native_capture.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/native_capture.cpp)

### 수정 범위가 컸던 핵심 파일

- [`stream_player.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- [`channel_session_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- [`clip_capture_manager.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- [`common_ui.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp)
- [`common_widgets.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_widgets.cpp)
- [`login_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp)
- [`main_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp)
- [`cctv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/cctv_screen.cpp)
- [`ugv_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp)
- [`playback_screen.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)
- [`mainwindow.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp)
- [`app_state.h`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h)
- [`settings_dialog.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/settings_dialog.cpp)
- [`event_ui_helpers.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_ui_helpers.cpp)
- [`event_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp)
- [`ws_client.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ws_client.cpp)
- [`playback_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.cpp)
- [`ugv_service.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp)
- [`CMakeLists.txt`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/CMakeLists.txt)
- [`server/vmsapi/VmsPlaybackController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackController.cpp)
- [`server/vmsapi/VmsPlaybackService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackService.cpp)
- [`server/vmsapi/UgvController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvController.cpp)
- [`server/vmsapi/UgvService.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvService.cpp)
- [`server/mediaMTX/MediaMTXController.cpp`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/mediaMTX/MediaMTXController.cpp)
- [`docs/통신 API 설계 - RESTful API.csv`](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/docs/통신%20API%20설계%20-%20RESTful%20API.csv)

---

## 결론

Phase 1~8은 단순 UI 수정 단계가 아니라, v1 임시 구조를 v2 서비스/컨텍스트/공통 UX/실시간 이벤트/실제 제어/운영 안정화/UGV 게이트웨이 연동 구조로 갈아타는 과정이었다.

실제 구현에서 중요한 변화는 다음과 같다.

- 렌더/캡처/멀티뷰 기반 경로가 바뀌었다.
- 인증/장치조회/RTSP 확보 경로가 서버 중심 구조로 바뀌었다.
- 선택 단위가 문자열에서 `SelectedChannelContext` 기반으로 바뀌었다.
- Topbar/알림센터/공통 상태 슬롯/전용 모달/QSS 토대가 생겼다.
- `MainWindow`와 `common_ui`의 큰 덩어리 책임을 일부 분리해 이후 Phase를 받을 구조를 만들었다.
- 실시간 이벤트는 `WsClient + EventService` 기반으로 전환되기 시작했고, 이벤트 UI는 서비스 캐시를 구독하는 구조로 바뀌었다.
- `/events`는 목록 seed, `/event/{eventId}`는 상세 보강, `/events/ws`는 실시간 추가라는 역할이 정리됐다.
- `Phase 4.5`에서 비활성 화면 파이프라인 suspend/resume와 공용 타입 분리를 통해 `Phase 5` 진입 전 최소 성능/구조 안정화가 이뤄졌다.
- `Phase 5`에서 CCTV Zoom/Focus UI는 실제 `CctvControlService` 왕복 구조로 바뀌었고, 이벤트 시간/복구 경로도 함께 정리됐다.
- `Phase 6`에서 Playback은 로컬/더미 기반을 걷고 `/playback/*` API 중심(`timeline -> stream -> export`)으로 전환했다.
- Playback 트리는 공통 Sidebar로 통일했고, 채널 선택/자동재생 흐름은 `channelId` 중심으로 정리했다.
- Export는 서버 job(`POST /playback/export`) + 상태 polling(`GET /playback/export/{jobId}`) + direct download 구조로 확정했다.
- `Phase 7`에서 클립 캡처는 운영 안정화(상태 단일화, 결과 코드화, 취소 정책)까지 반영했다.
- `Phase 7`의 Playback export는 `7-B-1~3`(polling 안정화, 원자 저장, 종료/로그아웃 취소 정책)까지 구현 완료했다.
- `7-B-4` 통합 검증은 서버 export 코드 배포 전이라 `Blocked` 상태이며, 배포 후 E2E 검증으로 닫는다.
- `Phase 8`에서 UGV는 `/gw/ws` 기반 `UgvService + UgvScreen` 구조로 실제 연동됐고, direct path/ACK 검증/맵/종료 정책까지 정리됐다.
- `Phase 8`의 success-path E2E는 `gateway /ugv/ws`가 준비되지 않아 `Blocked` 상태이며, 현재는 `/gw/ws` 연결과 실패 경로(`DEVICE_OFFLINE`)까지 확인된 상태다.

남은 일은 여전히 많지만, 이제 이후 Phase는 "더미 구조 위에 기능을 얹는 단계"가 아니라, "이미 정리된 기반 위에 실제 서비스와 정책을 채우는 단계"로 넘어간 상태다.

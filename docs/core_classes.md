# VMS v2 핵심 클래스/함수 상세 가이드

- 최종 갱신: 2026-03-23
- 기준: `VMS_v2` 현재 코드(`*.h`, `*.cpp`)와 최신 구조/운영 문서
- 목적: 새 작업자가 이 문서 하나로 **구조, 런타임 흐름, 핵심 클래스, 주요 함수, 확장 시 주의점**을 따라갈 수 있도록 정리
- 상태: **리팩토링(standalone-vms) 시작 시점 스냅샷.** 이 문서는 통째로 다시 쓰지 않는다. Phase 종료 후 실제로 바뀐 클래스에 해당하는 섹션만 부분 수정하고, 화면/미디어 계층처럼 설계 원칙상 건드리지 않는 섹션은 그대로 둔다. 이전 버전이 필요하면 `git log -p docs/core_classes.md`로 확인한다.

> 이 문서는 "클래스 목록" 문서가 아니다.  
> 실제 코드를 읽을 때 무엇부터 보고, 어떤 객체가 무엇을 소유하고, 어디서 상태가 바뀌는지를 설명하는 **코드 독해용 가이드**다.

---

## 0. 이 문서를 읽는 방법

이 문서는 세 단계로 읽는 것이 좋다.

1. **Section 1~3**
   - 시스템을 어떻게 나누어 봐야 하는지
   - 앱이 어떻게 시작되고 로그인 후 어떤 객체가 살아나는지

2. **Section 4~8**
   - 핵심 타입, 루트 객체, 공통 위젯, 화면 클래스, 서비스, 미디어 계층

3. **Section 9~12**
   - 실제 호출 체인
   - 디버깅 포인트
   - 기능 추가 시 안전한 수정 방법

즉, 처음부터 끝까지 사전처럼 읽기보다:

- 구조 파악
- 담당 클래스 확인
- 수정 지점 탐색

순서로 보는 것이 가장 효율적이다.

---

## 1. 시스템을 보는 기본 관점

## 1.1 가장 중요한 mental model

`VMS_v2`는 크게 네 층으로 본다.

1. **조립/라우팅 층**
   - `MainWindow`
   - `AppState`

2. **화면/UI 층**
   - `LoginScreen`, `DeviceCheckScreen`, `MainScreen`, `CctvScreen`, `UgvScreen`, `PlaybackScreen`
   - `TopbarWidget`, `SidebarWidget`, `EventViewWidget`

3. **서비스/통신 층**
   - `AuthService`, `DeviceService`, `EventService`, `PlaybackService`, `CctvControlService`, `UgvService`
   - `RestClient`, `WsClient`

4. **미디어/세션 층**
   - `StreamPlayer`
   - `VideoRenderWidget`
   - `ChannelSessionManager`
   - `ClipCaptureManager`

이 앱은 화면이 직접 모든 걸 하지 않는다.  
화면은 대체로:

- 현재 컨텍스트를 정하고
- 서비스에 작업을 요청하고
- 결과를 UI에 반영하며
- 실제 영상 재생/정지는 `ChannelSessionManager` 쪽에 맡긴다

즉 "화면 = 기능의 주인"이라기보다,  
**"화면 = 현재 컨텍스트를 선택하고 이를 서비스/미디어 계층에 연결하는 조정자"**에 가깝다.

## 1.2 이 코드베이스를 읽을 때 가장 먼저 기억할 사실

- 전역 런타임 상태는 `AppState`에 많이 모여 있다
- 화면 전환의 진짜 핵심은 `MainWindow::showScreen(...)`
- 영상 생명주기의 진짜 핵심은 `ChannelSessionManager::applyActiveChannels(...)`
- 이벤트 UI는 `EventUiHelpers`
- playback는 `PlaybackScreen` 단일 파일이 아니라 3파일 분리 구조
- UGV는 `UgvScreen` + `UgvService`를 함께 봐야 이해된다

---

## 2. 런타임 생명주기

## 2.1 앱 시작

시작 지점은 `main.cpp`다.

주요 역할:

- `QApplication` 생성
- 전역 앱 아이콘 설정 (`:/styles/clue_logomark.svg`)
- `MainWindow` 생성 및 표시

즉 사용자 입장에서 보이는 대부분의 조립은 `MainWindow` 생성자 이후에 시작된다.

## 2.2 `MainWindow` 초기화

`MainWindow::MainWindow()`는 다음을 순서대로 수행한다.

- 상태 초기화
- UI 구성
- 인증/서비스 객체 생성
- 시그널 연결
- 첫 화면 표시

이때 중요한 점:

- 로그인/회원가입/장치확인 화면은 인증 전에도 존재한다
- Main/CCTV/UGV/Playback는 런타임 화면이므로 **로그인 후에 생성/재생성**될 수 있다

## 2.3 로그인 성공 이후

로그인 성공 후 흐름은 대략 이렇다.

1. `AuthService::login(...)` 성공
2. `AppState`에 토큰/유저 정보 기록
3. `RestClient` access token 주입
4. `UgvService` access token 주입
5. `EventService` 최근 이벤트 fetch
6. `WsClient` 인증 연결 시작
7. `showScreen(DeviceCheck)`
8. `QTimer::singleShot(300, ...)`로 DeviceCheck 새로고침 시작

DeviceCheck 첫 진입 시 300ms 딜레이를 두는 이유는, 로그인 직후 너무 많은 네트워크 작업이 한꺼번에 몰릴 때 첫 장치조회가 흔들리던 문제를 줄이기 위해서다.

## 2.4 DeviceCheck -> Main 진입

장치확인 화면에서 사용자가 채널을 선택하고 `VMS 시작`을 누르면:

1. 선택된 `SelectedChannelContext` 집합 정리
2. 각 채널의 상세 RTSP/codec 조회
3. `AppState.selectedChannelContexts` 채움
4. `AppState.channelRtspByName/Id`, `channelVideoCodecByName/Id` 구성
5. CCTV 채널을 `AppState.gridCells`에 자동 배치
6. 런타임 화면 생성(`createRuntimeScreens(...)`)
7. `showScreen(Main)`

즉 Main 진입 전에는 이미:

- 멀티뷰 초기 셀 상태
- RTSP 맵
- 코덱 힌트 맵

이 어느 정도 만들어져 있어야 한다.

## 2.5 런타임 화면 전환

가장 중요한 함수는 `MainWindow::showScreen(ScreenId)`다.

이 함수는 단순히 `QStackedWidget` index만 바꾸지 않는다.

실제로는:

- 화면 인덱스 변경
- 창 geometry 정책 적용
- 현재 화면 상태 기록
- `activeChannelsForScreen(screenId)` 계산
- `ChannelSessionManager::applyActiveChannels(...)` 호출
- screen transition 성능 metric 기록

을 같이 한다.

즉 **이 앱에서 화면 전환은 UI 전환과 미디어 세션 전환이 결합된 작업**이다.

## 2.6 로그아웃 / 401 / 종료

이 경계 상황은 `MainWindow`가 처리한다.

대표 경로:

- 명시적 로그아웃
- `RestClient`의 `unauthorizedDetected`
- 앱 종료

이때 해야 하는 정리는:

- auth state 해제
- WebSocket 연결 해제
- UGV 세션/서비스 정리
- 런타임 화면 해제
- 채널 세션 정리
- 로그인 화면 복귀

즉 이 프로젝트는 "그냥 로그인 화면을 띄우는 것"이 아니라,  
**세션을 정리한 뒤 로그인 화면으로 돌아가는 구조**라는 점이 중요하다.

---

## 3. 파일 분해 상태와 읽기 순서

## 3.1 왜 분해가 중요했나

이 프로젝트는 최근 스프린트에서 파일 분해가 실제 구조 개선으로 이어진 대표 사례다.

현재 대표 분해:

- `screens.h` -> umbrella header
- `mainwindow.cpp`
- `mainwindow_auth.cpp`
- `mainwindow_runtime.cpp`
- `mainwindow_navigation.cpp`
- `playback_screen.cpp`
- `playback_screen_timeline.cpp`
- `playback_screen_export.cpp`
- `channel_context_dnd_helpers.*`
- `feedback_ui_helpers.*`
- `capture_storage_helpers.*`

이 분해 덕분에:

- 인증 문제는 `mainwindow_auth.cpp`
- 화면 재생성 문제는 `mainwindow_runtime.cpp`
- geometry/navigation 문제는 `mainwindow_navigation.cpp`
- playback marker/timeline 문제는 `playback_screen_timeline.cpp`

처럼 **문제 범위를 더 빨리 좁힐 수 있게** 됐다.

## 3.2 실제 추천 읽기 순서

새 작업자가 코드를 읽는 순서는 다음이 가장 효율적이다.

1. `app_state.h`
2. `mainwindow.h`
3. `mainwindow.cpp`
4. `mainwindow_auth.cpp`
5. `mainwindow_runtime.cpp`
6. `mainwindow_navigation.cpp`
7. `common_widgets.h/.cpp`
8. `event_ui_helpers.cpp`
9. `stream_player.h/.cpp`
10. `channel_session_manager.h/.cpp`
11. `main_screen.h/.cpp`
12. `cctv_screen.h/.cpp`
13. `ugv_screen.h/.cpp`
14. `playback_screen.h/.cpp`
15. `playback_screen_timeline.cpp`
16. `playback_screen_export.cpp`

이 순서를 추천하는 이유는:

- 먼저 전역 상태와 라우팅을 보고
- 그다음 공통 UI와 미디어 모델을 보고
- 마지막에 각 화면의 개별 동작을 보는 게 이해가 가장 빠르기 때문이다.

---

## 4. 핵심 데이터 타입

## 4.1 `ScreenId`

역할:

- 현재 앱이 어떤 화면에 있는지 표현
- `showScreen(...)`의 입력
- 화면별 geometry/active channels 정책의 기준

이 enum은 단순한 화면 이름 이상이다.  
이 프로젝트에서는 "현재 어떤 채널을 살아 있게 둘지"까지 영향을 준다.

## 4.2 `MainGridCellState`

역할:

- 멀티뷰 셀 하나의 상태를 표현

의미:

- display name
- channel id
- device id
- empty 여부에 해당하는 값들

중요성:

- 예전 병렬 배열 구조를 대체하면서 멀티뷰 상태 정합성을 크게 올렸다

## 4.3 `SelectedChannelContext`

역할:

- 장치확인에서 선택된 채널의 정규화된 런타임 표현

일반적으로 이 타입은 다음 의미를 묶는다.

- device type(CCTV/UGV)
- model
- device id
- channel id
- device ip
- channel number
- display name
- RTSP/codec 매핑과 연결될 키

이 타입은:

- 장치확인
- 멀티뷰 자동 배치
- 이벤트 채널명 표기
- playback 트리 모델/채널명 구성

까지 여러 곳에서 기반 데이터처럼 쓰인다.

## 4.4 `EventInfo`

역할:

- 이벤트 UI 전반의 공통 모델

포함 의미:

- timestamp
- raw event type
- display용 type
- channel/device 정보
- preview path
- read/unread

중요한 점:

- 이벤트 검색, 알림센터, 이벤트뷰, 상세 모달이 모두 이 타입 근처에서 움직인다

## 4.5 Playback 관련 타입

대표적으로 다음 축이 있다.

- available channel summary
- timeline range
- playback marker
- playback stream result
- export request/result/status

Playback에서 중요한 건 "하나의 모델이 모든 걸 다 하지 않는다"는 점이다.  
트리용 데이터, 타임라인용 데이터, 스트림 요청 결과, export 진행 상태가 분리돼 있다.

## 4.6 UGV telemetry / ACK 타입

대표 축:

- `UgvGpsTelemetry`
- `UgvRssiTelemetry`
- `UgvCommandAck`

이 타입들은 단순 DTO가 아니다.  
UI가 무엇을 표시할지, 세션 상태를 어떻게 갱신할지, stale message를 버릴지까지 영향을 준다.

---

## 5. 루트 코어: `AppState`와 `MainWindow`

## 5.1 `AppState`

파일:

- `app_state.h`

### 책임

- 전역 런타임 상태 저장
- 화면 간 컨텍스트 전달
- 멀티뷰 초기/현재 상태 유지
- playback/UGV/CCTV 활성 타깃 기억

### 대표 필드

- 인증
  - `isAuthenticated`
  - `accessToken`
  - `currentUserId`
- 현재 화면
  - `currentScreen`
- 활성 채널/장치
  - `activeChannel`
  - `activeCctvChannelId`
  - `activeUgvGatewayId`
  - `activeUgvChannelId`
- playback 컨텍스트
  - `playbackAutoStartRequested`
  - `playbackTargetChannelId`
  - `playbackTargetDate`
- 멀티뷰
  - `gridCells[9]`
- 장치/스트림 맵
  - `selectedChannelContexts`
  - `channelRtspByName`
  - `channelRtspById`
  - `channelVideoCodecByName`
  - `channelVideoCodecById`

### 핵심 메서드

- `setGridCell(...)`
- `clearGridCell(...)`
- `clearAllGridCells()`

### 이 클래스를 볼 때 주의할 점

- 편해서 여기 넣기 시작하면 계속 비대해진다
- 필드 하나 추가하면 보통 함께 확인해야 할 곳이 있다
  - 초기화
  - 로그아웃/401 정리
  - 화면 재생성
  - 화면 복귀 시 복원 로직

즉 `AppState`는 편리하지만, 추가 수정은 늘 신중해야 한다.

## 5.2 `MainWindow`

파일:

- `mainwindow.h`
- `mainwindow.cpp`
- `mainwindow_auth.cpp`
- `mainwindow_runtime.cpp`
- `mainwindow_navigation.cpp`

### 책임

- 앱 조립 루트
- 서비스 생성 및 연결
- 화면 생성 및 라우팅
- 인증 상태와 종료 루트 관리

### 분해된 각 파일의 역할

#### `mainwindow.cpp`

- 공통 helper
- state 초기화
- 서비스 초기화 보조
- 일부 normalize helper

#### `mainwindow_auth.cpp`

- 로그인/회원가입/장치확인 연결
- 로그인 성공 후 토큰/서비스 주입
- DeviceCheck 진입
- 로그아웃/401 처리

현재 주의:

- `configureLoginScreenState()` 기본 오류 문구에 깨진 한글 문자열이 남아 있어 복구 대상이다.

#### `mainwindow_runtime.cpp`

- Main/CCTV/UGV/Playback 화면 생성
- 런타임 화면 destroy/recreate
- `rebuildRuntimeScreens()`

#### `mainwindow_navigation.cpp`

- `showScreen(...)`
- geometry 정책
- close event
- screen transition perf metric

### 핵심 함수

#### `initializeAuthServices()`

이 함수는 서비스 객체를 생성하고 공통 경로를 연결하는 출발점이다.

#### `createRuntimeScreens(...)`

의미:

- 로그인 전 화면과 달리, 인증 이후에 필요한 runtime screen을 생성
- 각 screen에 서비스 주입
- `SidebarWidget`이 있으면 `PlaybackService` 연결

#### `rebuildRuntimeScreens()`

의미:

- 설정 변경/구조 변경 후 runtime screen 전체 재구성

주의:

- screen pointer가 바뀌므로, 외부에서 오래 잡고 있던 포인터는 위험할 수 있다

#### `showScreen(ScreenId)`

의미:

- 단순 UI 전환 함수가 아니다
- active channel, geometry, metrics를 함께 처리하는 핵심 라우팅 함수

### 꼭 기억할 invariant

- 어떤 화면이 보이느냐는 단순 `QStackedWidget` index 문제가 아니다
- 현재 화면은 곧 "어떤 채널 세션을 active 상태로 둘 것인가"와 연결된다

---

## 6. 공통 UI 클래스

## 6.1 `TopbarWidget`

파일:

- `common_widgets.h/.cpp`

### 역할

- 상단 공통 헤더
- 알림/설정/로그아웃 진입점 제공
- 글로벌 상태 문구 표시

### 현재 특징

- 예전 화면별 타이틀 텍스트 중심에서, 현재는 더 미니멀한 구조로 정리됨
- 오른쪽 아이콘 액션이 주요 역할

### 주요 메서드

- `setNotificationUnread(bool)`
- `setGlobalStatusMessage(...)`
- `clearGlobalStatusMessage()`

### 주요 시그널

- `settingsClicked()`
- `notificationCenterClicked()`
- `logoutClicked()`

## 6.2 `SidebarWidget`

파일:

- `common_widgets.h/.cpp`

### 역할

- 좌측 공통 사이드바 템플릿

포함 요소:

- 채널/플레이백 탭
- channel tree
- playback tree
- 하단 action buttons
- action status label

### 중요한 이유

이 위젯은 Main/CCTV/UGV/Playback 전체의 좌측 조작 UI를 사실상 통일한다.  
그래서 "좌측 UI를 바꾸고 싶다"는 요구는 대개 여기서 해결된다.

### 주요 메서드

- `populateChannelTree()`
- `setPlaybackService(...)`
- `reloadPlaybackTree()`
- `setMiddleRegionStretch(...)`

### 읽을 때 주의할 점

- 이 클래스는 보기보다 많은 역할을 갖고 있다
- channel tree와 playback tree가 둘 다 여기 있다
- action status label 정책도 여기와 강하게 연결된다

## 6.3 `EventViewWidget`

파일:

- `common_widgets.h/.cpp`

### 역할

- 메인 우측 이벤트뷰 렌더링

### 현재 정책

- 최근 30개만 표시
- 최근 3개 카드형
- 나머지 텍스트형

### 주요 메서드

- `setEvents(events, maxItems, showDispatchButton)`

### 주의할 점

- 이 위젯은 단순 표시 위젯이 아니라, 이벤트 상세/검색/UGV dispatch UX와 연결된다
- 이벤트 burst가 많을 때는 이 위젯 자체보다 "누가 얼마나 자주 setEvents를 호출하느냐"가 더 중요하다

## 6.4 `PopupManager`

파일:

- `popup_manager.h/.cpp`

### 역할

- 공통 모달 팝업 생성

### 현재 정책에서의 위치

- 반복성/정보성 오류에는 잘 안 쓰고
- 사용자 확인이 필요한 경우 또는 경로/파일 같은 조치형 오류에 쓴다

즉 이 클래스는 "아무 오류나 띄우는 공용 도구"가 아니라,  
**모달이 정말 필요한 상황에만 쓰는 쪽으로 정책이 정리된 상태**다.

## 6.5 `SettingsDialog`

파일:

- `settings_dialog.h/.cpp`

### 역할

- 장치관리
- 저장 경로
- 정책 설정

### 현재 특징

- 560x380 compact modal
- 장치 저장은 `TeamClue/VMS_v1`
- 저장 경로는 `TeamClue/VMS_v2`
- ONVIF 검색 경로는 현재 미노출 처리

### 왜 중요한가

설정창은 단순 옵션 UI가 아니라:

- 장치 목록 로컬 관리
- 저장 디렉터리 정책
- 일부 서비스/화면 재구성 트리거

와 연결되는 지점이다.

## 6.6 `EventUiHelpers`

파일:

- `event_ui_helpers.cpp`

### 역할

- 이벤트 관련 UI 로직을 화면 클래스에서 떼어낸 helper 집합

### 맡는 일

- 이벤트 타입 표시명 매핑
- 채널 표시명 구성
- 이벤트 상세 다이얼로그
- 이벤트 검색 다이얼로그
- 알림센터 다이얼로그
- unread 처리 일부

현재 정책(중요):

- 알림센터: 최근 24시간 고정, 컬럼(시간/채널/이벤트 종류) 정렬형 목록
- 이벤트 검색: 필터 1줄 + 정렬형 결과표
- 이벤트 상세: `allowDispatch` 플래그로 UGV 출동 버튼 노출 여부를 분기

### 왜 중요한가

이 helper 덕분에 이벤트 표현 규칙이 MainScreen에 박혀 있지 않다.  
즉 이벤트 표시 정책을 바꿀 때 한 군데에서 작업하기 훨씬 쉬워졌다.

---

## 7. 화면 클래스

## 7.1 `LoginScreen`

파일:

- `login_screen.h/.cpp`

### 역할

- 로그인 화면 UI

### 주요 시그널

- `loginRequested`
- `signupRequested`

### 특징

- 크기: 400x380
- compact 인증 화면의 시작점

## 7.2 `SignupScreen`

파일:

- `login_screen.h/.cpp`

### 역할

- 회원가입 화면 UI

### 특징

- 크기: 400x460

## 7.3 `DeviceCheckScreen`

파일:

- `login_screen.h/.cpp`

### 역할

- 장치/채널 선택
- 연결 상태 확인
- Main 진입 전 컨텍스트 구성

### 주요 동작

- `reloadDevices(retryCount=0)`
- `applyDeviceTree(...)`
- `selectedContexts()`

### 최근 안정화 포인트

- 첫 진입은 `showScreen(DeviceCheck)` 후 300ms 뒤 시작
- `Operation canceled`는 1회 자동 재시도
- 채널 fetch 동시성 cap = 4
- 채널 fetch 1회 retry

### 주의할 점

- 네트워크는 비동기지만, 결과 UI 구성은 메인 스레드
- 장치 수가 많을수록 tree build 부담이 커질 수 있다

## 7.4 `MainScreen`

파일:

- `main_screen.h/.cpp`

### 역할

- 멀티뷰 메인 화면
- 이벤트뷰
- 스냅샷/클립
- CCTV fullscreen 진입
- UGV dispatch 출발점

### 내부 핵심 객체

- `VideoCellWidget`
- `EventViewWidget`
- `SidebarWidget`

### 핵심 포인트

- 멀티뷰 상태는 `AppState.gridCells`
- drop/double click/cell delete가 모두 grid cell 상태와 연결
- Main은 CCTV 중심 멀티뷰, UGV는 dispatch 경로 중심으로 분리된 정책을 가진다

### 중요 메서드

- `refreshGridFromState()`
- `bindCellToState(...)`
- `handleCellDoubleClick(...)`
- `showEventDetail(...)`

### 주의할 점

- Main은 화면처럼 보여도 실제로는 이벤트/클립/dispatch가 함께 얽힌 조정자다

## 7.5 `CctvScreen`

파일:

- `cctv_screen.h/.cpp`

### 역할

- 단일 CCTV fullscreen
- zoom/focus control
- snapshot/clip

### 주요 동작

- `refreshStream()`
- `refreshOsd()`
- `submitZoomStep(...)`
- `submitFocusStep(...)`

### 현재 정책

- fullscreen도 fit 기반 렌더
- OSD는 멀티뷰와 유사한 상단 배치
- zoom/focus 실패는 상태라벨 중심

### 주의할 점

- 실제 스트림 지연은 화면 전환보다 카메라/GOP 특성에 더 많이 영향을 받는다

## 7.6 `UgvScreen`

파일:

- `ugv_screen.h`
- `ugv_screen.cpp`
- `ugv_screen_commands.cpp`
- `ugv_screen_feedback.cpp`
- `ugv_screen_map.cpp`

### 역할

- UGV fullscreen
- drive control
- PTZ control
- map/telemetry/feedback 표시

### 구조를 볼 때의 포인트

이 화면은 하나처럼 보이지만 실제로는 세 축으로 나뉜다.

- 비디오
- 컨트롤
- telemetry / feedback / map

### 주요 동작

- 서비스 연결: `setUgvService(...)`
- 세션 UI 갱신: `updateSessionUi()`
- 사이드바 상태: `refreshSidebarStatus()`
- 명령 전송: `sendDriveCommand(...)`, `sendPtzCommand(...)`
- telemetry 반영: `updateMapTelemetry(...)`

### 현재 UX 정책

- 사이드바는 공통 구조 유지
- 정보 패널은 영상 쪽 오버레이/근접 배치
- 드라이브와 PTZ는 connected 상태에서만 동작
- disabled 스타일을 명확히 둠
- PTZ 방향키는 press/release 반복 전송

### 주의할 점

- UGV 문제는 UI보다 서비스/환경/telemetry 주기를 함께 봐야 한다
- 지도/영상/telemetry가 한 화면에 있어 메인 스레드 부하에 민감하다

## 7.7 `PlaybackScreen`

파일:

- `playback_screen.h`
- `playback_screen.cpp`
- `playback_screen_timeline.cpp`
- `playback_screen_export.cpp`
- `playback_screen_helpers.h`

### 역할

- 날짜/채널 기반 이전영상 재생
- timeline + marker 표시
- event marker jump
- export

### 왜 분리됐는가

Playback는 책임이 명확히 세 갈래라 분리가 필수였다.

- `playback_screen.cpp`
  - UI 골격, service 주입, show/resize, snackbar
- `playback_screen_timeline.cpp`
  - available range
  - marker
  - 현재 위치 계산
  - stream 요청
- `playback_screen_export.cpp`
  - export dialog
  - request/poll/download

### 현재 timeline 정책

- 24시간 축 표시
- slider는 사실상 표시용
- 트리 클릭 -> 기본 playback 시작점부터 재생
- marker 클릭 -> marker 시각부터 재생
- marker는 3분 클러스터링
- playable range는 envelope orange bar로 표현

### 관련 상태 필드

- `m_timelineSpanStartMs`
- `m_timelineSpanEndMs`
- `m_selectedTimelinePositionMs`
- `m_playbackStartTimelinePositionMs`
- `m_currentTimelinePositionMs`
- `m_playbackStartWallclockMs`

### 주의할 점

- Playback는 "표현"을 건드리면 "동작"도 같이 흔들리기 쉬운 영역이다
- timeline 계산과 UX 변경은 반드시 작은 단계로 나눠야 한다

---

## 8. 서비스 / 인프라 / 미디어 클래스

## 8.1 `RestClient`

파일:

- `rest_client.h/.cpp`

### 역할

- 공통 HTTP JSON 클라이언트

### 맡는 일

- base URL
- access token header
- timeout
- JSON request/response
- 401 detection

### 왜 중요한가

서비스 레이어가 endpoint 의미에 집중할 수 있는 이유가 이 클래스 때문이다.

## 8.2 `WsClient`

파일:

- `ws_client.h/.cpp`

### 역할

- 인증된 WebSocket 연결
- reconnect/heartbeat

### 왜 중요한가

EventService와 실시간 알림 흐름이 이 객체에 의존한다.

## 8.3 `AuthService`

### 역할

- login/signup/logout

### 특징

- 응답 payload에서 access token, user id, error message를 추출하는 helper 성격도 가진다

현재 주의:

- `logout()` 경로의 fallback/기본 오류 문구 일부에 깨진 한글이 남아 있어 복구가 필요하다.

## 8.4 `DeviceService`

### 역할

- 장치 목록
- 장치별 채널 목록
- 채널 상세

### 왜 중요한가

DeviceCheck만의 서비스가 아니라, 런타임에서 RTSP/codec detail 정규화의 출발점이기도 하다.

## 8.5 `EventService`

### 역할

- 최근 이벤트 fetch
- 상세 이벤트 fetch
- WS 이벤트 ingest
- dedupe / unread / cache trim

### 핵심 포인트

- `kMaxCachedEvents = 200`
- 최근 이벤트 검색은 서버 전체 검색이 아니라 현재 캐시에 대해 local filtering하는 구조와 연결된다

## 8.6 `PlaybackService`

### 역할

- playback 가능 채널 조회
- timeline 조회
- stream URL 요청
- export 요청/상태 조회

### 주의할 점

- 이 서비스 자체보다, 응답을 PlaybackScreen이 어떻게 UI로 푸느냐가 더 자주 문제의 원인이 된다

## 8.7 `CctvControlService`

### 역할

- zoom/focus step 제어

### 특징

- 지원 step 값 검증(`-100, -10, -1, 1, 10, 100`) 같은 정책이 들어 있다

## 8.8 `UgvService`

### 역할

- UGV 세션 관리
- drive/PTZ 명령
- telemetry
- ACK/timeout/error 처리

### 상태 모델

- `Disconnected`
- `SocketConnecting`
- `SocketConnected`
- `ConnectingUgv`
- `ConnectedUgv`
- `DisconnectingUgv`
- `Error`

### 왜 중요한가

이 서비스는 UI보다 "계약의 중심"이다.  
UGV 화면이나 dispatch 흐름을 건드릴 때는 반드시 이 서비스를 먼저 봐야 한다.

## 8.9 `StreamPlayer`

파일:

- `stream_player.h/.cpp`

### 역할

- GStreamer pipeline 생명주기
- render widget 바인딩
- 재생 상태/위치/duration 제공

### 주요 메서드

- `setSource(...)`
- `start()`
- `stop()`
- `setPaused(...)`
- `seekToMs(...)`
- `positionMs()`
- `durationMs()`

### 왜 중요한가

화면은 이 객체를 직접 소유하지 않아도, 결국 재생 위치/상태/오류는 이 객체에서 나온다.

## 8.10 `ChannelSessionManager`

파일:

- `channel_session_manager.h/.cpp`

### 역할

- 채널별 `StreamPlayer` 세션을 재사용/관리

### 주요 메서드

- `bindChannelToWidget(...)`
- `unbindChannelFromWidget(...)`
- `applyActiveChannels(...)`
- `shutdown()`

### 가장 중요한 의미

이 앱에서 "어떤 채널이 지금 실제로 살아 있어야 하는가"를 결정하는 중앙 조정자다.

## 8.11 `VideoRenderWidget`

파일:

- `video_render_widget.h/.cpp`

### 역할

- `QOpenGLWidget` 기반 프레임 렌더
- FPS / first-frame metric 기록

### 주의할 점

- OSD와 네이티브 윈도우/overlay 문제는 이 위젯 또는 그 부모 구조와 강하게 연결된다

## 8.12 `ClipCaptureManager`

파일:

- `clip_capture_manager.h/.cpp`

### 역할

- 클립 녹화/인코딩 state machine

### 상태

- Idle
- Recording
- Encoding

### 특징

- snapshot/clip UI와 결합되지만, 실제 인코딩 상태 모델은 이 클래스가 책임진다

---

## 9. 공통 helper 모듈

## 9.1 `channel_context_dnd_helpers.*`

역할:

- displayName -> channelId 매핑
- RTSP/codec lookup
- drop payload 해석
- active CCTV target 정규화

왜 중요한가:

- 멀티뷰 drop, CCTV fullscreen 진입, RTSP lookup이 이 helper에서 많이 만난다

## 9.2 `feedback_ui_helpers.*`

역할:

- `showActionStatus(...)`
- `showToastMessage(...)`
- `showPersistentStatusMessage(...)`

현재 정책:

- 반복성/정보성 안내는 상태라벨/토스트
- 모달은 선택/조치가 필요한 경우만

## 9.3 `capture_storage_helpers.*`

역할:

- snapshot/clip 저장 경로
- 실제 파일 저장 helper
- clip encode failure 분기

특이점:

- `QSettings("TeamClue", "VMS_v2")`와 v1 lazy migration이 들어 있다

## 9.4 `common_ui.*`

역할:

- 공통 헤더성 helper 집합

주의:

- umbrella 성격이 있어 include는 편하지만, 의존을 여기로 너무 몰지 않도록 보는 게 좋다

## 9.5 `playback_screen_helpers.h`

역할:

- playback timeline/export에서 공통으로 쓰는 타입/보조 계산의 집합

왜 중요한가:

- Playback 로직을 다시 뭉치지 않게 하는 완충 지점이다

---

## 10. 실제 호출 체인

## 10.1 인증 체인

`LoginScreen`
-> `AuthService::login(...)`
-> 성공 시 `MainWindow`가 token/service state 주입
-> `showScreen(DeviceCheck)`
-> delayed `refreshDevices()`

## 10.2 런타임 화면 생성 체인

`DeviceCheckScreen::startRequested`
-> channel detail 정규화
-> `AppState` RTSP/codec/context 맵 구성
-> `createRuntimeScreens(...)`
-> `showScreen(Main)`

## 10.3 화면 전환 + 미디어 활성화 체인

`MainWindow::showScreen(screenId)`
-> geometry 적용
-> stacked widget index 변경
-> `activeChannelsForScreen(...)`
-> `ChannelSessionManager::applyActiveChannels(...)`

## 10.4 멀티뷰 셀 체인

셀 클릭/드롭/더블클릭
-> `AppState.gridCells` 변경
-> `VideoCellWidget` 표시 갱신
-> `ChannelSessionManager::bindChannelToWidget(...)`

## 10.5 CCTV fullscreen 체인

Main cell double click
-> `AppState.activeCctvChannelId` 설정
-> `showScreen(Cctv)`
-> `CctvScreen::refreshStream()`
-> `ChannelSessionManager::bindChannelToWidget(...)`

## 10.6 UGV dispatch 체인

이벤트 상세 `UGV 출동`
-> UGV target/gateway 식별
-> `AppState.activeUgv...` 설정
-> `showScreen(Ugv)`
-> `UgvService::connectUgv(...)`
-> telemetry / ACK / feedback 반영

## 10.7 Playback 체인

playback tree item activate
-> `startPlaybackForChannel(...)`
-> `loadTimelineForChannel(...)`
-> `PlaybackService::fetchTimeline(...)`
-> `applyTimelineResult(...)`
-> `requestPlaybackStream(...)`
-> `StreamPlayer::setSource/start`

event marker click
-> 해당 marker timestamp
-> `requestPlaybackStream(timestamp -> to)`

## 10.8 Snapshot / Clip 체인

버튼 클릭
-> save helper 또는 capture manager
-> 상태라벨/버튼 텍스트 갱신
-> 필요 시 `PopupManager`는 경로/실파일 실패에만 사용

---

## 11. 디버깅할 때 어디를 보면 좋은가

## 11.1 로그인은 되는데 장치확인이 비어 보일 때

우선 확인:

- `mainwindow_auth.cpp`의 login success 경로
- `DeviceCheckScreen::reloadDevices(...)`
- `DeviceService::fetchDevices(...)`
- `DeviceService::fetchDeviceChannels(...)`
- `applyDeviceTree(...)`

핵심 질문:

- 장치 목록이 안 오는가
- 채널 fan-out이 일부 실패하는가
- UI 적용이 늦는가

## 11.2 화면 전환은 됐는데 영상이 안 나올 때

우선 확인:

- `MainWindow::showScreen(...)`
- `activeChannelsForScreen(...)`
- `ChannelSessionManager::applyActiveChannels(...)`
- 해당 screen의 `refreshStream()`
- `StreamPlayer::setSource(...)`

핵심 질문:

- active channel 자체가 비어 있는가
- RTSP/codec lookup이 비어 있는가
- 세션은 살아 있는데 widget binding이 안 된 것인가

## 11.3 Playback가 이상할 때

우선 확인:

- `PlaybackService::fetchTimeline(...)`
- `applyTimelineResult(...)`
- `refreshTimelineUi()`
- `rebuildEventMarkers()`
- `requestPlaybackStream(...)`

핵심 질문:

- 타임라인 데이터가 문제인가
- marker 수가 과한가
- current position 계산이 흔들리는가
- 실제 stream 요청은 성공했는가

## 11.4 UGV가 이상할 때

우선 확인:

- `UgvService::sessionState()`
- `connectUgv(...)`
- `sendDrive(...)` / `sendPtz(...)`
- `handleAckMessage(...)`
- `handleTelemetryMessage(...)`
- `UgvScreen::updateSessionUi()`

핵심 질문:

- UI가 disabled라서 안 되는가
- 서비스가 disconnected인가
- pending ACK가 안 지워지는가
- telemetry는 오는데 UI가 못 따라오는가

## 11.5 성능이 이상할 때

기본 metric:

- `screen_transition_ms`
- `first_frame_after_transition_ms`
- `first_live_frame_after_transition_ms`
- `render_fps`
- `active_apply_ms`
- `clip_total_ms`

읽는 법:

- 전환은 빠른데 체감이 느리면 `first_live_frame_after_transition_ms`
- 멀티뷰 전환이 느리면 `active_apply_ms`
- 녹화/저장 쪽 체감이 느리면 `clip_total_ms`

---

## 12. 안전하게 수정하는 방법

## 12.1 새 서비스 endpoint 추가

순서:

1. `RestClient`를 직접 화면에서 호출하지 말고
2. 해당 서비스 클래스에 메서드 추가
3. parse helper 분리
4. screen에서는 service만 호출

이 원칙을 지키면 통신 정책이 흩어지지 않는다.

## 12.2 새 화면 추가

순서:

1. screen class 생성
2. `MainWindow`가 생성/보유/주입
3. `showScreen`과 geometry 정책에 연결
4. active channel 정책이 필요하면 `activeChannelsForScreen`에 반영

즉 새 화면은 UI 클래스만 추가하면 끝나는 게 아니라,  
**라우팅과 미디어 활성화 정책까지 함께 연결해야 완성**된다.

## 12.3 새 이벤트 타입 추가

가장 먼저 볼 곳:

- `event_ui_helpers.cpp`

수정 포인트:

- `displayEventTypeName(...)`
- `detailedEventTypeLabel(...)`
- 필요하면 검색/상세 표현

이벤트 타입 매핑을 여기서 모아 관리하는 이유는, 화면마다 raw type을 따로 해석하지 않게 하기 위해서다.

## 12.4 Playback를 수정할 때

규칙:

- "표현"과 "동작"을 동시에 크게 바꾸지 않는다
- 타임라인 상태 필드 의미를 먼저 문서로 확인한다
- marker / playable range / current position을 한 번에 바꾸지 않는다

Playback는 작은 수정도 쉽게 꼬이는 영역이라, 반드시 단계적으로 간다.

## 12.5 UGV를 수정할 때

규칙:

- UI만 보지 말고 `UgvService`부터 같이 본다
- ACK/timeout/stale 메시지 정책을 먼저 이해한다
- 연결 안 된 상태에서 버튼을 활성화하지 않는다
- drive/PTZ hold 동작은 press/release와 session state를 같이 본다

UGV는 "버튼이 안 먹는다"가 UI 문제일 수도 있지만, 서비스 state 문제일 확률도 매우 높다.

---

## 13. 꼭 기억할 현재 정책들

- 로그인 화면: `400x380`
- 회원가입 화면: `400x460`
- 장치확인 화면: `560x380`
- DeviceCheck 첫 로드:
  - 화면 전환 후 300ms 뒤 refresh
  - `Operation canceled`는 자동 재시도
  - 채널 fan-out 동시성 cap 4
- `EventService` 캐시 상한: 200
- Playback marker clustering: 3분
- playback slider: 표시용 중심, marker click 재생 유지
- 알림센터: 최근 24시간 고정 목록(필터 버튼 미사용)
- 장치 저장: `TeamClue/VMS_v1`
- 저장 경로: `TeamClue/VMS_v2`
- 앱/윈도우 아이콘: `:/styles/clue_logomark.svg`

이건 세부 구현보다 운영 정책에 가까운 사실이라, 기능 수정 시 쉽게 흔들지 않는 것이 좋다.

---

## 14. 마지막 체크리스트

이 문서를 읽고 다음 질문에 답할 수 있으면 구조를 꽤 잘 이해한 상태다.

- 로그인 후 왜 바로 Main이 아니라 DeviceCheck로 가는가
- 왜 `showScreen(...)`이 단순 UI 전환 함수가 아닌가
- 멀티뷰 셀 상태는 어디에 저장되는가
- 이벤트 상세 채널명은 어디서 조합되는가
- playback marker 클릭은 어디서 stream 재요청으로 이어지는가
- UGV drive/PTZ 명령은 어떤 상태에서만 나가는가
- 스냅샷/클립 실패가 언제 상태라벨이고 언제 팝업인가

이 질문들에 답할 수 있다면, 새 기능 추가도 훨씬 안전하게 할 수 있다.

---

## 15. 함께 읽으면 좋은 문서

- `docs/VMS_v2_code_assessment.md`
- `docs/VMS_v2_implementation_history.md`
- `docs/VMS_v2_post_phase8_execution_plan.md`
- `docs/VMS_v2_popup_toast_status_matrix.md`
- `docs/VMS_v2_performance_tracking.md`
- `docs/subfiles/VMS_v2_architecture.md`
- `docs/troubleshooting/troubleshooting_multiview_streaming.md`

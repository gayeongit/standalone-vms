# VMS v2 코드 평가 보고서

- 최종 갱신: 2026-03-23
- 검토 범위: `VMS_v2` 현재 코드베이스(`*.h`, `*.cpp`, `styles/*.qss`)와 관련 운영 문서
- 목적: 인수인계, 구조 판단, 리팩토링 우선순위 결정, 이후 스프린트의 안정적인 작업 범위 정의

---

## 1. 이 문서가 보는 관점

이 문서는 단순히 "코드가 깔끔한가"만 평가하지 않는다. 실제 운영 관점에서 아래를 함께 본다.

- 현재 구조가 기능 추가에 얼마나 버티는가
- 화면/서비스/미디어/전역 상태가 어떤 방식으로 엮여 있는가
- 성능 문제가 다시 생기기 쉬운 지점이 어디인가
- 누가 다음 작업을 잡아도 덜 흔들리게 만들려면 무엇을 먼저 정리해야 하는가

즉 이 문서는 점수표보다 **코드를 다룰 때의 체감 난이도와 리스크 지도**에 가깝다.

---

## 2. 한 줄 총평

현재 `VMS_v2`는 **운영 가능한 수준의 Qt/GStreamer 기반 클라이언트**이며, 최근 스프린트에서 구조와 UX 정책이 빠르게 정리되면서 "거대한 실험 코드" 단계는 이미 벗어났다.  
특히 다음 축은 분명히 좋아졌다.

- `MainWindow`와 `PlaybackScreen`의 구현 분해
- `AppState`의 멀티뷰 상태 정규화
- `StreamPlayer` / `ChannelSessionManager` 중심의 미디어 세션 재사용 모델
- `Popup / Toast / StatusLabel` 정책 정리
- Playback 타임라인 안정화(`PB-1`~`PB-3`)
- DeviceCheck 첫 진입 안정화

반대로, 아직 이 코드베이스를 "장기적으로 편하게 유지보수 가능한 상태"라고 보기는 어렵다.  
남아 있는 핵심 리스크는 다음에 모여 있다.

- `AppState` 싱글턴이 너무 많은 의미를 들고 있음
- 이벤트/플레이백/장치확인 UI가 여전히 메인 스레드에 무거운 작업을 몰아줄 수 있음
- 테스트 자동화가 사실상 없어서 회귀 방어가 약함
- 설정/장치 저장 정책이 `VMS_v1`과 `VMS_v2`를 병행 사용함
- Playback 마커 렌더링은 응급 안정화는 됐지만 구조적으로는 아직 무거움

요약하면:

> **지금 코드는 "실서비스형 클라이언트로 굴릴 수 있는 상태"이며, 다음 단계는 기능 추가보다 구조적 부하를 줄이는 정리 작업이 더 중요하다.**

---

## 3. 종합 등급

### 3.1 현재 등급

- 아키텍처 일관성: **A-**
- 기능 분리/책임 분리: **B+**
- 운영 정합성(서비스 계약, 에러 흐름, ACK 처리): **A-**
- 미디어/세션 구조: **B+**
- UI 정책 일관성: **B+**
- 성능 관측 가능성: **A-**
- 테스트 가능성/회귀 방어: **C**
- 장기 유지보수 편의성: **B**

### 3.2 등급 해석

- `A`를 못 준 이유는 구조가 나빠서가 아니라, **전역 상태 범위와 자동 테스트 부재** 때문이다.
- `C` 이하로 내려가지 않는 이유는, 최근 정리 덕분에 실제 런타임 흐름은 꽤 명확하고, 성능/운영 지표도 이미 코드에 심어져 있기 때문이다.

---

## 4. 코드베이스 스냅샷

## 4.1 현재 구조를 한 문장으로 요약하면

`MainWindow`가 서비스와 화면을 조립하고, `AppState`가 런타임 컨텍스트를 공유하며, `ChannelSessionManager`가 실제 미디어 세션 생명주기를 관리하는 구조다.

## 4.2 레이어 관점 구조

- **UI Layer**
  - 화면: `LoginScreen`, `SignupScreen`, `DeviceCheckScreen`, `MainScreen`, `CctvScreen`, `UgvScreen`, `PlaybackScreen`
  - 공통 위젯: `TopbarWidget`, `SidebarWidget`, `EventViewWidget`
  - 공통 UI 유틸: `PopupManager`, `EventUiHelpers`, `SettingsDialog`, `feedback_ui_helpers`, `capture_storage_helpers`

- **Application Service Layer**
  - `AuthService`
  - `DeviceService`
  - `EventService`
  - `PlaybackService`
  - `CctvControlService`
  - `UgvService`

- **Infrastructure Layer**
  - `RestClient`
  - `WsClient`

- **Media Layer**
  - `StreamPlayer`
  - `VideoRenderWidget`
  - `ChannelSessionManager`
  - `ClipCaptureManager`

- **State Layer**
  - `AppState` singleton
  - 타입 계층: `SelectedChannelContext`, `EventInfo`, `Playback*`, `Ugv*Telemetry`, `MainGridCellState`

## 4.3 최근 구조 개선이 의미 있는 이유

이 프로젝트는 한때 `screens.h`, `mainwindow.cpp`, `playback_screen.cpp`가 사실상 "거대 통합 파일" 역할을 하던 단계에 가까웠다.  
지금은 다음처럼 분해가 실제 코드로 반영됐다.

- `screens.h` -> umbrella include
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

이건 단순히 파일 수가 늘어난 게 아니라, **"어디를 고치면 어떤 성격의 문제가 나오는가"를 예측할 수 있게 만든 변화**다.

---

## 5. 좋은 점: 이 코드가 이미 잘하고 있는 것

## 5.1 루트 조립 구조가 한 군데로 모여 있다

`MainWindow`는 여전히 무겁지만, 이 무게의 상당 부분이 "좋은 무게"다.  
왜냐하면 서비스 생성, 화면 전환, 인증 상태, 런타임 화면 재구성을 한 군데에서 관리하기 때문이다.

장점:

- 앱 시작 시점이 명확함
- 인증 이후 어떤 서비스에 어떤 토큰이 주입되는지 추적 가능함
- 런타임 화면 재구성(`rebuildRuntimeScreens`)이 공통 경로를 가짐
- `showScreen(...)`에서 화면 전환 + active channel 적용 + perf metric 기록이 함께 일어남

특히 `showScreen(...) -> ChannelSessionManager::applyActiveChannels(...)` 연결은 이 코드베이스의 핵심 설계 포인트다.  
화면만 바꾸는 게 아니라, **현재 화면에 필요한 스트림만 살아 있도록 미디어 세션을 함께 정리한다.**

## 5.2 멀티뷰 상태를 병렬 배열에서 구조체 배열로 바꾼 건 매우 큰 개선이다

이전 병렬 배열(`displayName`, `channelId`, `deviceId`를 따로 들고 있던 방식`)은 UI와 상태가 어긋나기 가장 쉬운 형태다.  
현재는 `AppState.gridCells`의 `MainGridCellState`로 정리돼 있어서:

- 셀 상태 읽기/쓰기 단위가 명확하고
- 빈 셀 / 채널명 / 장치 아이디 / RTSP 대응을 한 객체로 볼 수 있고
- 선택 셀, DnD, 자동 배치, 삭제 동작의 정합성이 좋아졌다

이건 멀티뷰 기능이 있는 앱에서 장기 유지보수 난이도를 크게 낮추는 변화다.

## 5.3 서비스 레이어 경계가 비교적 명확하다

각 서비스의 역할이 겹치지 않는다.

- `AuthService`: 인증
- `DeviceService`: 장치/채널 조회
- `EventService`: 최근 이벤트, 상세 이벤트, WS 이벤트 ingest
- `PlaybackService`: playback 가능 채널/타임라인/재생/export
- `CctvControlService`: zoom/focus step 제어
- `UgvService`: UGV 세션/명령/telemetry/ACK

이 정도 경계만 유지돼도, 다음 작업자가 "어디에 기능을 넣어야 하는지"를 판단하기가 훨씬 쉽다.

## 5.4 `RestClient`와 `WsClient`로 통신 정책이 중앙화되어 있다

`RestClient`는 다음을 공통 정책으로 묶는다.

- base URL
- access token
- timeout
- 401 처리
- JSON 요청/응답 표준화

`WsClient`는 다음을 담당한다.

- bearer 인증 연결
- ping/pong
- reconnect
- raw/parsed message forwarding

이렇게 인프라 계층을 따로 둔 건, 서비스 파일이 endpoint 의미와 파싱에 집중하게 해준다.  
만약 이 프로젝트가 각 화면에서 직접 `QNetworkAccessManager`를 만지는 구조였다면, 지금보다 구조 품질은 훨씬 낮았을 것이다.

## 5.5 미디어 세션 재사용 구조가 잘 잡혀 있다

`ChannelSessionManager` + `StreamPlayer` 조합은 이 코드베이스의 가장 좋은 설계 중 하나다.

의미:

- 채널 단위로 세션을 재사용할 수 있음
- 여러 화면/여러 위젯이 같은 채널을 공유 바인딩할 수 있음
- 화면 전환 때 전체 재연결 대신 active channel diff 적용이 가능함
- 품질 프로필과 render host 구성이 세션 중심으로 관리됨

특히 `applyActiveChannels(...)`가 있다는 점은 중요하다.  
이 함수 덕분에 "지금 화면에서 필요 없는 스트림을 바로 정리"할 수 있어서, 구조적으로도 성능적으로도 훨씬 건강하다.

## 5.6 성능 계측이 실제로 useful 하다

이 프로젝트는 perf metric이 단순 장식이 아니다. 실제로 병목 판단에 도움이 되는 지표가 들어 있다.

- `screen_transition_ms`
- `first_frame_after_transition_ms`
- `first_live_frame_after_transition_ms`
- `render_fps`
- `active_apply_ms`
- `clip_total_ms`

이 조합이 좋은 이유:

- 화면 전환 시간이 느린 건지
- 첫 프레임이 늦는 건지
- 진짜 live frame이 늦는 건지
- clip 인코딩이 막히는 건지

를 분리해서 볼 수 있다.  
운영형 미디어 앱에서 이 정도 계측을 코드에 심어둔 건 꽤 큰 장점이다.

## 5.7 UGV 서비스는 "어려운 문제"를 비교적 제대로 풀고 있다

`UgvService`는 이 프로젝트에서 가장 까다로운 서비스인데, 현재 구조는 꽤 안정적이다.

좋은 점:

- 세션 상태가 enum으로 분리돼 있음
- ACK와 error 경로가 분리돼 있음
- `msgId` 기반 pending ACK 관리가 있음
- stale/late message를 무시하는 정책이 들어 있음
- 연결 붕괴형 에러와 명령 실패형 에러를 구분하려는 의도가 보임

UGV는 단순 REST 조회가 아니라 socket session + command + telemetry + timeout + ACK라서 버그가 나기 쉬운 영역인데, 현재 코드는 최소한 "어디서 문제가 날 수 있는지"는 드러나는 구조다.

## 5.8 UX 정책이 정리되면서 코드 유지비도 함께 줄었다

최근 팝업/토스트/상태라벨 정책 정리는 단순 디자인 수정이 아니다.  
이 작업은 유지보수성 측면에서도 의미가 크다.

이전:

- 같은 성격의 실패가 어떤 화면에서는 팝업, 어떤 화면에서는 상태라벨
- 같은 실패를 팝업 + 상태라벨로 이중 알림
- 인수인계 시 "왜 여긴 팝업이지?"를 계속 설명해야 함

현재:

- 정보성/반복성 실패는 상태라벨/토스트
- 확인이 필요한 경우만 모달
- Export도 일반 실패와 경로/파일 실패를 나눔

이건 코드를 읽는 사람 입장에서 상당히 큰 정리다.

---

## 6. 약점: 지금 구조에서 조심해야 하는 부분

## 6.1 `AppState`는 여전히 너무 넓다

`AppState`가 들고 있는 정보는 많고, 역할도 넓다.

- 인증 정보
- 현재 화면
- 활성 CCTV/UGV
- 멀티뷰 셀 상태
- 선택 채널 컨텍스트
- RTSP/codec lookup
- playback auto-start 관련 값

이게 당장은 편하다. 하지만 장기적으로는 다음 문제가 생기기 쉽다.

- 어떤 필드가 진짜 source of truth인지 헷갈림
- 로그아웃/재진입/401 처리에서 일부 필드를 놓치기 쉬움
- 화면 간 state coupling이 커짐

현재는 `clearAuthenticationState()`와 초기화 루트가 비교적 잘 잡혀 있어서 운영은 가능하지만,  
장기적으로는 다음 정도의 분리가 필요하다.

- auth/session state
- multiview routing state
- playback state
- selected device/channel catalog

즉 `AppState`는 현재 단계에서는 실용적이지만, 다음 큰 기능 추가 전에는 분리 방향을 고민해야 한다.

## 6.2 DeviceCheck는 네트워크는 비동기지만 결과 처리는 메인 스레드 집중형이다

최근 안정화는 많이 됐지만, 구조적으로는 아직 메인 스레드에 부담이 모일 수 있다.

현재 첫 진입 경로:

- `showScreen(DeviceCheck)` 먼저
- `QTimer::singleShot(300, ...)` 뒤 `refreshDevices()`
- `reloadDevices(retryCount=0)`
- `Operation canceled`는 자동 재시도
- 장치별 채널 조회는 동시성 4 + 1회 재시도

이건 분명 좋은 개선이다.  
하지만 결국:

- 장치 목록 결과 수신
- 채널 fan-out 콜백 수신
- 트리 조립
- `applyDeviceTree(...)`

는 UI 스레드에서 실행된다.

즉 "네트워크 동기 호출이라서" 버벅이는 건 아니지만,  
**콜백 이후 UI 구성 작업이 몰리면 spinner가 버벅일 여지**는 여전히 있다.

## 6.3 이벤트 UI는 아직도 전체 rebuild 성향이 강하다

메인 이벤트뷰는 최근에 debounce와 캐시를 넣어서 많이 나아졌지만, 구조 자체는 여전히 "현재 이벤트 목록을 받아 다시 그리는" 성향이 남아 있다.

리스크:

- 이벤트 burst 시 위젯 재생성이 많아질 수 있음
- card/thumbnail/path load가 다시 부담이 될 수 있음

운영상 크게 터지지 않을 수는 있어도, 구조적으로는 incremental update 모델이 더 건강하다.

## 6.4 Playback는 안정화됐지만 아직 완전히 가볍진 않다

Playback는 최근 `PB-1`~`PB-3` 덕분에 많이 좋아졌다.

좋아진 점:

- 타임라인 상태 계산 단순화
- slider seek 정책 명확화
- marker overlay 제거
- current position 표시 복구
- marker clustering(3분)
- 24시간 트랙/재생 가능 구간/마커 레이어 표현 재정리

그럼에도 남는 부채:

- marker가 아직 `QPushButton` 기반
- playable range는 envelope 표현
- 타임라인은 여전히 UI와 상태 계산이 가까움

즉 "이제 쓸 수는 있는데, 대량 marker 상황에서 구조적으로 가장 싼 설계"는 아니다.

## 6.5 설정 저장 정책이 이중 네임스페이스를 쓴다

현재 저장소 정책:

- 장치 관리: `QSettings("TeamClue", "VMS_v1")`
- 저장 경로 등 신규 설정: `QSettings("TeamClue", "VMS_v2")`

이건 지금 시점에서는 의도된 타협이다.  
이전 사용자 데이터를 깨지 않으면서 v2 정책을 도입하려는 흔적이다.

하지만 이 방식이 계속 길어지면 생기는 문제:

- "왜 장치는 v1이고 경로는 v2지?"를 매번 설명해야 함
- 마이그레이션 정책이 문서 없이 암묵화되기 쉬움
- 다음 설정 추가 때 판단이 흔들릴 수 있음

즉 지금은 괜찮지만, **정책을 문서로 못 박아두지 않으면 향후 혼란이 커질 수 있다.**

## 6.6 문자열/인코딩은 아직도 운영 리스크다

최근 여러 파일에서 한글 깨짐을 복구한 이력이 있고, 현재도 일부 경로에 깨진 문자열이 남아 있다.

이 리스크는 기능 버그만큼 무섭다.

- 팝업/상태라벨 문구가 깨지면 신뢰도가 바로 떨어짐
- 문서와 코드 사이 문구가 달라지기 쉬움
- 터미널 인코딩 문제와 파일 인코딩 문제가 섞이면 판단이 헷갈림

현재 확인되는 활성 이슈(2026-03-23 기준):

- `mainwindow_auth.cpp` `configureLoginScreenState()` fallback 메시지 깨짐
- `auth_service.cpp` `logout()` fallback/기본 오류 메시지 일부 깨짐

즉 이 항목은 "과거 리스크"가 아니라, **현재도 수정이 필요한 결함 + 재발 방지 과제**로 보는 게 맞다.

## 6.7 자동 테스트가 사실상 없다

현재 코드베이스에는 `QTest`, `EXPECT_`, `ASSERT_`, `TEST(` 같은 자동 테스트 흔적이 사실상 없다.  
연구/실험 문서는 있지만, 실제 회귀 테스트 스위트는 없다.

의미:

- 로그인 성공
- DeviceCheck 첫 진입
- Main -> CCTV/UGV/Playback 전환
- Playback marker click
- UGV ACK/state transition
- clip encode error path

같은 핵심 흐름은 전부 사람 손으로 확인해야 한다.

이건 지금 가장 큰 장기 리스크 중 하나다.

---

## 7. 서브시스템별 평가

## 7.1 MainWindow / 라우팅

### 좋은 점

- 조립 루트가 명확하다
- 인증/런타임/내비게이션이 파일 수준으로 분리되어 있다
- unauthorized, logout, rebuild 같은 cross-cutting concern이 한 곳에 있다

### 아쉬운 점

- 여전히 `MainWindow`는 많은 지식을 안다
- 화면 생성, 서비스 주입, window geometry, active channel routing이 모두 여기 있다

### 결론

현재 단계에서는 좋은 방향이다.  
다만 다음에는 `runtime screen composition` 일부를 별도 coordinator로 뺄 수 있으면 더 좋다.

## 7.2 Common UI / Widget Layer

### 좋은 점

- `TopbarWidget`, `SidebarWidget`, `EventViewWidget`로 공통 구조가 많이 정리됐다
- topbar / sidebar / bottom action bar / status label 정책이 재사용 가능하다

### 아쉬운 점

- `SidebarWidget`가 꽤 많은 역할을 가진다
  - 채널 트리
  - playback 트리
  - action status
  - 하단 버튼
  - playback service 연결

### 결론

현재는 충분히 실용적이다.  
다만 나중에 "탭 구조"와 "하단 액션 구조"를 더 분리하면 class 의미가 더 선명해질 수 있다.

## 7.3 Event subsystem

### 좋은 점

- `EventService`가 REST + WS ingest를 한 군데로 모은다
- dedupe, unread count, cache trim이 있다
- `EventUiHelpers`가 이벤트 표시 규칙과 대화상자를 상당 부분 흡수한다
- 알림센터는 24시간 고정 + 정렬 가능한 표 형식으로 단순화되어 UX가 예측 가능하다

### 아쉬운 점

- event search는 실제 DB 검색이 아니라 **현재 메모리 이벤트 목록 local filter**다
- 최근 이벤트 캐시는 `kMaxCachedEvents = 200`으로 제한돼 있어서, 검색 범위가 서버 전체가 아님

### 결론

운영 UX는 충분히 좋지만, "검색"이라는 이름에 비해 실제 기능은 좁다.  
이건 기능 명세와 UX 기대치를 맞출 필요가 있다.

## 7.4 Playback subsystem

### 좋은 점

- screen/timeline/export 분리가 잘 됐다
- 타임라인 안정화 작업이 실제 코드에 반영됐다
- marker clustering으로 대량 marker 시 UI 멈춤을 줄였다

### 아쉬운 점

- marker가 아직 위젯 기반이라 scale issue가 완전히 사라진 건 아니다
- playable range를 envelope로 그리는 건 UX상 타협이고, 정확한 구조 표현은 아니다
- slider는 now display-only 정책인데, 코드를 모르는 사람은 seek 가능한 듯 오해할 수 있다

### 결론

지금은 "안정화 우선" 단계에선 잘 정리됐다.  
다음 큰 개선은 성능보다도 **타임라인 표현 모델과 클릭 모델의 분리**다.

## 7.5 UGV subsystem

### 좋은 점

- 세션/ACK/error/telemetry 구조가 가장 성숙한 축 중 하나
- UI도 최근 정리되면서 정보 패널, control area, map area 역할이 명확해졌다
- drive hold / PTZ hold / disabled control 표현이 들어갔다

### 아쉬운 점

- 실환경 의존성이 높다
- 성공 path E2E를 상시 자동으로 검증하기 어렵다
- 로그아웃/전환/세션 해제 같은 경계 상황은 환경에 따라 다시 흔들릴 수 있다

### 결론

코드 품질 자체는 꽤 괜찮다.  
리스크는 코드보다 환경 의존 검증 쪽에 더 많다.

## 7.6 Media subsystem

### 좋은 점

- `StreamPlayer` / `VideoRenderWidget` / `ChannelSessionManager` 역할 분리가 좋다
- quality profile, codec hint, multi-host binding, first-frame metric이 다 있다

### 아쉬운 점

- 미디어 경로는 환경 민감도가 높아서 버그가 재현 불안정할 수 있다
- UI에서 stream rebinding을 자주 만지는 기능은 여전히 조심해야 한다

### 결론

현재 구조는 꽤 좋은 편이다.  
이 프로젝트가 더 커져도 media layer를 통째로 갈아엎어야 할 가능성은 낮다.

---

## 8. 성능 관점 평가

## 8.1 좋아진 부분

- screen transition과 first frame/live frame을 분리해 원인 분석이 쉬워졌다
- clip encode는 `stdin rawvideo` 중심 경로로 체감이 나아졌다
- EventView rebuild와 Playback marker issue도 완화됐다
- DeviceCheck 첫 진입은 timing/retry/concurrency cap으로 안정화됐다

## 8.2 아직 조심할 부분

- DeviceCheck tree build
- 이벤트 burst 시 UI rebuild
- Playback marker 위젯 생성
- UGV telemetry + map + video 동시 갱신

즉 이 프로젝트는 "항상 느린 앱"은 아니지만,  
**UI 스레드에 한 번에 많은 시각 요소를 몰아줄 때 취약한 구조**는 아직 남아 있다.

---

## 9. 운영 관점 평가

## 9.1 강한 점

- 401 short-circuit 경로가 있다
- 로그아웃/세션 정리 루트가 있다
- UGV error/ACK 정책이 상대적으로 명시적이다
- 팝업/토스트/상태라벨 정책이 최근 많이 정리됐다

## 9.2 취약한 점

- 서버 환경/실장비 환경에 따라 체감이 달라질 수 있는 축이 많다
- 이벤트 검색/알림센터는 실제 서버 데이터 범위에 크게 의존한다
- 플레이백/UGV는 서버와의 계약이 조금만 흔들려도 UI 증상으로 크게 보인다

즉 운영 리스크는 "코드가 너무 지저분해서"라기보다,  
**서버/장비/실시간성에 의존하는 기능이 많기 때문**이다.

---

## 10. 지금 이 코드베이스에서 가장 먼저 읽어야 할 순서

새 작업자가 들어온다면 아래 순서가 가장 효율적이다.

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

이 순서로 읽으면:

- 전체 라우팅
- 공통 UI
- 미디어 모델
- 각 화면 구현

순으로 무리 없이 들어갈 수 있다.

---

## 11. 다음 리팩토링 우선순위

## 11.1 우선순위 1: 테스트 기초 만들기

가장 먼저 필요한 건 대형 리팩토링이 아니다.  
핵심 흐름 몇 개만이라도 자동으로 확인할 수 있는 최소 테스트 기반이다.

추천 대상:

- `UgvService` state/ack transition
- `EventService` dedupe/trim
- `Playback timeline` 상태 계산
- `AppState.gridCells` 기본 조작

## 11.2 우선순위 2: UI-thread hot spot 완화

다음으로 효과가 큰 건 메인 스레드 과부하 지점을 줄이는 것이다.

추천 순서:

1. Playback marker 렌더 위젯 기반 제거 또는 축소
2. EventView incremental update
3. DeviceCheck tree build 단위 축소

## 11.3 우선순위 3: 상태 경계 재정리

`AppState`를 한 번에 찢을 필요는 없다.  
대신 다음처럼 문서상/코드상 경계를 먼저 세우는 게 좋다.

- auth/session state
- multiview routing state
- playback state
- selected device catalog

## 11.4 우선순위 4: 설정/문자열 품질 관리

- `TeamClue/VMS_v1` vs `TeamClue/VMS_v2` 정책 문서화
- UTF-8 검사 규칙
- 자주 쓰는 사용자 문구 상수 정리
- 우선 수정 대상: `mainwindow_auth.cpp`, `auth_service.cpp` 깨진 한글 문자열 복구

---

## 12. 인수인계 관점 최종 판단

이 코드베이스는 **인수인계 가능한 상태**다.  
다만 "문서만 읽으면 다 된다" 수준은 아니다.  
현재는 다음 조합이 필요하다.

- 이 평가 문서
- `docs/subfiles/VMS_v2_core_classes.md`
- `docs/VMS_v2_implementation_history.md`
- `docs/VMS_v2_popup_toast_status_matrix.md`
- 실제 코드

즉 문서 품질이 매우 중요하고, 그 문서들이 지금처럼 최신 코드 기준으로 유지될 때 인수인계 가치가 커진다.

---

## 13. 최종 결론

`VMS_v2`는 현재:

- 기능적으로는 꽤 많은 화면과 실시간 기능을 소화하고 있고
- 구조적으로는 최근 스프린트에서 확실히 좋아졌고
- 미디어/이벤트/UGV 같은 어려운 문제를 비교적 현실적으로 풀고 있으며
- 남은 문제는 "무너진 코드"라기보다 "운영형 코드가 갖는 무게를 어디까지 정리할 것인가"에 가깝다

따라서 이 프로젝트의 다음 단계는:

1. 큰 기능 추가보다 회귀 방어와 구조적 부하 감소를 먼저 챙기고
2. Playback / DeviceCheck / Event UI의 메인 스레드 hot spot을 정리하고
3. `AppState`와 설정 정책을 문서/코드 양쪽에서 더 명확히 하는 것

이다.

한 문장으로 정리하면:

> **이 코드는 이미 "쓸 수 있는 제품 코드"다. 이제 필요한 건 기능을 더 많이 넣는 것보다, 잘 돌아가는 구조를 덜 흔들리게 만드는 정리 작업이다.**

---

## 14. 같이 읽으면 좋은 문서

- `docs/VMS_v2_implementation_history.md`
- `docs/VMS_v2_post_phase8_execution_plan.md`
- `docs/VMS_v2_performance_tracking.md`
- `docs/VMS_v2_popup_toast_status_matrix.md`
- `docs/subfiles/VMS_v2_architecture.md`
- `docs/troubleshooting/troubleshooting_multiview_streaming.md`

# 코드 리뷰 결과 및 단계별 안정화 계획

- 작성일: 2026-09-16
- 대상: 현재 `standalone-vms` 코드 및 프로젝트 구조
- 참고 문서: `docs/roadmap.md`, `docs/dev_execution_plan.md`
- 목적: 계획 문서 자체를 평가하는 것이 아니라, 현재 구현이 계획 의도와 맞는지 검토하고 Phase 4/5/6 사이에 개선 작업을 어떻게 배치할지 정리한다.

---

## 1. 결론

현재 구현은 Phase 0~3b의 핵심 방향과 상당히 잘 맞는다.

- `OnvifLiteClient -> DeviceService -> AppState -> 기존 화면/미디어 계층` 연결이 구현되어 있다.
- CCTV 제어는 서버 프록시 대신 ONVIF PTZ/Imaging 직접 통신으로 전환되어 있다.
- 로그인/게스트 진입과 카메라 자격증명의 세션/QtKeychain 분기가 반영되어 있다.
- 목업 카메라 호스트는 메인 애플리케이션과 별도 프로젝트로 분리되어 있다.

다만 현재 상태를 그대로 Phase 5까지 진행한 뒤 모든 문제를 Phase 6에서 한꺼번에 해결하는 것은 권장하지 않는다. 기능 정확성이나 보안 경계에 영향을 주는 문제는 Phase 4 진입 전에 바로잡아야 한다. 반대로 대형 파일 분할, 전역 상태 해체, 중복 제거 같은 광범위한 구조 개선은 Phase 6에서 처리하는 편이 효율적이다.

따라서 다음 원칙으로 진행한다.

> Phase 3b 안정화 패치로 실행 기준선을 먼저 만든 뒤 Phase 4와 Phase 5를 진행하고, 대규모 구조 정리는 Phase 6에서 수행한다.

---

## 2. 계획 대비 현재 구현 상태

### Phase 0~3b

대부분 구현되어 있으며 계획의 핵심 흐름과 일치한다.

- Phase 0: 목업 카메라 호스트가 별도 CMake 프로젝트로 존재한다.
- Phase 1: ONVIF discovery, 장치 프로필 조회, RTSP URI 조회와 `DeviceService` 캐시가 구현되어 있다.
- Phase 2: zoom/focus가 ONVIF PTZ/Imaging 요청으로 전환되어 있다.
- Phase 3a: 로그인과 게스트 진입 경로가 분리되어 있다.
- Phase 3b: 장치별 자격증명 입력, 게스트 세션 캐시, 로그인 상태의 QtKeychain 저장 경로가 구현되어 있다.

단, Phase 1~3b에는 목업에서는 통과하지만 실제 카메라에서는 실패할 가능성이 있는 ONVIF 구현 문제가 남아 있다. 따라서 기능 완료 기록과 별개로 Phase 4 전에 안정화가 필요하다.

### Phase 4

아직 계획된 로컬 이벤트 수신 경로가 없다.

- `LocalEventListener`가 존재하지 않는다.
- 현재 이벤트 경로는 서버 기반 `EventService + WsClient`이다.
- 목업 호스트의 UDP event broadcaster는 존재하지만 VMS에서 이를 수신하지 않는다.

Phase 4에서는 UDP 수신 데이터를 별도의 UI 경로로 직접 연결하기보다, 기존 `EventService`가 서버 이벤트와 로컬 이벤트를 동일한 `EventInfo` 모델로 받도록 입력 경계를 통합하는 것이 바람직하다.

### Phase 5

기존 서버 기반 Playback 기능은 존재하지만 계획된 로컬 목업 경로는 아직 구현되지 않았다.

- 현재 `PlaybackService`는 REST 서버 응답을 전제로 한다.
- 목업 호스트에는 시간대별 재생 URL을 제공하는 로컬 Playback HTTP 경로가 없다.
- 다운로드 코드에 URL origin과 파일명 검증 문제가 있으므로 Phase 5 기능 확장 전에 보안 경계를 먼저 정해야 한다.

### Phase 6

다음 항목은 계획대로 Phase 6의 통합 검증 및 최종 정리 범위로 남아 있다.

- UGV 등 현재 핵심 범위 밖 코드의 유지/삭제 판단
- 사용하지 않는 `cgi_mock` 정리
- 대형 화면 파일 분할
- 전역 상태와 중복 로직 정리
- 빌드, 배포, 로깅 및 문서 최종 정비

---

## 3. 전반적인 구조 평가

### 장점

- `src/app`, `src/core`, `src/services`, `src/media`, `src/screens`, `src/ui`로 폴더 역할이 구분되어 있다.
- `RestClient`, `WsClient`, `OnvifLiteClient` 등 프로토콜 클라이언트가 화면 코드와 분리되어 있다.
- `DeviceService`가 기존 공개 인터페이스를 유지하면서 server/onvif 소스를 내부에서 분기한다.
- `ChannelSessionManager`, `StreamPlayer`, `ClipCaptureManager`로 미디어 책임이 분리되어 있다.
- Qt parent ownership, `QPointer`, `deleteLater()` 등 비동기 객체 수명 처리를 전반적으로 고려했다.
- 로그아웃과 종료 시 미디어, WebSocket, UGV, 클립/내보내기 작업을 함께 정리한다.
- 파일, 타입, 메서드, 멤버 네이밍은 전반적으로 일관적이다.

### 구조적 한계

`AppState`는 계획상 기존 화면/미디어 계층을 보존하기 위한 호환 접점이었지만, 현재는 다음 책임이 모두 모인 전역 저장소가 되었다.

- 로그인 및 토큰 상태
- 화면 전환 상태
- 선택 채널과 그리드 상태
- RTSP 및 코덱 매핑
- ONVIF 서비스 주소와 프로필 토큰
- 카메라 사용자명과 비밀번호
- Playback 이동 상태

단기적으로는 계획에 맞는 실용적인 선택이지만, 장기적으로는 변경 영향 범위와 테스트 난도를 높인다.

화면 계층도 다음과 같은 비-UI 책임을 직접 수행한다.

- `DeviceCheckScreen`: 장치/채널 비동기 fan-out, 재시도, 동시성 제한
- `PlaybackScreen`: 직접 HTTP 다운로드, Bearer 토큰 부착, 파일 저장
- Main/CCTV/UGV 화면: 반복되는 스냅샷 및 클립 인코딩 제어

또한 일부 파일이 지나치게 크다.

- `src/screens/ugv_screen.cpp`: 약 1,500줄
- `src/ui/common_widgets.cpp`: 약 1,200줄
- `src/screens/main_screen.cpp`: 약 1,100줄
- `src/media/stream_player.cpp`: 약 1,050줄
- `src/screens/login_screen.cpp`: 약 990줄
- `src/app/mainwindow_auth.cpp`: 약 800줄

현재 기능 개발을 중단하고 전면 분해할 정도는 아니지만, Phase 6에서 책임 단위 분할이 필요하다.

---

## 4. Phase 4 전에 반드시 수정할 항목

### 4.1 서버 설정 없이 로컬 카메라 서비스를 초기화할 수 있도록 분리

#### 현재 문제

`app_config.json`은 환경별 실제 서버 주소를 포함하므로 Git에서 제외되어 있다. 그러나 `loadAppConfig()`는 설정 파일이나 `apiBaseUrl`이 없으면 실패하고, `MainWindow::initializeAuthServices()`는 즉시 반환한다.

이 때문에 클린 체크아웃에서는 다음 객체도 생성되지 않는다.

- `OnvifLiteClient`
- `DeviceService`
- `CctvControlService`

결과적으로 게스트 버튼은 눌러도 DeviceCheck에서 `DeviceService 미연결` 상태가 된다. 이는 서버 없이 카메라를 직접 사용하는 프로젝트의 핵심 목표와 충돌한다.

#### 수정 방향

- 서버 의존 서비스와 로컬 카메라 서비스를 별도로 초기화한다.
- ONVIF/Device/CCTV 서비스는 서버 설정이 없어도 생성한다.
- `apiBaseUrl`은 Auth, 서버 이벤트, 서버 Playback에만 필요한 선택 설정으로 취급한다.
- `app_config.example.json`을 커밋한다.
- README에 설정 파일 생성과 게스트 실행 방법을 작성한다.
- 설정 파일 누락 시 로그인만 비활성화하고 게스트 ONVIF 경로는 유지한다.

#### 완료 기준

- 클린 체크아웃에서 실제 서버 설정 없이 앱을 실행할 수 있다.
- 게스트 -> DeviceCheck -> ONVIF discovery -> Main 진입이 가능하다.
- 서버 관련 메뉴는 설정 없음 상태를 명확하게 표시한다.

### 4.2 ONVIF 실제 카메라 호환성 보정

#### 현재 문제

현재 ONVIF 구현은 목업 응답 형식에 강하게 맞춰져 있다.

1. SOAP XML을 정규식으로 파싱한다.
   - 네임스페이스 접두어가 없는 XML에서 실패할 수 있다.
   - 줄바꿈이 포함된 블록에서 실패할 수 있다.
   - XML entity decoding과 구조 검증이 없다.

2. WS-Security `Created` 생성 시 UTC ISO 문자열 뒤에 `Z`를 다시 붙인다.
   - Qt의 UTC ISO 문자열은 이미 `Z`를 포함하므로 `...ZZ`가 될 수 있다.
   - 현재 목업은 타임스탬프 자체를 검증하지 않고 digest만 재계산하므로 문제를 발견하지 못한다.

3. SOAP 요청에 들어가는 값을 XML escape하지 않는다.
   - 사용자명, 비밀번호, 프로필 토큰 등에 `&`, `<`, `>` 등이 포함되면 요청 XML이 깨질 수 있다.

4. PTZ `ProfileToken`과 Imaging `VideoSourceToken`을 분리하지 않는다.
   - Imaging `Move`는 `VideoSourceToken`을 요구하지만 현재는 프로필 토큰을 전달한다.
   - 목업은 토큰의 의미를 검증하지 않아 통과한다.

#### 수정 방향

- SOAP 파싱을 `QXmlStreamReader` 기반으로 변경한다.
- SOAP 생성을 `QXmlStreamWriter` 또는 공통 XML escape helper 기반으로 변경한다.
- `Created`를 정확한 UTC ISO 8601 형식으로 생성한다.
- `GetProfiles` 응답에서 다음 토큰을 분리해 저장한다.
  - Profile token
  - VideoSourceConfiguration token 또는 실제 VideoSource token
- 목업에서 `Created` 형식과 토큰 종류도 검증하도록 강화한다.
- 실제 카메라 응답과 유사한 줄바꿈/네임스페이스 변형 fixture를 테스트에 포함한다.

#### 완료 기준

- 접두어가 다른 SOAP fixture와 줄바꿈된 SOAP fixture를 모두 파싱한다.
- WS-Security `Created`가 단일 `Z`를 갖는다.
- 특수문자가 포함된 사용자명/토큰으로도 올바른 XML을 생성한다.
- PTZ와 Imaging이 서로 맞는 토큰을 사용한다.

### 4.3 최소 자동 테스트 기반 추가

#### 현재 문제

저장소에 테스트 디렉터리, `QTest`, `enable_testing()`, `add_test()` 타깃이 없다. 현재 검증은 빌드와 수동 게이트 테스트 기록에 의존한다.

비동기 콜백과 전역 상태가 많은 현재 구조에서는 Phase 4/5 변경이 기존 로그인, 카메라, Playback 경로를 깨뜨려도 빠르게 발견하기 어렵다.

#### 우선 추가할 테스트

1. ONVIF SOAP 생성 및 파싱
2. `DeviceService` source 분기와 캐시
3. 로그인/게스트/로그아웃 상태 초기화
4. 이벤트 JSON 파싱, 중복 제거, 200개 제한
5. Playback URL origin과 파일명 검증
6. 카메라 자격증명 성공/실패/취소 흐름

#### 구조 방향

- UI와 독립적인 코드를 별도 라이브러리 타깃으로 묶는다.
- Qt Test 기반 테스트 실행 파일을 추가한다.
- 네트워크 통합 테스트보다 먼저 고정 JSON/XML fixture를 사용하는 단위 테스트를 만든다.
- Phase 4/5 구현 시 신규 사례를 같은 테스트 묶음에 추가한다.

### 4.4 장치 관리 기능의 처리 방향 확정

#### 현재 문제

설정 화면의 장치 관리는 `QSettings("TeamClue", "VMS_v1")`에 이름, 타입, RTSP URL을 저장한다. 하지만 런타임은 `AppState.selectedChannelContexts`만 사용하고 이 설정을 읽지 않는다.

설정 변경 후 실행되는 동작도 기존 선택 상태 정리와 화면 재생성뿐이므로, 사용자가 추가한 장치가 실제 채널이나 스트림에 반영되지 않는다.

#### 선택지

1. 수동 RTSP 장치를 지원한다.
   - 수동 장치 저장 모델을 `DeviceService`의 한 source로 편입한다.
   - discovery 결과와 수동 장치를 충돌 없이 병합한다.
   - 장치 ID, 중복 URL, 자격증명 키를 정의한다.

2. 현재 범위에서 제외한다.
   - 장치 관리 탭을 비활성화하거나 숨긴다.
   - Phase 6에서 삭제 또는 재설계한다.

작동하는 것처럼 보이지만 실제 런타임에는 아무 영향이 없는 현재 상태는 유지하지 않는다.

### 4.5 범위가 작은 명확한 결함 수정

- 로그아웃과 초기화 시 `channelVideoCodecByName/Id`도 함께 비운다.
- `DeviceCheckScreen::reloadDevices()`의 상호 캡처된 `QSharedPointer<std::function>` 순환 참조를 제거한다.
- `RestClient`에서 `context == nullptr`일 때 콜백을 버리는 동작을 다른 서비스와 일관되게 수정한다.
- 카메라 자격증명 모달에서 필요한 필드가 비어 있는 상태로 확인할 수 없게 한다.
- QtKeychain 읽기/쓰기 실패를 호출자에게 반환하고 UI에 표시한다.

---

## 5. Phase 4 구현 시 적용할 원칙

### 5.1 권장 구조

```text
UDP broadcast
    -> LocalEventListener
    -> Local event payload parser
    -> EventService::ingestLocalEvent(...)
    -> EventInfo/cache/unread/signals
    -> 기존 EventViewWidget
```

로컬 이벤트와 서버 이벤트가 UI 직전에서 따로 놀지 않도록 `EventService`를 공통 정규화 및 캐시 경계로 사용한다.

### 5.2 필요한 검증

- 잘못된 JSON 또는 필수 필드 누락 시 안전하게 무시한다.
- 선택되지 않은 채널 이벤트 처리 정책을 명시한다.
- 같은 이벤트가 WS와 UDP 양쪽에서 들어왔을 때 중복 제거한다.
- UDP burst 상황에서도 UI를 이벤트마다 즉시 전체 재렌더링하지 않는다.
- 소켓 bind 실패와 Windows 방화벽 문제를 사용자에게 구분해서 알린다.
- 게스트 모드에서는 로컬 이벤트만, 로그인 모드에서는 서버+로컬 이벤트 병행 여부를 명확하게 정의한다.

---

## 6. Phase 5 착수 시 먼저 수정할 보안 경계

### 6.1 외부 다운로드 URL에 Bearer 토큰을 보내지 않기

현재 Playback 응답이 절대 HTTP(S) URL을 반환하면 호스트 검증 없이 사용하며, 다운로드 요청에는 API Bearer 토큰을 무조건 첨부한다.

수정 원칙:

- API base URL과 동일 origin인 경우에만 Bearer 토큰을 붙인다.
- 외부 스토리지 URL은 서명된 일회성 URL을 사용하고 Bearer 토큰을 붙이지 않는다.
- 허용 가능한 scheme을 `https` 중심으로 제한한다.
- redirect 이후 origin이 바뀌는 경우에도 토큰이 전달되지 않게 한다.

### 6.2 서버 제공 파일명 정규화

서버가 반환한 `fileName`을 그대로 저장 디렉터리에 결합하면 `../` 등을 통한 경로 이탈 위험이 있다.

수정 원칙:

- `QFileInfo(fileName).fileName()`으로 디렉터리 부분을 제거한다.
- 허용 문자와 최대 길이를 제한한다.
- 출력 확장자를 허용 목록으로 제한한다.
- 최종 canonical path가 선택한 저장 디렉터리 내부인지 확인한다.
- 기존 파일 덮어쓰기 정책을 명시한다.

### 6.3 다운로드 책임 분리

`PlaybackScreen`이 직접 네트워크, 인증 토큰, 파일 저장을 처리하지 않도록 `ExportDownloadService` 또는 이에 준하는 별도 객체로 분리한다.

이 객체가 다음을 담당한다.

- URL/origin 정책
- 인증 헤더 정책
- timeout과 취소
- 파일명 정규화
- 임시 파일과 atomic commit
- 진행률과 오류 모델

---

## 7. Phase 6으로 미룰 구조 개선

다음 작업은 현재 기능 정확성을 직접 막지 않으므로 Phase 6에서 통합적으로 수행한다.

### 7.1 AppState 점진적 분리

한 번에 제거하지 않고 다음 상태 객체로 책임을 나눈다.

- `SessionState`: 로그인 사용자, access token
- `NavigationState`: 현재 화면, Playback 이동 요청
- `CameraCatalog`: 선택 채널, RTSP, 코덱, ONVIF endpoint/token
- `CredentialSession`: 세션 중 카메라 자격증명
- `GridLayoutState`: Main 그리드 배치와 활성 채널

화면에는 가능한 한 읽기 전용 조회 API를 제공하고, 변경은 명시적인 메서드나 controller를 통해 수행한다.

### 7.2 대형 파일과 화면 책임 분할

- `ugv_screen.cpp`: UI 구성, 키 입력, 세션 연결, 스트림, 텔레메트리를 분리한다.
- `main_screen.cpp`: 그리드, 이벤트 패널, 캡처 동작을 분리한다.
- `common_widgets.cpp`: `TopbarWidget`, `EventViewWidget`, `SidebarWidget` 구현 파일을 분리한다.
- `stream_player.cpp`: pipeline 생성, bus/error 처리, render binding을 분리한다.
- `login_screen.cpp`: Login/Signup/DeviceCheck 구현 파일을 분리한다.
- `mainwindow_auth.cpp`: 계정 인증과 카메라 자격증명/채널 해석 파이프라인을 분리한다.

### 7.3 중복 제거

- Main/CCTV/UGV의 클립 버튼 및 인코딩 완료 처리
- 스냅샷 저장과 UI 피드백
- `applyDeviceChangesToRuntimeState()`와 `pruneStateDeviceSelection()`의 공통 검증
- Auth/Device/Playback/Event 서비스의 JSON 값 변환 helper
- 화면별 status label style 갱신

### 7.4 미사용 코드 정리

- `cgi_mock` 유지 필요 여부 결정
- `src/core/screens.cpp`와 umbrella header 필요 여부 결정
- UGV 기능을 유지할지 별도 모듈로 분리할지 결정
- 사용되지 않는 설정 및 legacy `VMS_v1` QSettings namespace 제거 또는 migration

### 7.5 빌드 및 의존성 재현성

- `file(GLOB_RECURSE)` 대신 타깃별 명시적 소스 목록 사용
- QtKeychain을 정확한 commit으로 고정
- 오프라인 또는 패키지 관리자 기반 의존성 경로 제공
- 메인 앱, core/services, tests, mock host 타깃 분리
- compiler warning 수준과 CI 빌드 추가
- Debug/Release 및 GStreamer 유무 조합 검증

### 7.6 로깅 체계

현재의 산발적인 `qInfo/qWarning` 대신 `QLoggingCategory`를 도입한다.

권장 카테고리:

- `vms.auth`
- `vms.network.rest`
- `vms.network.ws`
- `vms.network.onvif`
- `vms.events`
- `vms.media`
- `vms.playback`
- `vms.ugv`

민감정보는 로그에 남기지 않는다.

- access token
- 카메라 비밀번호
- 자격증명이 포함된 RTSP URL
- WS-Security 원문

개발 성능 로그와 운영 오류 로그도 구분한다.

---

## 8. 추가 기술 부채 및 개선 항목

### 자격증명 저장

- QtKeychain 호출이 UI 스레드의 중첩 `QEventLoop`를 사용하므로 비동기로 전환한다.
- 사용자명과 비밀번호를 두 개의 독립 key로 저장해 한쪽만 저장되는 부분 실패가 가능하다. 하나의 직렬화된 credential record로 저장하거나 원자적 갱신 정책을 둔다.
- device IP는 DHCP 변경에 취약하므로 장기적으로 ONVIF UUID 등 안정적인 식별자를 우선 사용한다.
- RTSP URL 문자열에 자격증명을 직접 삽입한 채 `AppState`에 저장하는 방식은 로그와 crash dump 노출 위험이 있다. 가능하면 미디어 파이프라인의 인증 속성으로 전달한다.

### 입력 검증

- `apiBaseUrl`, WebSocket URL, ONVIF manual XAddr의 scheme/host를 명시적으로 검증한다.
- 로그인과 회원가입 입력에는 길이 상한을 둔다.
- 이벤트 ID, channel ID, timestamp 필수 필드를 공통 schema 수준으로 검증한다.
- Playback timestamp와 export 시간 구간의 선후 관계를 서비스 계층에서도 검증한다.

### 비동기 처리

- 네트워크 요청에는 generation/epoch 또는 취소 토큰을 일관되게 적용한다.
- 동일 장치 새로고침이 겹쳤을 때 이전 `DeviceService` 콜백이 새 캐시에 섞이지 않게 한다.
- timeout으로 abort된 요청과 사용자 취소를 구분한 오류 모델을 사용한다.
- 화면이 파괴되었을 때 callback만 막는 것과 실제 요청을 취소하는 것을 구분한다.

### 런타임 효율

- 숨겨진 화면에서 계속 실행되는 UI timer를 `showEvent/hideEvent`에서 시작/중지한다.
- 고정 200ms polling이 필요한지 검토하고 가능하면 signal 기반으로 전환한다.
- 장치별 `GetStreamUri` 순차 호출이 채널 수 증가 시 느려질 수 있으므로 제한된 병렬 처리 또는 lazy 조회를 검토한다.

---

## 9. 권장 실행 순서

### Step 1. Phase 3b 안정화 패치

1. 서버/로컬 서비스 초기화 분리
2. 예제 설정과 README 추가
3. ONVIF XML 및 WS-Security 수정
4. ProfileToken/VideoSourceToken 분리
5. 순환 참조 및 상태 초기화 결함 수정
6. 최소 Qt Test 타깃 추가
7. 장치 관리 탭 처리 방향 확정

### Step 2. Phase 4

1. `LocalEventListener` 추가
2. UDP payload parser 추가
3. `EventService` 공통 ingest 경로 추가
4. 서버+로컬 이벤트 중복 제거 정책 구현
5. UDP/이벤트 단위 테스트와 목업 통합 테스트

### Step 3. Phase 5

1. 로컬 Playback 목업 HTTP 경로 구현
2. `PlaybackService` 신규 source 또는 endpoint 적용
3. 다운로드 same-origin/token 정책 구현
4. 파일명/path 검증 구현
5. 다운로드 책임을 화면에서 분리
6. Playback 서비스 및 보안 테스트 추가

### Step 4. Phase 6

1. 전체 통합 게이트 테스트
2. AppState와 대형 파일 점진적 분리
3. 중복 제거
4. 미사용 코드 제거
5. 로깅/빌드/배포 정리
6. 최종 문서 갱신

---

## 10. 우선순위 요약

### 지금 수정

- 서버 설정 없이 게스트 ONVIF 경로 초기화
- 실제 카메라 호환성을 막는 ONVIF 문제
- 최소 자동 테스트 기반
- 장치 관리 기능의 no-op 상태 해소
- 순환 참조, codec 캐시 누락, null context 등 작은 명확한 결함

### Phase 5 착수 시 수정

- 다운로드 URL same-origin 정책
- 외부 URL에 Bearer 토큰 전달 금지
- 파일명/path traversal 방지
- 다운로드 로직 서비스 분리

### Phase 6에서 정리

- AppState 분해
- 대형 파일 분할
- 화면 간 클립/스냅샷 중복 제거
- 미사용 UGV/CGI/legacy 코드 정리
- 로깅 카테고리 및 파일 로깅
- CMake와 의존성 재현성 개선

---

## 11. 검증 메모

코드 리뷰 시점에 저장소에는 현재 소스보다 최신인 기존 `VMS_v2.exe`와 목업 실행 파일이 존재했다. 새 빌드를 시도했으나 CMake의 glob 재검사 이후 장시간 진행되지 않아 중단했다. 자동 테스트 타깃이 없으므로 이 검토에서는 fresh build와 실제 런타임 동작을 독립적으로 보증하지 않는다.

향후 각 단계의 완료 조건에는 최소한 다음을 포함한다.

- 클린 configure/build 성공
- Qt Test 전체 통과
- 서버 없이 게스트 카메라 경로 통과
- 로그인 상태 서버 경로 회귀 없음
- 목업 호스트 end-to-end 통과
- 가능하면 실제 ONVIF 카메라 smoke test 통과


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

RPi를 아직 쓸 수 없어서 원래 순서(Phase 0부터)를 못 밟는 상황이라, 카메라 의존이 없는 이 작업을 먼저 진행했다. 배경은 `docs/roadmap.md`의 "3.5 진행 순서 변경" 참고.

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

## Phase 진입 전 결정: RPi 제약 해결 (2026-09-06)

Phase 0부터는 실제 카메라 역할을 하는 대상(원래 계획은 RPi)이 필요한데, 라즈베리파이를 자유롭게 못 쓰는 상황이 계속됨. 검토 결과 로컬 PC에서 mediamtx + Flask + UDP 브로드캐스트 스크립트를 일반 프로세스로 띄우는 방식으로 대체하기로 결정. 클라우드는 WS-Discovery/이벤트 broadcast가 전제하는 "같은 로컬 네트워크"를 만들 수 없어 기각, 로컬 Docker는 Windows에서 UDP 브로드캐스트/멀티캐스트가 실제 LAN과 다르게 동작할 수 있어 보류. 상세 검토 내용은 `docs/roadmap.md`의 "3.6 RPi 제약 해결" 절 참고.

이 결정으로 Phase 0/1/2/4/5가 더 이상 하드웨어를 기다릴 필요 없이 바로 진행 가능해짐. 아래 Phase 0부터 순서대로 세부 계획을 이어서 작성한다.

---

## Phase 0. 목업 카메라 호스트 구축 (로컬 PC)

배경은 `docs/roadmap.md` 3.6절 참고 — RPi 대신 로컬 PC에서 일반 프로세스로 카메라 역할을 대체한다.

### 언어/스택 결정 (2026-09-08)

- 목업 호스트 3개 컴포넌트(ONVIF-lite mock, CGI mock, UDP 이벤트 브로드캐스터)는 **C++/Qt**(`QTcpServer`/`QUdpSocket`)로 작성한다.
- 이유: 이 머신엔 이미 이 레포 빌드용 툴체인(CMake + MSVC + Ninja + Qt 6.8.3 msvc2022_64)이 검증되어 있음. 확인해보니 Python은 실제 설치 없이 Windows 스토어 스텁만 있는 상태였고 Node.js도 없어서, 둘 다 쓰려면 새 런타임 설치 비용이 붙음. Qt는 이미 알고 있는 언어/툴체인이라 마찰이 가장 적음.
- `mediamtx`는 언어 무관 독립 Go 바이너리이므로 그대로 사용 (RTSP 송출만 담당). 다만 mediamtx 자체는 파일을 직접 loop 재생하지 않으므로, `runOnInit`으로 **ffmpeg**를 띄워 샘플 영상을 loop push하는 방식이 필요함 — ffmpeg는 스크립팅 런타임이 아니라 단일 바이너리 데이터 플레인 도구라 설치 비용이 낮음 (Windows 정적 빌드 다운로드만 하면 됨).
- 목업 호스트는 VMS_v2 앱 코드와 완전히 분리 (별도 디렉토리 + 별도 `CMakeLists.txt`). 기존 화면/미디어/서비스 계층은 이 Phase에서 전혀 건드리지 않음.

### 파일 배치 (예상)

레포 루트에 `mock_camera_host/` 신설:

- `CMakeLists.txt` — 독립 빌드, `Qt6::Core` `Qt6::Network`만 링크 (Widgets/OpenGL 등 불필요)
- `onvif_mock.cpp` — WS-Discovery(UDP multicast `239.255.255.250:3702`) Probe 응답 + 최소 SOAP(`GetDeviceInformation`/`GetProfiles`/`GetStreamUri`), `QTcpServer`로 HTTP POST 처리
- `cgi_mock.cpp` — zoom/focus/PTZ HTTP 엔드포인트, OK 응답만 (`QTcpServer`)
- `event_broadcaster.cpp` — 더미 이벤트 UDP broadcast 송신 (`QUdpSocket` + `QTimer`로 주기 전송)
- `mediamtx.yml` — RTSP 송출 설정 (`runOnInit`에 ffmpeg loop push 명령 등록)
- `sample_media/` — 루프 재생용 샘플 영상 파일 (직접 준비)
- `README.md` — 실행 방법, 포트 목록

### 포트 계획 (기본값, 필요 시 조정)

- ONVIF-lite mock: WS-Discovery UDP `3702` (표준), 디바이스 서비스 HTTP TCP `8082`
- CGI mock: HTTP TCP `8081`
- mediamtx: RTSP TCP `8554` (기본값)
- 이벤트 브로드캐스터: UDP `9998`

### 작업 순서

1. [x] mediamtx Windows 바이너리를 `mock_camera_host/mediamtx/`에 배치, ffmpeg는 `mock_camera_host/ffmpeg/`에 배치 (bin 하위 없이 exe 3개 직접). `mediamtx.yml`의 `paths.cam1.runOnInit`에서 `../ffmpeg/ffmpeg.exe ... ../sample_media/sample_video.mp4`로 loop push. **경로 구분자는 `/`를 써야 함** — mediamtx의 `runOnInit` 파서가 `\`를 이스케이프 문자로 먹어버려 `\`를 쓰면 `..\ffmpeg\ffmpeg.exe`가 `..ffmpegffmpeg.exe`로 깨짐. 사용자가 직접 ffplay/ffprobe로 재생 확인 완료 (2026-09-08)
2. [x] `mock_camera_host/CMakeLists.txt` 작성 — 독립 프로젝트, `Qt6::Core`/`Qt6::Network`만 링크. `find_package(Qt6 ...)`가 기본 경로에서 안 잡혀서 `-DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64`를 명시해야 configure 통과함. 3개 타깃(event_broadcaster/cgi_mock/onvif_mock) 전부 빌드 성공 (exit code 0)
3. [x] `event_broadcaster.cpp` — `QUdpSocket`으로 5초 주기 UDP broadcast(`255.255.255.255:9998`), 더미 이벤트 3종(MOTION_DETECTED/MOTION_CLEARED/LINE_CROSSING) 순환 전송. 페이로드는 JSON(`deviceId`/`channelId`/`channelName`/`eventType`/`timestamp`) — Phase 4 착수 시 `LocalEventListener`가 실제로 뭘 기대하는지 보고 다시 조율
4. [x] `cgi_mock.cpp` — `QTcpServer` 기반 최소 HTTP 서버(포트 8081), `POST /channel/{channelId}/zoom|focus`만 매칭해 항상 `{"data":{"result":"OK"}}` 응답. 실제 `CctvControlService::requestControl(...)`이 보내는 요청/응답 포맷(`src/services/cctv_control_service.cpp`) 확인 후 맞춤
5. [x] (부분) `onvif_mock.cpp` — WS-Discovery(UDP 3702, multicast `239.255.255.250`) Probe → ProbeMatch 응답은 표준 스펙대로 실제 구현(고정 XAddrs `http://127.0.0.1:8082/onvif/device_service`). 디바이스 서비스 SOAP 본문(`GetDeviceInformation`/`GetProfiles`/`GetStreamUri`)은 계획대로 **placeholder로 남김** — 어떤 SOAP action이 왔는지 로그만 찍고 TODO 주석 응답 반환. 실제 필드는 Phase 1의 `OnvifLiteClient` 파서와 맞춰서 확정

컴파일까지는 확인됐고(`cmake --build` exit code 0), 아래 게이트 테스트(Wireshark/curl/Wireshark로 broadcast 확인 등 실제 네트워크 동작 검증)는 사용자가 직접 진행.

### 완료 기준

- [x] 4개 컴포넌트(ONVIF-lite mock, CGI mock, mediamtx+ffmpeg, 이벤트 브로드캐스터)가 빌드/배치 완료 — onvif_mock의 디바이스 서비스 응답은 의도적으로 placeholder
- VMS 코드는 이 Phase에서 아직 연결하지 않음 (Phase 1에서 `DeviceService`에 연결)

### 게이트 테스트

모두 같은 PC(로컬 loopback) 기준으로 진행. 별도 기기로 분리한 테스트는 아직 안 함 (아래 참고).

- [x] **mediamtx RTSP 스트림 재생 확인** — `ffplay.exe rtsp://127.0.0.1:8554/cam1`로 `sample_video.mp4` loop 재생 확인 (사용자 확인, 2026-09-08)
- [x] **UDP 이벤트 브로드캐스트 패킷이 실제 전송됨** — `event_broadcaster.exe` 실행 후, 별도 터미널에서 PowerShell `UdpClient(9998)` 리스너로 5초 주기 JSON 페이로드(`{"channelId":...,"eventType":"MOTION_DETECTED",...}`) 수신 확인 (사용자 확인, 2026-09-08)
- [x] **WS-Discovery UDP Probe에 mock이 응답** — `onvif_mock.exe` 실행 후, PowerShell `UdpClient`로 WS-Discovery Probe SOAP 페이로드를 `127.0.0.1:3702`에 유니캐스트 전송 → `onvif_mock` 콘솔에 `Probe 수신 <- 127.0.0.1:53475 -> ProbeMatch 응답` 로그, 클라이언트 쪽에 `ProbeMatch` 전체 XML(`<w:XAddrs>http://127.0.0.1:8082/onvif/device_service</w:XAddrs>` 포함) 수신 확인 (사용자 확인, 2026-09-08)
- [x] **zoom/focus 엔드포인트 OK 응답 확인** — `cgi_mock.exe` 실행 후 `Invoke-RestMethod -Uri http://localhost:8081/channel/1/zoom -Method Post -Body '{"value":10}' -ContentType application/json` → `{"data":{"result":"OK"}}` 응답, `cgi_mock` 콘솔에 `channel=1 action=zoom value=10 -> OK` 로그 확인 (사용자 확인, 2026-09-08)
- [x] **같은 PC 루프백에서 멀티캐스트/브로드캐스트 동작** — 위 4개 테스트 전부 같은 PC 안에서 별도 방화벽 규칙 추가 없이 정상 동작. 다른 기기로 분리하는 테스트는 이번 Phase 범위에서는 안 함 — 실제로 필요해지는 시점(Phase 1 WS-Discovery 클라이언트, Phase 4 이벤트 수신)에 다시 확인. `docs/roadmap.md` 5절 리스크로 계속 추적
- [x] **Windows 방화벽 인바운드 규칙 필요 여부** — 이번 로컬 테스트 범위에서는 별도 규칙 추가 없이 4개 컴포넌트 모두 응답/수신 정상 (같은 PC 안 loopback이라 방화벽 인바운드 필터를 안 거쳤을 가능성 있음 — 다른 기기에서 접근하는 시나리오는 위와 마찬가지로 이후 Phase에서 재확인)

**상태: Phase 0 완료 (2026-09-08).** 4개 컴포넌트(mediamtx+ffmpeg, event_broadcaster, onvif_mock, cgi_mock) 빌드 및 로컬 게이트 테스트 전부 통과. onvif_mock의 디바이스 서비스 SOAP 본문은 계획대로 placeholder로 남겨두고 Phase 1에서 확정.

---

## Phase 1. 카메라 자동 탐색 (ONVIF-lite)

### 진입 전 결정 (2026-09-08)

Phase 1 착수 전, 기존 서버 REST API 형식을 그대로 카메라/목업 쪽에 이어 쓸지 재검토했다. 조사 결과와 결정:

- **현재 서버 API 챗니스 측정**: `DeviceService`(`fetchDevices`→`fetchDeviceChannels`→`fetchChannelDetail`) 기준 디바이스 2개·채널 2개면 정상 케이스 7회, 재시도 겹치면 최대 9회 HTTP 호출. 배칭/캐싱 없음.
- **ONVIF-lite 리서치**: `GetProfiles`가 채널 목록+해상도/코덱을 한 번에 반환해 "채널 목록"과 "채널 상세"를 하나로 합침. `WS-Discovery(1) → 디바이스당(GetCapabilities/GetDeviceInformation 1 + GetProfiles 1 + 채널당 GetStreamUri 1)`로 구조적으로 flat.
- **결정 1 (discovery)**: ONVIF-lite 유지. 챗니스 문제를 구조적으로 해결.
- **결정 2 (control, Phase 2 대상)**: SUNAPI를 그대로 베끼지 않고 GET+query-param "카메라스러운" CGI 스타일로 전환. 지금 구현 안 함, Phase 2에서 `cgi_mock.cpp`/`CctvControlService`를 같이 고칠 때 적용.

세부 내용은 `C:\Users\yeong\.claude\plans\phase-wobbly-cray.md`(승인된 계획) 참고.

### 구현 내용

- **`OnvifLiteClient`** 신설 (`include/services/onvif_lite_client.h`, `src/services/onvif_lite_client.cpp`) — `RestClient`와 동급 계층(프로토콜 클라이언트, `AppState`/`SelectedChannelContext` 모름).
  - `discover(...)`: WS-Discovery Probe(UDP multicast 239.255.255.250:3702) 전송, `discoveryTimeoutMs` 동안 ProbeMatch 수집. 결과는 `invalidate()` 전까지 캐시. `setManualXAddr(...)`로 WS-Discovery가 막힌 환경을 위한 수동 fallback 지원.
  - `fetchDeviceProfiles(uuid, xaddr, ...)`: `GetDeviceInformation` → `GetProfiles` → 프로필별 `GetStreamUri`를 한 시퀀스로 처리, uuid별로 메모이즈. SOAP 요청은 `QNetworkAccessManager::post`, 응답 파싱은 네임스페이스 접두어에 안 흔들리는 local-name 정규식 매칭(`extractSingleValue`) — 설계 원칙 4에 따라 `QXmlStreamReader` 없이 최소로.
- **`DeviceService` 내부 재배선** (public 시그니처 무변경) — `device.source` 설정에 따라 분기:
  - `fetchDevices(...)`: onvif 소스면 `discover()` 후 디바이스별 `fetchDeviceProfiles()`를 병렬 fan-out(pending 카운터 패턴)으로 한 번에 실행해, 채널 목록+RTSP+코덱까지 이 시점에 전부 `m_channelDetailCache`에 캐싱. 매 reload마다 캐시 전체 무효화 + `OnvifLiteClient::invalidate()`.
  - `fetchDeviceChannels(deviceId, ...)` / `fetchChannelDetail(channelId, ...)`: onvif 소스면 **캐시만 읽고 새 SOAP 호출 없음** — `QTimer::singleShot(0, ...)`으로 항상 비동기 디스패치해 기존 콜백 계약(동기 콜백으로 인한 `mainwindow_auth.cpp`의 `pending` 카운터 재진입 위험) 보존.
  - uuid/profile-token ↔ 합성 `int deviceId`/`channelId` 매핑은 `DeviceService` 내부 `QHash`로 관리.
  - `device.source: "server"`면 기존 REST 경로 그대로 (무수정).
- **`app_config.json` / `AppConfig`**: `device.source`(기본 `"onvif"`), `device.onvifDiscoveryTimeoutMs`(기본 1500), `device.onvifManualXAddr` 추가.
- **`mainwindow.h`/`mainwindow_auth.cpp`**: `m_onvifLiteClient` 멤버 추가, `initializeAuthServices()`에서 생성 후 `DeviceService` 생성자에 주입 + `setDeviceSource(...)` 호출. `DeviceCheckScreen`/`startRequested` 핸들러는 무수정.
- **`mock_camera_host/onvif_mock.cpp`**: Phase 0의 placeholder를 실제 응답으로 구현. `GetDeviceInformation`(Manufacturer/Model/SerialNumber), `GetProfiles`(프로필 2개, `profile_1`/`profile_2`, 각각 Name/Encoding/Resolution), `GetStreamUri`(요청의 `ProfileToken`으로 mediamtx 경로 매핑: `profile_1→cam1`, `profile_2→cam2`).
- **`mock_camera_host/mediamtx/mediamtx.yml`**: `cam2` 경로 추가 (같은 샘플 영상 재사용).
- **부수 정리**: `device_service.cpp`에 남아있던 인코딩 깨진 한글 에러 문구 6곳을 이번에 손댄 함수 안이라 같이 정리.

### 빌드 확인

- `VMS_v2` 메인 프로젝트: `cmake --build build` exit code 0 (OnvifLiteClient 추가 시 최초 1회 AUTOMOC 캐시가 안 갱신되는 이슈 있었음 — `VMS_v2_autogen` clean 후 재빌드로 해결)
- `mock_camera_host`: `cmake --build mock_camera_host/build` exit code 0, 3개 타깃 전부 정상

### 완료 기준

- [x] `OnvifLiteClient` 구현 + `DeviceService` 재배선 + config 분기 + mock host 실제 SOAP 응답, 컴파일/링크 성공
- [x] 실제 실행으로 게이트 테스트 (아래) — 사용자 확인 완료 (2026-09-08)

### 게이트 테스트

- [x] `mock_camera_host`의 `onvif_mock.exe`(3702/8082) + `mediamtx.exe`(cam1+cam2, 8554) 실행, 서버는 끈 상태에서 VMS 게스트 진입 → DeviceCheck 화면에 목업 디바이스/채널 2개가 뜸 (사용자 확인)
- [x] 채널 선택 → "VMS 시작" → Main 화면 멀티뷰/전체화면에서 실제 mediamtx RTSP(cam1/cam2) 재생됨 (사용자 확인). 중간에 이 저장소의 VMS_v2 빌드가 `VMS_WITH_GSTREAMER=0`으로 잡혀 있어 재생이 전혀 안 되는 별도 이슈가 있었음 — pkg-config가 PATH에 없어 CMake가 GStreamer(설치는 이미 돼 있었음)를 못 찾은 것. GStreamer `bin` 폴더를 PATH에 추가하고 재구성/재빌드해서 해결(Phase 1 코드와는 무관한 로컬 환경 이슈)
- [x] DeviceCheckScreen 새로고침 시 크래시/중복 호출 없이 다시 스캔됨 (사용자 확인)
- [ ] (선택, 미실행) `device.source`를 `"server"`로 바꾸고 기존 서버 켠 상태에서 기존 REST 흐름이 회귀 없이 동작하는지 — 필수 아님

**상태: Phase 1 완료 (2026-09-08).** `OnvifLiteClient` + `DeviceService` 캐싱으로 디바이스 조회 챗니스(7~9회 → discovery+프로필 조회 1세트) 구조적으로 해결, 목업 호스트 대상으로 엔드투엔드(DeviceCheck→Main 재생) 확인 완료.

---

## Phase 2. CctvControlService 직접 제어 전환

### 진입 전 결정 (2026-09-09) — GET+query-param CGI에서 ONVIF PTZ/Imaging으로 재변경

Phase 1 진입 전 결정에서는 control 와이어를 GET+query-param "CGI스러운" 스타일로 가기로 했었으나, Phase 2 착수 논의 중 재검토해서 뒤집었다.

- **왜 CGI를 버렸나**: PTZ CGI는 벤더마다 형식이 전부 다름(한화 SUNAPI/Axis VAPIX/Dahua 등 서로 호환 안 됨). 우리가 만든 GET+query-param 방식도 실제로는 아무 카메라도 안 쓰는 "우리만의 방언"이라 범용성이 없었다.
- **왜 ONVIF인가**: ONVIF PTZ(`RelativeMove`)/Imaging(`Move`) 서비스는 Profile S 카메라라면 공통으로 지원하는 표준. Phase 1에서 이미 discovery를 ONVIF-lite로 만들어놨으니 같은 프로토콜 계열로 가는 게 일관성 있음.
- **왜 `RelativeMove`(Continuous 아님)**: 현재 UI는 "누르면 -100~100 중 하나의 스텝만큼 이동"하는 discrete 방식이라, "누르고 있는 동안 계속 이동"하는 `ContinuousMove`보다 "정해진 양만큼 이동 후 정지"하는 `RelativeMove`/Imaging `Move`(Relative)가 정확히 맞아떨어짐. 별도의 "일정 시간 후 정지" 타이머 로직이 필요 없음.
- **`cctv.source`(서버/카메라 토글) 도입 안 함**: 처음엔 Phase 1의 `device.source`처럼 토글을 만들려고 했으나, Phase 2 로드맵 원문("base URL만 서버 → 목업 호스트로 **교체**")을 다시 보면 이건 "서버냐 카메라냐 고르는 토글"이 아니라 "카메라 주소가 바뀔 수 있다"는 뜻이었음. 애초에 zoom/focus는 카메라가 이미 직접 지원하는 기능이라 서버가 필요한 적이 없었고, 서버 상태와 무관하게 항상 카메라로 가는 게 리팩토링 목적(서버 의존성 제거)에 맞음. `device.source`가 토글인 건 Phase 1 로드맵 원문이 실제로 "서버 있음/없음에 따라 분기"라고 명시했기 때문 — Phase 2와는 원문 자체가 다름.
- **Phase 3b(자격증명 캐시)와의 관계**: 실제 ONVIF 카메라는 PTZ 요청에 인증이 필요하지만, 지금 mock(`onvif_mock`)은 인증을 검사하지 않으므로 Phase 2는 자격증명 없이도 mock 대상으로 완전히 동작한다. Phase 3b(장치별 ID/PW 모달 + QtKeychain, 게스트=휘발성/회원=영구 저장)는 의도적으로 분리해서 나중에 진행 — 세부 설계는 `docs/roadmap.md` Phase 3b 섹션에 정리해둠.
- **`cgi_mock.cpp`(Phase 0)**: 이번 Phase에서 안 씀. 삭제하지 않고 남겨두고 최종 정리는 Phase 6에서.

### 작업 범위 (예정)

- `include/services/onvif_lite_client.h/.cpp`: `relativeMove(xaddr, profileToken, zoomDelta, context, callback)`(PTZ), `imagingRelativeMove(xaddr, videoSourceToken, focusDelta, context, callback)`(Imaging) 추가. 기존 `postSoap` 재사용.
- `include/services/device_service.h/.cpp`의 `ChannelDetailResult`: `onvifXAddr`/`onvifProfileToken` 필드 추가, `fetchDevicesFromOnvif`에서 캐싱 시 같이 채움.
- `include/core/app_state.h`: `channelOnvifXAddrById`/`channelOnvifProfileTokenById`(`QHash<int, QString>`) 추가 — 설계 원칙 1(AppState를 접점으로) 적용.
- `src/app/mainwindow_auth.cpp`의 `finalize()`: `fetchChannelDetail` 콜백에서 위 두 필드를 AppState 맵에 채움 (`channelRtspById` 채우는 자리 옆에).
- `include/services/cctv_control_service.h/.cpp`: `OnvifLiteClient*` 의존성 추가(생성자 확장), `requestControl` 내부를 `AppState`에서 xaddr/token 조회 → `OnvifLiteClient`의 새 메서드 호출로 교체. `zoomStep`/`focusStep`/`isSupportedStepValue` 시그니처는 무변경.
- `src/app/mainwindow_auth.cpp`의 `initializeAuthServices()`: `CctvControlService` 생성자에 `m_onvifLiteClient` 추가 주입.
- `mock_camera_host/onvif_mock.cpp`: device_service SOAP 디스패처에 `RelativeMove`(PTZ)/`Move`(Imaging) 액션 처리 추가 — 로그만 찍고 빈 성공 응답 반환(설계 원칙 4, mock은 정교할 필요 없음).
- `CctvScreen`: 무수정.

### 완료 기준 / 게이트 테스트

- [x] 빌드 성공 (VMS_v2, mock_camera_host 둘 다 exit code 0)
- [x] `CctvScreen`에서 zoom/focus 조작 시 `onvif_mock` 콘솔에 RelativeMove/Move 로그 확인 (사용자 확인, 2026-09-09) — `PTZ RelativeMove token=profile_1 zoom=0.0100/0.1000/1.0000`, `Imaging Move token=profile_1 focus=-0.1000/-1.0000/0.0100` 등. step 값(±1/±10/±100)이 정확히 ±0.01/±0.1/±1.0로 스케일링됨 확인
- [x] 지원하지 않는 step 값(-100~100 외) 클라이언트 차단 — `isSupportedStepValue` 로직 자체는 무변경이라 코드 리뷰로 확인 (별도 실행 테스트 불필요)

**상태: Phase 2 완료 (2026-09-09).** `CctvControlService`가 서버 프록시 없이 ONVIF PTZ(`RelativeMove`)/Imaging(`Move`)로 카메라(목업)에 직접 zoom/focus. `cgi_mock.cpp`는 이번 Phase에서 안 씀(Phase 6에서 최종 정리 판단), 관련 REST path 설정(`cctv.zoomPath`/`focusPath`, `setZoomPathTemplate`/`setFocusPathTemplate`)도 같이 제거.

---

## Phase 1/2 사후 보정: GetCapabilities 누락 (2026-09-09)

Phase 2 완료 후 사용자가 ONVIF 실제 흐름을 다룬 외부 글([onvif-introduction](https://pingu52.vercel.app/posts/embedded-system/protocol-security/onvif-introduction/), [onvif-ws-discovery](https://pingu52.vercel.app/posts/embedded-system/protocol-security/onvif-ws-discovery/))을 검토하다가, 우리 구현이 **`GetCapabilities` 단계를 완전히 스킵**하고 있다는 걸 발견했다.

- **실제 ONVIF 흐름**: WS-Discovery → Device Service에서 `GetDeviceInformation`/`GetCapabilities` → `GetCapabilities` 응답의 Media(및 PTZ/Imaging) Service XAddr로 → `GetProfiles`/`GetStreamUri`(Media Service) / `RelativeMove`(PTZ Service) / `Move`(Imaging Service).
- **우리가 스킵한 것**: `GetCapabilities`를 안 부르고, WS-Discovery로 받은 Device Service 주소를 Media/PTZ/Imaging 주소인 것처럼 그대로 재사용했음(Phase 1의 `GetProfiles`/`GetStreamUri`, Phase 2의 `RelativeMove`/`Move` 전부).
- **gSOAP 사용 여부 논의**: 실무에서는 ONVIF C++ 구현 시 `wsdl2h`/`soapcpp2`로 코드를 생성하는 gSOAP이 표준이지만, 로드맵이 명시한 스코프("ONVIF 풀스펙 구현 안 함, Discovery/GetStreamUri/PTZ 최소만")에 비해 새 빌드 툴체인을 통째로 들이는 건 과함 — gSOAP 도입 없이 기존 Qt 기반 SOAP 문자열 조립 방식 그대로 `GetCapabilities` 단계만 추가하기로 결정.
- **수정**:
  - `OnvifLiteClient`: `GetDeviceInformation` 다음에 `GetCapabilities` 호출 추가. 응답에서 Media/PTZ/Imaging XAddr을 각각 파싱해 `DeviceProfilesResult`에 저장(`mediaXAddr`/`ptzXAddr`/`imagingXAddr`). 파싱 실패/누락 시 Device Service 주소로 폴백. 이후 `GetProfiles`/`GetStreamUri`는 `mediaXAddr`로 전송.
  - `DeviceService`/`ChannelDetailResult`/`AppState`: 채널당 xaddr 하나(`onvifXAddr`/`channelOnvifXAddrById`)를 `onvifPtzXAddr`/`onvifImagingXAddr`(그리고 `channelOnvifPtzXAddrById`/`channelOnvifImagingXAddrById`) 두 개로 분리.
  - `CctvControlService`: zoom은 PTZ XAddr, focus는 Imaging XAddr을 각각 조회해서 사용.
  - `mock_camera_host/onvif_mock.cpp`: `GetCapabilities` 응답 추가 — Media/PTZ/Imaging XAddr을 같은 포트(8082), 다른 경로(`/onvif/media_service` 등)로 반환. 서버 자체는 경로 안 보고 액션 이름으로만 분기하지만, **클라이언트가 받은 주소를 실제로 그대로 써서 요청을 보내는지**는 검증됨(하드코딩된 재사용이 아님).
- 빌드 확인: `VMS_v2`, `mock_camera_host` 둘 다 exit code 0.
- **실행 재확인 완료 (2026-09-09, 사용자 확인).** `onvif_mock` 콘솔에 `GetCapabilities -> media=http://127.0.0.1:8082/onvif/media_service ptz=.../ptz_service imaging=.../imaging_service` 로그 확인, 이후 `GetProfiles`/`GetStreamUri`(profile_1/profile_2)/`RelativeMove`(zoom)/`Move`(focus) 전부 정상 동작.

**상태: Phase 1/2 사후 보정 완료 (2026-09-09).**

---

## Phase 3b. 로컬 자격증명 캐시

### 진입 전 결정 (2026-09-16)

**어디에 인증을 걸 것인가.** 처음엔 "discovery로 디바이스 찾고, 그 디바이스 정보(GetCapabilities/GetProfiles/GetStreamUri)를 ID/PW로 요청"하는 흐름을 생각했는데, 이러면 `DeviceCheckScreen`의 채널 트리 자체가 인증 없이는 안 그려진다 — "채널 선택부터 하고 나중에 ID/PW 입력" 흐름과 정면으로 충돌한다.

**결정**: discovery/트리 조회(`GetDeviceInformation`/`GetCapabilities`/`GetProfiles`/`GetStreamUri`)는 Phase 1 그대로 무인증 유지. **인증은 실제로 "그 장치를 쓸 때"인 두 곳에만 건다:**
1. RTSP 영상 연결 (`rtsp://id:pw@host/path` 형태로 URL에 자격증명 임베드)
2. PTZ 제어(`RelativeMove`/`Move`) — WS-Security UsernameToken

이게 사용자가 원래 팀 프로젝트에서 겪었던 흐름("디스커버리 → 채널 목록/RTSP 받고 → 영상 불러올 때/PTZ 조절할 때 ID·PW 포함해서 요청")과 정확히 일치하고, Phase 1에서 이미 테스트 통과한 discovery 흐름도 안 건드리게 된다.

**QtKeychain 도입 방식**: 이 머신에 QtKeychain이 설치되어 있지 않음(vcpkg 없음, Qt Creator 내부 사본은 헤더/lib 없이 못 씀). CMake `FetchContent`로 소스를 받아서 우리 빌드에 같이 컴파일하는 방식으로 결정 — 시스템에 아무것도 수동 설치 안 해도 되고, GStreamer pkg-config 때와 달리 새 설치 없이 첫 configure 때 자동으로 받아진다.

**mock이 인증을 실제로 검증하게 만든다** — 틀린 ID/PW를 넣으면 실제로 연결이 거부되어야 포트폴리오로서 의미가 있다는 결정. `onvif_mock`은 WS-Security digest를 직접 계산해서 비교, `mediamtx`는 자체 `authInternalUsers` 기능으로 RTSP 인증을 검사.

**세션 캐시 vs 영구 저장 키가 다름**: 세션 중 캐시는 `DeviceService`가 이미 갖고 있는 synthetic `deviceId`(int, 세션마다 재할당됨)로 충분하지만, QtKeychain 영구 저장은 재시작해도 같은 카메라를 알아봐야 해서 **`deviceIp`(문자열, 안정적)를 키로 사용** — uuid를 밖으로 더 노출시키지 않고 이미 `SelectedChannelContext.deviceIp`에 있는 값을 재사용.

### 대상

- 신규: `include/ui/device_credential_dialog.h`, `src/ui/device_credential_dialog.cpp` — `SettingsDialog`와 같은 패턴의 `QDialog` (아이디/비번 입력 폼)
- 신규: `include/services/credential_store.h/.cpp` (가칭) — QtKeychain 래퍼, 세션 캐시(`AppState`)와 영구 저장(QtKeychain) 조정
- `CMakeLists.txt` — QtKeychain `FetchContent` 추가
- `include/core/app_state.h` — `deviceOnvifUsernameById`/`deviceOnvifPasswordById`(세션 캐시, `QHash<int, QString>`) 추가
- `src/app/mainwindow_auth.cpp`의 `startRequested` 핸들러 — 선택된 채널들의 distinct `deviceId` 중 세션 캐시에 없는 것마다 모달 표시(로그인 상태면 먼저 QtKeychain 조회), 입력받은 자격증명으로 RTSP URL에 `id:pw@` 임베드
- `include/services/onvif_lite_client.h/.cpp` — `relativeMove`/`imagingRelativeMove`에 username/password 파라미터 추가, WS-Security `UsernameToken`(Nonce+Created+PasswordDigest) 헤더 생성 후 SOAP 요청에 포함
- `include/services/cctv_control_service.h/.cpp` — `requestControl`에서 `deviceIdForChannelId(channelId)`(기존 `channel_context_dnd_helpers` 헬퍼 재사용)로 deviceId 조회 → `AppState`에서 자격증명 조회 → `OnvifLiteClient`에 전달
- `mock_camera_host/onvif_mock.cpp` — `RelativeMove`/`Move` 요청에서만 WS-Security digest 검증(하드코딩 계정, 예: `admin`/`admin1234`), 불일치 시 401 응답. discovery용 액션(GetDeviceInformation 등)은 무인증 유지
- `mock_camera_host/mediamtx/mediamtx.yml` — `authInternalUsers`에 같은 계정을 RTSP `read` 권한으로 등록 (현재 `any` 사용자 설정 대체)

### 작업

1. `CMakeLists.txt`에 QtKeychain `FetchContent` 추가, 빈 스켈레톤으로 빌드 확인
2. `AppState`에 세션 캐시 필드 추가
3. `DeviceCredentialDialog` 작성 (장치명 표시 + ID/PW 입력 + OK/Cancel)
4. `CredentialStore`(QtKeychain 래퍼) 작성 — `load(deviceIp)`/`save(deviceIp, username, password)`
5. `mainwindow_auth.cpp`의 `startRequested`에 모달 트리거 로직 삽입 — distinct deviceId 순회, 세션 캐시 미스 시 (로그인 상태면 QtKeychain 조회 후) 모달, 취소하면 해당 디바이스의 채널은 선택에서 제외
6. RTSP URL에 자격증명 임베드 (finalize()의 `resolvedRtsp` 처리 지점)
7. `OnvifLiteClient`에 WS-Security 헤더 생성 추가, `relativeMove`/`imagingRelativeMove` 시그니처 확장
8. `CctvControlService`가 새 시그니처로 자격증명 전달하도록 수정
9. `onvif_mock.cpp`에 WS-Security 검증 추가, `mediamtx.yml`에 `authInternalUsers` 설정
10. 빌드 + 실제 실행 확인

### 구현 중 확정된 세부 사항

- **QtKeychain 태그**: `v0.14.3`은 존재하지 않아 최신 릴리스 `v0.14.0`으로 확정. 기본이 Qt5를 찾게 되어 있어 `BUILD_WITH_QT6 ON`을 강제 설정해야 configure 통과함.
- **QtKeychain include 경로 이슈**: `qt6keychain` 타깃은 `install(INSTALL_INTERFACE)`에서만 include 경로를 노출해서, FetchContent로 설치 없이 바로 빌드하면 소비하는 쪽(`VMS_v2`)에 헤더 경로가 자동 전파 안 됨 — `target_include_directories(VMS_v2 PRIVATE ${qtkeychain_SOURCE_DIR} ${qtkeychain_BINARY_DIR})`로 직접 추가해서 해결.
- **QtKeychain은 shared 라이브러리(dll)로 빌드됨** — `VMS_v2.exe`와 같은 폴더로 자동 복사하는 POST_BUILD 스텝 추가(Qt/GStreamer 때와 같은 문제 재발 방지).
- **자격증명 세션 캐시 키**: `deviceId`(int)는 `DeviceCheckScreen` 새로고침마다 재할당되어 불안정하다는 걸 발견 — `deviceIp`(문자열)로 키를 잡고, `channelId` 키 버전은 `finalize()` 시점에 복제해서 `CctvControlService`/RTSP 임베드에서 바로 조회 가능하게 함.
- **인증 하드코딩 계정**: `admin`/`admin1234` — `onvif_mock.cpp`의 WS-Security 검증과 `mediamtx.yml`의 RTSP read 계정을 동일하게 맞춰서, 캐시된 자격증명 하나로 영상 재생+PTZ 제어가 둘 다 되게 함.
- **mediamtx 인증 범위**: `read`(재생)만 인증 필요하도록 제한, `publish`(ffmpeg의 자체 loop push)는 무인증 유지 — publish까지 잠그면 mock 자체의 `runOnInit` 스크립트가 깨짐.

### 틀린 자격증명 시 UX 보정 (2026-09-16)

최초 구현은 "틀린 ID/PW로도 일단 Main에 진입시키고, 해당 채널은 자연스럽게 `연결 안 됨`/`연결 중` 상태로 표시"하는 방식이었다(추가 코드 없이 기존 그레이스풀 디그레이드 동작 재사용). 실제로 동작을 확인한 사용자가 이 UX가 어색하다고 판단 — 타이틀바/닫기(X) 버튼이 있는 "선택된 채널"처럼 보이면서 계속 실패 상태로 남아있는 게 진짜 빈 셀과 헷갈린다는 피드백.

**재시도 모달 같은 새 UI를 만드는 건 오버엔지니어링이라는 데 합의**했고, 대신 다음으로 변경:

1. 모달에서 자격증명을 **새로 입력**받은 디바이스에 한해(캐시/키체인에서 그냥 로드된 경우는 재검증 안 함), 그 디바이스의 대표 채널 PTZ에 `RelativeMove(delta=0)` "ping"을 한 번 날려 인증 여부를 즉시 확인.
2. ping 실패 시 세션 캐시에서 해당 자격증명을 지우고, 그 디바이스의 채널들을 이미 존재하던 "RTSP 조회 실패" 필터 경로로 그대로 흘려보냄 — 그리드에 배정되지 않아 빈 셀처럼 보인다. 새 팝업/재시도 버튼 없음, 다음에 그 채널을 다시 선택하면 모달이 다시 뜬다.
3. `main_screen.cpp`(화면 계층)는 전혀 수정하지 않음 — 설계 원칙 1을 지키면서, `finalize()`가 채널을 그리드에 배정하기 전 단계에서 걸러내는 방식으로 해결.

구현상 `startRequested` 핸들러의 후반부(펜딩 카운터·`fetchChannelDetail` fan-out·`finalize`)를 `MainWindow::runDeviceSelectionPipeline(...)`로 분리 — ping 검증이 비동기라 기존 동기 루프 구조로는 이어붙일 수 없었음.

### 완료 기준 / 게이트 테스트

- [x] 빌드 성공 (`VMS_v2` — QtKeychain FetchContent 포함, `mock_camera_host`) — 둘 다 exit code 0
- [x] 게스트로 채널 선택 → 처음 보는 장치면 ID/PW 모달 → `admin`/`admin1234` 입력 → Main에서 RTSP 재생 + PTZ 정상 동작 — 사용자 확인 완료 (2026-09-16)
- [x] 틀린 ID/PW 입력 시 RTSP 재생 실패 / PTZ 요청 실패로 이어지는지 (mock이 실제로 거부하는지) — 사용자 확인 완료 (2026-09-16), UX 보정(위 항목) 반영 후 재확인 완료
- [x] 게스트 모드: 자격증명이 QtKeychain에 저장되지 않고 세션 한정으로만 쓰이는지 — 명시적 재시작 테스트는 안 했지만, 게스트 경로는 `isAuthenticated`가 `false`라 `CredentialStore::save()` 호출 자체가 코드상 실행되지 않으므로 구조적으로 보장됨
- [x] **로그인 상태(QtKeychain 영구 저장) 테스트는 스코프에서 제외** — 실제/목업 인증 서버가 없어 `AppState.isAuthenticated`를 `true`로 만들 방법이 이 환경에 없음. 이 프로젝트의 목적 자체가 "서버 없이 동작"이므로, 서버 의존적인 이 경로를 억지로 검증하는 것은 우선순위가 아니라고 판단해 의도적으로 스킵(사용자 결정, 2026-09-16)

Phase 3b 완료.

---

## 다음 Phase 메모

- Phase 3b 착수. 위 작업 순서대로 구현 진행.
- 이후 Phase 4 → 5(선택) → 6 순으로 진행.

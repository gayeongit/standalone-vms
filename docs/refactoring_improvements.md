# standalone-vms 리팩토링 정리

- 문서 성격: 포트폴리오/이력서용으로 "왜 리팩토링했는지"와 "무엇을 개선했는지"를 정리한 요약 문서.
- 실행 과정의 세부 커맨드/테스트 로그는 `docs/dev_execution_plan.md`, Phase 구성은 `docs/roadmap.md` 참고. 이 문서는 그 둘을 압축해 "성과" 관점으로 재구성한 것이다.
- 원본: 팀 프로젝트 [VEDA3-CLUE-VMS](https://github.com/gayeongit/VEDA3-CLUE-VMS)(`v2` 브랜치)의 VMS 클라이언트 파트.

---

## 1. 왜 리팩토링했는가

팀 프로젝트를 마치고 다시 본 `VMS_v2`에는 구조적으로 명확한 두 가지 문제가 있었다.

### 문제 1 — 서버가 없으면 앱 자체에 진입할 수 없었다

```
VMS 실행 → 로그인(서버 인증 필수) → 서버가 등록한 CCTV 조회 → 메인 진입
```

로그인부터 장치 조회, CCTV 제어까지 전 과정이 서버를 거쳤다. 서버가 꺼져 있으면 로그인 화면 다음으로 넘어갈 방법이 없었다 — 카메라가 자체적으로 ID/PW 인증, RTSP 스트리밍, CGI 기반 제어(zoom/focus)를 이미 지원하고 있는데도, VMS가 카메라와 직접 통신할 경로 자체가 없었다.

### 문제 2 — 장치/채널 조회 API가 불필요하게 여러 번 호출됐다

기존 `DeviceService`는 아래 3단계를 전부 별도 HTTP 요청으로 처리했다.

```
fetchDevices()              — 디바이스 목록 (1회)
  └ fetchDeviceChannels()   — 디바이스별 채널 목록 (디바이스당 1회)
       └ fetchChannelDetail() — 채널별 RTSP/코덱 상세 (채널당 1회)
```

디바이스 2개·채널 2개 구성 기준으로 실측한 결과, 정상 케이스에서도 **7회**, 재시도가 겹치면 **최대 9회**의 HTTP 호출이 발생했다. "채널 목록"과 "채널 상세"가 원래 하나의 정보인데도 두 번 나눠 물어보는 구조였고, 배칭/캐싱도 없었다 — 이게 앱이 무거워진 원인 중 하나로 지목됐다.

---

## 2. 무엇을 바꿨는가

리팩토링의 방향은 "서버를 없애는 것"이 아니라 **"서버 의존성을 없애는 것"**이었다. VMS가 카메라(또는 카메라 역할을 하는 대상)와 직접 통신해서 핵심 기능이 돌아가게 만들고, 서버는 그 위에 선택적으로 얹히는 부가 기능(계정 동기화, 이벤트 히스토리)으로 내렸다.

### 2.1 서버 없이도 진입 가능하게 — 게스트 분기 (Phase 3a)

`LoginScreen`에 "게스트로 시작" 경로를 추가해 `AuthService::login(...)` 호출 없이 장치 확인 화면으로 진입할 수 있게 했다. 이 과정에서 코드를 뜯어보니, "채널 0개(카메라 없음)"로도 메인 화면까지 진입하는 걸 막는 가드가 **3곳**에 중복으로 걸려 있었다 — UI 클릭 핸들러, 라우팅 핸들러, RTSP 조회 결과 처리 로직 각각에. 이 세 곳을 "선택을 안 한 것"과 "선택했는데 조회가 실패한 것"을 구분하도록 고쳐서, 로그인 여부·카메라 존재 여부와 무관하게 메인 화면까지 도달할 수 있게 만들었다.

기존 로그인 경로(서버가 켜져 있는 경우)는 코드 한 줄도 건드리지 않고 그대로 유지했다 — 서비스 인터페이스는 유지하고 구현만 확장하는 원칙을 지켰다.

### 2.2 카메라와 직접 통신하는 경로 구축 (Phase 0 — 목업 카메라 호스트)

실제 카메라(RPi 등)를 당장 확보할 수 없는 상황이라, "카메라처럼 응답하는 대상"을 로컬 PC에서 직접 만들었다. `mock_camera_host/`에 VMS 앱 코드와 완전히 분리된 독립 빌드로 4개 컴포넌트를 C++/Qt로 구현:

- **ONVIF-lite mock** — WS-Discovery(UDP)/GetDeviceInformation/GetProfiles/GetStreamUri
- **CGI mock** — zoom/focus 제어 엔드포인트 (POST+JSON body). *이후 Phase 2에서 제어를 ONVIF PTZ/Imaging으로 바꾸면서 실제로는 안 쓰게 됐다 — 2.4절 참고. 삭제하지 않고 코드는 남겨둠.*
- **UDP 이벤트 브로드캐스터** — 카메라가 쏘는 더미 이벤트
- **mediamtx + ffmpeg** — 실제 RTSP 스트림 송출(루프 영상)

카메라 프로토콜(ONVIF-lite SOAP/RTSP/UDP)만 맞으면 이후 RPi나 실제 카메라로 그대로 교체 가능한 구조다.

### 2.3 API 호출 구조 개선 — ONVIF-lite 도입으로 라운드트립 축소 (Phase 1)

이 부분이 문제 2를 실제로 해결한 지점이다. 기존 서버 API를 그대로 카메라 쪽에 옮겨 쓰는 대신, 프로토콜 자체를 재검토했다.

**조사 결과**: ONVIF의 `GetProfiles` 응답은 채널 목록과 해상도/코덱 정보를 한 번에 반환한다 — 즉 기존에 "채널 목록 조회"와 "채널 상세 조회"로 나뉘어 있던 두 단계가 프로토콜 차원에서 원래 하나였다.

**구현**: `DeviceService` 뒤에 신규 `OnvifLiteClient`를 붙이고(기존 `DeviceService`의 public 메서드 시그니처는 무변경, `DeviceCheckScreen` 등 화면 코드도 무수정), 아래처럼 재구성했다.

```
discover()                    — WS-Discovery Probe (네트워크 전체, 1회)
  └ fetchDeviceProfiles()     — GetDeviceInformation + GetProfiles + 채널별 GetStreamUri
                                 (디바이스당 1세트, 결과를 uuid별로 캐싱)
```

`fetchDevices()` 시점에 위 시퀀스를 디바이스별로 병렬 실행해서 채널 목록·RTSP·코덱까지 전부 미리 캐싱해두기 때문에, 이후 화면에서 호출하는 `fetchDeviceChannels()`/`fetchChannelDetail()`은 **캐시만 읽고 새 네트워크 호출을 하지 않는다.** 결과적으로 디바이스 2개·채널 2개 기준 최초 7~9회였던 호출이, 최초 discovery+프로필 조회 시점에 한 번 몰리고 그 이후 화면 조작에서는 추가 네트워크 호출 없이 즉시 응답하는 구조로 바뀌었다.

이 결정을 내리기 전에 "그냥 SUNAPI(한화비전 CGI)로 가는 게 낫지 않을까"도 검토했는데, SUNAPI의 zoom/focus 파라미터는 파트너 포털에 gated돼 있어 공개 검증이 어려웠고, ONVIF의 실제 PTZ/zoom 제어는 CGI가 아니라 SOAP이라 오히려 복잡도가 늘었다. 그래서 discovery는 ONVIF-lite로 확정했고, 제어(zoom/focus)는 처음엔 "SUNAPI를 그대로 베끼지 않는 GET+query-param 스타일" 정도로 잠정 결정해뒀다 — 이 결정은 Phase 2에서 다시 뒤집힌다(2.4절).

### 2.4 제어(zoom/focus)도 표준 프로토콜로 — CGI 대신 ONVIF PTZ (Phase 2)

Phase 2 착수 시점에 "PTZ 제어 API가 카메라 벤더마다 다 다른데, 우리가 만드는 CGI 방식도 결국 아무 카메라도 안 쓰는 방언 아닌가?"라는 질문에서 다시 설계를 검토했다.

- 한화 SUNAPI, Axis VAPIX, Dahua CGI는 서로 호환되지 않는 벤더별 형식이다. Phase 2 착수 전 "SUNAPI를 그대로 베끼지 않는 GET+query-param 스타일" 정도로 잠정 결정해뒀던 것도(2.3절), 구현에 들어가기 전에 다시 보니 결국 우리 mock 하나만 아는 방언이 될 뿐이라 방향을 바꿨다.
- ONVIF는 PTZ(`RelativeMove`)/Imaging(`Move`) 서비스를 표준화해뒀다 — ONVIF Profile S를 지원하는 실제 카메라라면 공통으로 동작한다.
- 현재 UI는 "누르면 -100~100 중 한 스텝만큼 이동"하는 discrete 방식이라, "누르고 있는 동안 계속 이동"하는 `ContinuousMove`보다 "정해진 양만큼 이동 후 정지"하는 `RelativeMove`/Imaging `Move`(Relative)가 UX와 정확히 맞아떨어졌다.

그래서 discovery(Phase 1)와 제어(Phase 2)를 같은 ONVIF 프로토콜 계열로 통일했다. `CctvControlService`의 public 시그니처(`zoomStep`/`focusStep`, step 검증 -100~100)는 그대로 두고 내부 구현만 REST POST+JSON body → ONVIF SOAP `RelativeMove`/`Move`로 교체했다. channelId별로 필요한 ONVIF 주소/프로필 토큰은 discovery 시점에 이미 알아낸 정보를 `AppState`에 노출해서 재사용했다 — 서비스 간에 새로운 의존을 만들지 않고 "AppState를 접점으로 삼는다"는 원칙을 그대로 지킨 것.

### 2.5 뒤늦게 발견한 누락 — GetCapabilities (Phase 1/2 사후 보정)

Phase 1/2가 끝난 뒤 ONVIF 실제 통신 흐름을 다룬 외부 자료를 다시 보다가, 우리 구현이 표준 흐름의 한 단계를 통째로 건너뛰고 있다는 걸 발견했다.

**실제 ONVIF 흐름**: WS-Discovery로 Device Service 주소를 얻고 → Device Service에 `GetCapabilities`를 호출해 Media/PTZ/Imaging 등 각 기능별 서비스 주소를 얻고 → 그 주소로 `GetProfiles`/`GetStreamUri`(Media), `RelativeMove`(PTZ), `Move`(Imaging)를 호출한다. 서비스별로 주소가 다를 수 있다는 게 핵심.

**우리가 놓친 것**: `GetCapabilities` 호출 자체가 없었다. WS-Discovery로 받은 Device Service 주소를 Media/PTZ/Imaging 주소인 것처럼 그냥 재사용하고 있었다 — 우리 mock이 실제로 그렇게 동작하니 겉으로는 문제없이 돌아갔지만, "받은 주소를 따라간다"는 프로토콜의 핵심 동작 자체가 빠진 상태였다.

**gSOAP은 안 씀**: 실무 ONVIF C++ 구현은 보통 gSOAP(`wsdl2h`/`soapcpp2`로 WSDL에서 코드 생성)을 쓰지만, 이 프로젝트 스코프("ONVIF 풀스펙 구현 안 함, 최소만")에는 새 빌드 툴체인을 들이는 게 과하다고 판단해 기존 Qt 기반 SOAP 조립 방식 그대로 `GetCapabilities` 단계만 추가했다.

**수정**: `OnvifLiteClient`가 `GetDeviceInformation` 다음에 `GetCapabilities`를 호출해 Media/PTZ/Imaging XAddr을 각각 파싱하고, 이후 호출을 그 주소로 보내도록 변경. 검증을 위해 mock도 세 서비스 주소를 일부러 다른 경로(`/onvif/media_service`, `/onvif/ptz_service`, `/onvif/imaging_service`)로 응답하게 만들어서, 클라이언트가 실제로 그 주소를 따라가는지(하드코딩된 재사용이 아닌지) 확인했다.

---

## 3. 기술적으로 신경 쓴 지점

- **서비스 인터페이스 유지 + 구현만 교체.** `DeviceService`/`CctvControlService`의 public 시그니처를 그대로 두고 내부 통신 대상만 서버 → 카메라로 바꿈으로써, 화면/미디어 계층(`DeviceCheckScreen`, `ChannelSessionManager`, `MainScreen` 등)을 한 줄도 건드리지 않고 백엔드를 교체했다.
- **비동기 콜백 계약 보존.** 캐시로 응답을 앞당기면서도, 호출부(`mainwindow_auth.cpp`의 pending 카운터 패턴)가 "항상 비동기로 콜백이 온다"고 가정하고 있던 부분을 깨지 않기 위해 캐시 히트도 `QTimer::singleShot(0, ...)`으로 일관되게 비동기 디스패치했다.
- **결정을 내리기 전에 실측하고 리서치했다.** "느려진 것 같다"는 감이 아니라 실제 호출 체인을 코드로 추적해 호출 횟수를 세고, ONVIF/SUNAPI 스펙을 조사해서 근거를 확보한 뒤에 아키텍처를 바꿨다.
- **끝난 뒤에도 실제 스펙과 다시 비교했다.** Phase 1/2를 "완료"로 닫은 뒤에도 ONVIF 실제 흐름을 다룬 자료를 다시 읽다가 `GetCapabilities` 누락을 스스로 발견하고 되돌아가 고쳤다 — mock 기준으로는 이미 잘 동작하고 있었지만, "우리 mock에서만 통한다"와 "실제 스펙대로 동작한다"는 다르다고 판단했다.
- **로그인 여부가 데이터 흐름을 가르지 않게.** 게스트 상태에서도 카메라 데이터 경로(디바이스 조회, RTSP)는 로그인 사용자와 동일하게 동작한다 — 로그인은 자격증명 동기화 같은 부가 기능에만 영향을 준다.

---

## 4. Before / After

| | Before (팀 프로젝트 원본) | After (리팩토링 후) |
|---|---|---|
| 서버 없을 때 | 로그인 화면에서 진입 불가 | 게스트로 DeviceCheck → Main까지 진입 가능 |
| 카메라 데이터 경로 | 전부 서버 REST 프록시 | VMS가 카메라(목업/실카메라)와 직접 통신 |
| 디바이스 2개·채널 2개 조회 시 HTTP/SOAP 호출 | 7~9회 (매 조회마다 재발생) | discovery+프로필 조회 1세트 (최초 1회), 이후 화면 조작은 캐시 응답 |
| 채널 목록/상세 조회 | 별도 엔드포인트 2단계 | ONVIF `GetProfiles` 한 번으로 통합 |
| zoom/focus 제어 | 서버 REST 프록시(POST+JSON body) | 서버 상태와 무관하게 카메라로 직접 ONVIF PTZ/Imaging SOAP |
| ONVIF 서비스 주소 사용 | Device Service 주소를 Media/PTZ/Imaging에도 하드코딩 재사용 | `GetCapabilities`로 서비스별 실제 주소를 얻어 그 주소로 호출 |
| 화면/미디어 계층 코드 | — | 무수정 (서비스 인터페이스 유지 원칙) |

---

## 5. 남은 작업

- **Phase 3b** — 카메라별 로그인 자격증명을 QtKeychain으로 로컬 캐싱 (디바이스 선택 후 Main 진입 전 ID/PW 모달, 게스트=휘발성/회원=영구 저장)
- **Phase 4** — UDP 브로드캐스트 이벤트를 서버 없이 직접 수신
- **Phase 5(선택)** — Playback을 mediamtx 기반으로 전환
- **Phase 6** — 통합 검증 + 최종 문서/발표자료 정리

세부 진행 상황은 `docs/dev_execution_plan.md` 참고.

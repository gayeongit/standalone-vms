# standalone-vms 개발 로드맵

- 문서 성격: 전체 로드맵 (뼈대). Phase별 세부 실행계획은 `docs/dev_execution_plan.md`에 Phase 진입 직전 이어서 작성한다.
- 원본: [VEDA3-CLUE-VMS](https://github.com/gayeongit/VEDA3-CLUE-VMS) (팀 프로젝트, `v2` 브랜치)의 VMS 클라이언트 파트

---

## 0. 배경

팀 프로젝트 때 만든 VMS(`VMS_v2`)는 다음 흐름으로 동작했다.

```
VMS 실행 → 로그인 → 서버 인증 → 서버가 등록한 CCTV 조회 → 메인 진입
```

서버가 켜져 있지 않으면 VMS 자체에 들어갈 수 없었고, 서버에 CCTV가 등록/검색되지 않으면 메인 화면으로 넘어갈 수 없었다. 즉 VMS의 모든 기능이 서버를 거쳐야만 동작하는 구조였다.

프로젝트를 마치고 다시 보니, CCTV 자체가 이미 ID/PW 인증, RTSP 스트리밍, CGI 기반 제어(zoom/focus 등)를 자체적으로 지원하고 있어서 VMS가 카메라와 직접 통신할 수 있는 여지가 충분했다. 그런데 코드 구조상 `DeviceService` 등이 서버 응답을 파싱하는 역할에 머물러 있었고, 카메라 자체를 다루는 코드는 없었다.

이번 리팩토링의 목표는 서버를 없애는 게 아니라, VMS가 카메라와 직접 통신할 수 있게 만들고 서버는 그 위에 선택적으로 얹히는 부가 기능(계정, 이벤트 히스토리 등)으로 내리는 것이다.

## 1. 이번에 하려는 것 / 안 하려는 것

### 하려는 것
- 서버 없이 `VMS ↔ 목업 카메라 호스트` 만으로 로그인~멀티뷰~PTZ 제어~이벤트 알림까지 굴러가게 (목업 호스트는 지금은 로컬 PC/노트북에서 프로세스로 띄우고, 나중에 라즈베리파이나 실제 카메라로 교체 가능하게 만든다 — 3.6절 참고)
- 서버를 켜면 기존처럼 계정/자격증명 동기화/이벤트 히스토리도 계속 쓸 수 있게 — 없애는 게 아니라 얹는 구조로
- 기존에 이미 잘 짜여 있던 화면/미디어 계층(`ChannelSessionManager`, `StreamPlayer`, `MainScreen` 등)은 최대한 안 건드리기

### 안 하려는 것
- ONVIF 풀스펙 구현 — Discovery/GetStreamUri/PTZ 정도만 최소로
- 실제 짐벌/모터 하드웨어 제어 — 목업 호스트가 OK 응답만 주면 됨
- UGV 관련 기능 전체 — 이번 범위 밖, 따로 정리
- Playback 영상 퀄리티 — 프로토콜 흐름만 맞으면 됨, 콘텐츠는 더미로
- 자체 암호화 구현 — QtKeychain 같은 OS 네이티브 저장소로 대체

## 2. 설계 원칙

1. **`AppState`를 접점으로 삼는다.** `selectedChannelContexts`, `channelRtspByName/Id`, `channelVideoCodecByName/Id` 같은 기존 필드를 그대로 채우는 방식으로 새 입구를 만든다. 화면/미디어 계층은 건드릴 이유가 없다.
2. **서비스 인터페이스는 유지하고 구현만 바꾼다.** `CctvControlService`, `DeviceService` 같은 클래스는 시그니처 그대로 두고 내부 통신 대상만 서버 → 카메라로 바꾼다.
3. **로그인 여부가 "데이터가 오는지 안 오는지"를 가르면 안 된다.** 로그인 안 해도 카메라 데이터는 항상 VMS가 직접 받는다. 로그인하면 자격증명 동기화나 이벤트 히스토리처럼 편의 기능이 얹힐 뿐.
4. **목업 카메라 호스트는 진짜 CCTV처럼 만들 필요 없다.** HTTP 응답, 더미 UDP 이벤트, mediamtx 스트림 정도면 충분하다. 처음엔 라즈베리파이에 올릴 생각이었지만, 실제로 하드웨어일 필요는 없어서 지금은 로컬 PC(또는 같은 네트워크의 다른 기기)에서 일반 프로세스로 띄운다. 목적은 VMS 쪽 로직 검증이지 카메라 재현이 아니다.

## 3. 목표 아키텍처 (요약)

```
                Qt VMS (v2)
        ┌─────────────────────────┐
        │ DeviceService (신규 입구) │──ONVIF-lite SOAP──▶ 목업 호스트(로컬 PC) (onvif_srvd 등)
        │ CctvControlService       │──ONVIF PTZ/Imaging SOAP──▶ 목업 호스트(로컬 PC) (zoom/focus mock)
        │ 신규: LocalEventListener │◀─UDP broadcast──────  목업 호스트(로컬 PC) (더미 이벤트 송신)
        │ PlaybackService(신규 경로)│──HTTP + mediamtx────▶ 목업 호스트(로컬 PC) + mediamtx (더미 영상)
        └─────────────────────────┘
                    │ (선택적)
                    ▼
        AuthService / EventService / WsClient / RestClient
                    │
                    ▼
                 기존 Server (그대로 유지, 켜져 있으면 로그인 모드로 활성화)
```

## 3.5 진행 순서 변경 (2026-08-17)

RPi를 당장 쓸 수 없는 상황이라 Phase 0(RPi 테스트 베드)부터 시작하는 원래 순서를 못 밟았다. 그래서 카메라 의존성이 없는 작업(Phase 3a)을 먼저 진행했다. Phase 3a는 2026-09-03 완료. RPi 확보 자체의 이슈는 3.6절에서 별도로 해결.

**변경 내용 (당시)**

- Phase 3("로그인/게스트 분기 + 로컬 자격증명 캐시")를 둘로 쪼갬.
  - **3a. 로그인/게스트 분기** — 카메라 없이도 가능. 선행조건 없음. → **완료.**
  - **3b. 로컬 자격증명 캐시(QtKeychain)** — 카메라별 ID/PW를 저장하는 작업이라 실제 카메라 연결이 있어야 의미가 있음. Phase 1 이후로 유지.
- Phase 0/1(카메라 테스트 베드, 카메라 자동 탐색)은 당시 RPi 확보 전까지 대기 상태였음 → 3.6절에서 해제.

**"안 쓸 부분 쳐내기"에 대한 방침**

- UGV, ONVIF 풀스펙 등 "안 하려는 것"에 속한 부분이라도 지금 단계에서 물리적으로 삭제하지는 않는다. `UgvScreen`/`UgvService`는 `showScreen`, `activeChannelsForScreen`, `createRuntimeScreens` 등 라우팅 핵심부에 얽혀 있어서, 지금 지웠다가 나중에 다시 필요해지면 재작업 비용이 크고 Main 진입 흐름 자체가 불안정해질 위험이 있음.
- 대신 이번 단계에서는 **비활성화/스텁 처리** 위주로 간다 (예: UGV 관련 화면 생성을 조건부로 스킵). 실제 삭제는 최종 구조가 확정되는 Phase 6(통합 검증)에서 정리한다.

## 3.6 RPi 제약 해결 — 로컬 목업 호스트로 전환 (2026-09-06)

라즈베리파이를 자유롭게 못 쓰는 상황이 계속돼서 Phase 0/1/2/4/5가 "RPi 확보 전까지 대기" 상태로 묶여 있었다. 다시 보니 설계 원칙 4번에서 이미 "카메라 역할을 하는 대상이 진짜 하드웨어일 필요는 없다"고 정리해뒀던 부분이라, 굳이 하드웨어를 기다릴 이유가 없었다.

**검토한 옵션**

- **클라우드(AWS/Azure)** — 기각. Phase 1(WS-Discovery)과 Phase 4(이벤트 UDP broadcast)는 둘 다 "같은 로컬 네트워크"를 전제로 하는 프로토콜이라, 멀티캐스트/브로드캐스트가 인터넷 너머로 안 나가는 클라우드에서는 검증 자체가 안 됨. 비용도 불필요.
- **로컬 Docker** — 보류. 가능은 하지만 특히 Windows Docker Desktop은 컨테이너 네트워크가 WSL2/Hyper-V를 거쳐서 UDP 브로드캐스트/멀티캐스트가 실제 LAN과 다르게 동작하는 경우가 있음. 지금 검증하려는 게 바로 그 디스커버리/브로드캐스트 동작이라, 지금 단계에서 도입하면 오히려 디버깅 변수만 늘어남. 목업 스택이 로컬에서 검증되고 난 뒤, 패키징 편의를 위해 나중에 감싸는 용도로는 고려 가능.
- **로컬 환경에서 직접 세팅 (채택)** — mediamtx + Flask + UDP 브로드캐스트 스크립트를 로컬 PC(또는 같은 네트워크의 다른 노트북)에서 일반 프로세스로 띄운다. 하드웨어 불필요, 비용 없음, 실제 LAN 환경과 가장 가까움.

**결정.** 목업 카메라 호스트를 로컬 PC에서 직접 실행하는 방식으로 전환한다. RPi는 나중에 확보되면 같은 역할을 하는 호스트로 그대로 교체 가능 — VMS 쪽 코드는 호스트가 뭐든 동일한 프로토콜(ONVIF-lite SOAP/HTTP CGI/RTSP/UDP broadcast)로만 통신하므로 설계에 영향 없음.

**영향**

- Phase 0/1/2/4/5를 막고 있던 "RPi 확보 전까지 대기" 상태 해제. 순서대로 진행 가능.
- 문서 전반의 "RPi" 표현은 "목업 호스트"로 일반화. 실제 라즈베리파이를 가리킬 때만 "RPi"라고 명시한다.
- Phase 0의 작업 내용(onvif_srvd/Flask/mediamtx/UDP broadcast) 자체는 변경 없음 — 어디서(어떤 장치에서) 실행하느냐만 바뀐다.

## 4. Phase 구성

| Phase | 내용 | 선행조건 | 상태 |
|---|---|---|---|
| 0 | 목업 카메라 호스트 구축 (로컬 PC) | 없음 | 완료 (2026-09-08) |
| 1 | 카메라 자동 탐색 (ONVIF-lite Discovery) | Phase 0 | 완료 (2026-09-08) |
| 2 | CctvControlService 직접 제어 전환 | Phase 1 | 완료 (2026-09-09) |
| 3a | 로그인/게스트 분기 (카메라 무관) | 없음 | 완료 (2026-09-03) |
| 3b | 로컬 자격증명 캐시 | Phase 1 | **대기 (다음 작업)** |
| 4 | 이벤트 직접 수신 경로 | Phase 0 | 대기 |
| 5 | Playback (mediamtx 기반, 더미 콘텐츠) | Phase 1 | 대기 (선택) |
| 6 | 통합 검증 + 문서 정리 (UGV 등 미사용 코드 최종 정리 포함) | 전체 | 대기 |

Phase 3b, 4는 서로 독립적이라 순서 바꿔도 무방. Phase 5는 시간 나면.

---

### Phase 0 — 목업 카메라 호스트 구축 (로컬 PC, 완료)

**하려는 것.** VMS가 "카메라"라고 부를 수 있는 대상을 로컬 네트워크에 만든다. 원래는 라즈베리파이에 올릴 생각이었지만, 하드웨어를 자유롭게 못 쓰는 상황이라 지금은 로컬 PC(또는 같은 네트워크의 다른 노트북)에서 그냥 프로세스로 띄운다. 나중에 라즈베리파이나 실제 카메라가 생기면 같은 역할의 호스트로 교체하면 된다 — VMS 쪽 코드는 호스트가 뭐든 동일한 프로토콜로 통신하므로 영향 없음.

- onvif_srvd(또는 동급)로 ONVIF Discovery/디바이스 프로파일 최소 응답 → C++/Qt로 직접 구현(`onvif_mock.cpp`)
- Flask 등으로 CGI 스타일 zoom/focus/PTZ 엔드포인트 (OK 응답만) → C++/Qt로 직접 구현(`cgi_mock.cpp`)
- mediamtx로 RTSP 송출 (미리 구한 영상 파일을 루프 재생) → mediamtx + ffmpeg(`runOnInit`)
- 더미 이벤트 UDP broadcast 송신 스크립트 → C++/Qt로 직접 구현(`event_broadcaster.cpp`)

**확인할 것.** Wireshark/curl로 각 엔드포인트 응답 확인, RTSP를 VLC 등 외부 플레이어로 재생 확인.

**상태.** 완료 (2026-09-08). 언어/스택 결정(Python/Node 대신 C++/Qt), 실제 게이트 테스트 결과는 `docs/dev_execution_plan.md`의 Phase 0 섹션 참고.

---

### Phase 1 — 카메라 자동 탐색 (완료)

**하려는 것.** `DeviceService`가 서버 REST 대신 ONVIF-lite로도 `AppState`의 채널 맵을 채울 수 있게 한다. 이게 이번 리팩토링에서 제일 핵심인 부분이다.

- 최소 SOAP: WS-Discovery(UDP), GetStreamUri
- 신규 클래스(가칭 `OnvifLiteClient`)를 `DeviceService` 뒤에 붙이는 구조 — `DeviceCheckScreen`은 무수정
- 서버 있음/없음에 따라 `DeviceService`가 소스를 분기

**확인할 것.** 서버 없이 DeviceCheck 화면에서 목업 호스트가 채널로 잡히고, Main 화면 멀티뷰에서 실제 RTSP 재생됨.

**상태.** 완료(2026-09-08) — `OnvifLiteClient` 신설, `DeviceService`가 `device.source`(기본 `"onvif"`)로 분기. 게스트 모드로 목업 호스트 대상 DeviceCheck→Main 재생까지 사용자 확인 완료. 세부 내용은 `docs/dev_execution_plan.md`의 Phase 1 섹션 참고.

---

### Phase 2 — CctvControlService 전환 (완료)

**하려는 것.** zoom/focus 요청이 서버 프록시 없이 카메라(목업)로 직접 가게.

**결정 변경 (2026-09-09).** 처음엔 "HTTP CGI" 방식(Phase 0의 `cgi_mock.cpp`, POST+JSON body)으로 계획했으나 재검토 후 **ONVIF PTZ/Imaging SOAP**으로 변경. 이유:
- PTZ CGI는 벤더마다 형식이 다 다름(한화 SUNAPI/Axis VAPIX/Dahua 등 서로 호환 안 됨) — "우리만 아는 방언"을 만드는 것과 다름없음.
- ONVIF는 PTZ(`RelativeMove`)/Imaging(`Move`) 서비스가 표준화돼 있어 ONVIF Profile S를 지원하는 실제 카메라와도 호환됨.
- 지금 UI는 "누르면 한 스텝"(-100~100 discrete) 방식이라 `ContinuousMove`(누르고 있는 동안 계속)보다 `RelativeMove`/Imaging `Move`(Relative, 정해진 만큼 이동 후 정지)가 정확히 맞음.
- Phase 1에서 만든 `OnvifLiteClient`의 SOAP 전송 인프라를 그대로 재사용 가능.

작업:
- `zoomStep(channelId, value)` → PTZ `RelativeMove` (value/100을 Zoom 축 상대 이동량으로 변환)
- `focusStep(channelId, value)` → Imaging `Move` (Relative)
- 기존 step 검증 로직(-100~100) 유지
- `channelId` → ONVIF `xaddr`/profile token 매핑을 `DeviceService` 내부에서 `AppState`로 노출(설계 원칙 1) — `CctvControlService`가 이걸 읽어서 SOAP 대상 결정. 별도 base URL 설정 불필요(discovery 때 이미 알아낸 주소 재사용)
- `cgi_mock.cpp`(Phase 0에서 만든 CGI mock)는 이번 Phase에서 안 씀 — 삭제하지 않고 남겨두되, 최종 정리는 Phase 6에서

**확인할 것.** `CctvScreen`에서 zoom/focus 조작 시 목업 호스트(`onvif_mock`)가 OK로 응답, UI 상태라벨 정상 반영.

**상태.** 완료 (2026-09-09) — `onvif_mock` 콘솔에 `PTZ RelativeMove`/`Imaging Move` 로그로 step 값(±1/±10/±100 → ±0.01/±0.1/±1.0) 정확한 스케일링 확인. 세부 내용은 `docs/dev_execution_plan.md`의 Phase 2 섹션 참고.

---

### Phase 3a — 로그인/게스트 분기 (카메라 무관, 완료)

**하려는 것.** 로그인 없이도 메인 진입이 되게. 카메라가 아직 없어도(선택 채널 0개) `DeviceCheck` → `Main`까지 도달 가능하게 만드는 부분만 먼저 뗀다.

- 로그인 스킵 경로 추가 (게스트 진입점) — `mainwindow_auth.cpp`
- `DeviceCheckScreen`에서 채널 0개여도 "VMS 시작" 진행 가능하도록 확인/보완
- `createRuntimeScreens(...)` → `showScreen(Main)`이 빈 `gridCells`로도 정상 동작하는지 검증
- 로그인 경로/서버 연동 로직은 그대로 유지 (서버 켜져 있으면 기존과 동일하게 동작해야 함)
- UGV 등 지금 범위 밖인 화면/서비스는 삭제하지 않고 비활성화·스텁 처리로 대응

**확인할 것.** 게스트로 로그인 없이, 카메라 하나도 안 잡힌 상태에서 Main 화면까지 진입되는지. 로그인 경로도 기존과 동일하게 동작하는지.

**상태.** 완료 (2026-09-03). 세부 내용은 `docs/dev_execution_plan.md`의 Phase 3a 섹션 참고.

---

### Phase 3b — 로컬 자격증명 캐시 (Phase 1 이후)

**하려는 것.** 카메라별 자격증명은 로컬(QtKeychain)에 캐시. 실제 카메라 연결이 전제이므로 Phase 1 완료 후 진행.

**구체 흐름 (2026-09-09, Phase 2 작업 중 설계 논의에서 정리)**

- `DeviceCheckScreen`의 디바이스→채널 트리는 이미 discovery(Phase 1) 결과 기반으로 존재 — 무수정.
- 채널 선택 → "VMS 시작" 눌러서 Main 진입하기 **전**, 아직 자격증명이 캐시되지 않은 장치가 있으면 장치별 ID/PW 입력 모달을 띄운다.
- 입력받은 자격증명은 이후 그 장치를 대상으로 하는 카메라 제어 요청(zoom/focus 등 PTZ, Phase 2에서 ONVIF SOAP으로 전환됨)에 사용한다.
- **게스트 모드**: 자격증명은 휘발성 — 앱을 재시작하거나 로그인 화면으로 돌아가면 다시 입력해야 함.
- **로그인 상태**: QtKeychain에 영구 저장 — 한 번 입력한 장치는 이후 재연결 시 자동 사용, 최초 연결 장치만 입력 필요.
- 로그인한 경우 서버 동기화는 선택적 계층으로 유지 (기존 `AuthService` 그대로 활용)

**확인할 것.** 게스트로 로그인 없이 카메라 접속 시 매번 재입력 필요, 로그인 상태에서는 최초 1회만 입력하고 재접속 시 비번 재입력 없음.

---

### Phase 4 — 이벤트 직접 수신

**하려는 것.** 팀 프로젝트 때 와이어샤크로 보면 CCTV가 딱히 정해진 대상 없이 이벤트를 broadcast하고 있었는데, 이걸 서버 없이도 VMS가 직접 받아서 `EventViewWidget`에 표시.

- 신규 `LocalEventListener` (QUdpSocket, broadcast bind)
- 수신 데이터 → 기존 `EventInfo`로 매핑 → `EventUiHelpers` 파이프라인 재사용
- 로그인 모드에서는 기존 `EventService`(REST+WS, 히스토리/캐시 200개)와 병행 가능하게 유지

**확인할 것.** 게스트 모드에서 목업 호스트가 쏘는 더미 이벤트가 실시간으로 이벤트뷰에 표시됨. 앱 재시작 시 히스토리는 없음(의도된 동작).

---

### Phase 5 — Playback (선택)

**하려는 것.** SUNAPI 느낌의 단순 REST로 목업 호스트가 "이 시간대 영상 URL"을 응답 → mediamtx로 서빙 → `PlaybackService`가 그대로 소비.

- 목업 호스트에 더미 mp4 몇 개 준비, 시간대별 매핑 REST
- `PlaybackService` 응답 파싱 부분만 신규 소스에 맞게 확장
- 타임라인/마커 UI는 무수정

**확인할 것.** Playback 화면에서 날짜/채널 선택 → 더미 영상 재생, 마커 클릭 시 해당 시점 재생.

---

### Phase 6 — 통합 검증 + 문서 정리

- 서버 켠 상태 / 끈 상태 둘 다 정상 동작하는지 체크리스트로 확인
- Phase 3a에서 스텁/비활성화로만 처리했던 UGV 등 미사용 코드를 이 시점에 최종 정리(삭제 여부 판단)
- README/발표자료 갱신 (기존 `VMS_v2_presentation_material.md` 참고해서 재구성)
- 아키텍처 변경 전/후 다이어그램 정리

---

## 5. 아직 열려있는 리스크

- Phase 1: WS-Discovery(UDP multicast)가 실제 환경(공유기, OS 방화벽 등)에서 막힐 가능성 — 안 되면 수동 IP 입력 fallback 필요. VMS와 목업 호스트를 같은 PC 한 대에서 띄우는 경우 loopback 멀티캐스트가 막혀 있을 수 있으니, 안 되면 같은 네트워크의 다른 기기로 분리해서 테스트
- Phase 4: broadcast 수신이 OS 방화벽에 막힐 수 있음 — 개발 중 확인 필요 (Windows 방화벽에서 해당 포트/프로그램 인바운드 허용 확인)
- Phase 5: mediamtx가 파일 기반 반복 재생을 실제 "재생 시점 탐색"처럼 보이게 하려면 약간의 트릭(파일을 시간 오프셋별로 나눠 저장 등)이 필요할 수 있음

## 6. 다음 액션

Phase 0부터 로컬 PC에 목업 카메라 호스트(mediamtx + Flask + UDP broadcast 스크립트)를 세팅하고 진행. `docs/dev_execution_plan.md`에 Phase 0 세부 실행계획(작업 항목, 예상 파일, 체크리스트)을 이어서 작성하고 시작.

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
- 서버 없이 `VMS ↔ RPi(카메라 대역)` 만으로 로그인~멀티뷰~PTZ 제어~이벤트 알림까지 굴러가게
- 서버를 켜면 기존처럼 계정/자격증명 동기화/이벤트 히스토리도 계속 쓸 수 있게 — 없애는 게 아니라 얹는 구조로
- 기존에 이미 잘 짜여 있던 화면/미디어 계층(`ChannelSessionManager`, `StreamPlayer`, `MainScreen` 등)은 최대한 안 건드리기

### 안 하려는 것
- ONVIF 풀스펙 구현 — Discovery/GetStreamUri/PTZ 정도만 최소로
- 실제 짐벌/모터 하드웨어 제어 — RPi가 OK 응답만 주면 됨
- UGV 관련 기능 전체 — 이번 범위 밖, 따로 정리
- Playback 영상 퀄리티 — 프로토콜 흐름만 맞으면 됨, 콘텐츠는 더미로
- 자체 암호화 구현 — QtKeychain 같은 OS 네이티브 저장소로 대체

## 2. 설계 원칙

1. **`AppState`를 접점으로 삼는다.** `selectedChannelContexts`, `channelRtspByName/Id`, `channelVideoCodecByName/Id` 같은 기존 필드를 그대로 채우는 방식으로 새 입구를 만든다. 화면/미디어 계층은 건드릴 이유가 없다.
2. **서비스 인터페이스는 유지하고 구현만 바꾼다.** `CctvControlService`, `DeviceService` 같은 클래스는 시그니처 그대로 두고 내부 통신 대상만 서버 → 카메라로 바꾼다.
3. **로그인 여부가 "데이터가 오는지 안 오는지"를 가르면 안 된다.** 로그인 안 해도 카메라 데이터는 항상 VMS가 직접 받는다. 로그인하면 자격증명 동기화나 이벤트 히스토리처럼 편의 기능이 얹힐 뿐.
4. **RPi는 진짜 CCTV처럼 만들 필요 없다.** HTTP 응답, 더미 UDP 이벤트, mediamtx 스트림 정도면 충분하다. 지금 하드웨어 여력이 안 되기도 하고, VMS 쪽 로직을 보는 게 목적이라 카메라를 정교하게 재현할 필요는 없다.

## 3. 목표 아키텍처 (요약)

```
                Qt VMS (v2)
        ┌─────────────────────────┐
        │ DeviceService (신규 입구) │──ONVIF-lite SOAP──▶ RPi (onvif_srvd 등)
        │ CctvControlService       │──HTTP CGI──────────▶ RPi (zoom/focus/PTZ mock)
        │ 신규: LocalEventListener │◀─UDP broadcast──────  RPi (더미 이벤트 송신)
        │ PlaybackService(신규 경로)│──HTTP + mediamtx────▶ RPi + mediamtx (더미 영상)
        └─────────────────────────┘
                    │ (선택적)
                    ▼
        AuthService / EventService / WsClient / RestClient
                    │
                    ▼
                 기존 Server (그대로 유지, 켜져 있으면 로그인 모드로 활성화)
```

## 3.5 진행 순서 변경 (2026-08-17)

RPi를 당장 쓸 수 없는 상황이라 Phase 0(RPi 테스트 베드)부터 시작하는 원래 순서를 못 밟는다. 그래서 카메라 의존성이 없는 작업부터 먼저 진행하기로 함.

**변경 내용**

- Phase 3("로그인/게스트 분기 + 로컬 자격증명 캐시")를 둘로 쪼갠다.
  - **3a. 로그인/게스트 분기** — 카메라 없이도 지금 바로 가능. 선행조건 없음. **다음 작업으로 진행.**
  - **3b. 로컬 자격증명 캐시(QtKeychain)** — 카메라별 ID/PW를 저장하는 작업이라 실제 카메라(RPi)가 있어야 의미가 있음. Phase 1 이후로 유지.
- Phase 0/1(RPi 테스트 베드, 카메라 도메인 입구)은 RPi 확보 전까지 대기.

**3a 작업 범위**

- 로그인 없이 `DeviceCheck`까지 진입하는 게스트 경로 추가 (`mainwindow_auth.cpp` 쪽)
- `DeviceCheckScreen`에서 선택 채널 0개(카메라 없음)여도 "VMS 시작"이 눌리도록 — Main 진입이 채널 존재를 전제로 하지 않는지 확인 후 필요하면 완화
- `AppState.selectedChannelContexts` / `gridCells`가 빈 상태로 `createRuntimeScreens` → `showScreen(Main)`까지 정상 도달하는지 확인
- 로그인 경로는 기존 그대로 유지 (서버 켜져 있으면 기존 흐름 그대로 동작해야 함 — 설계 원칙 2, 3 참고)

**"안 쓸 부분 쳐내기"에 대한 방침**

- UGV, ONVIF 풀스펙 등 "안 하려는 것"에 속한 부분이라도 지금 단계에서 물리적으로 삭제하지는 않는다. `UgvScreen`/`UgvService`는 `showScreen`, `activeChannelsForScreen`, `createRuntimeScreens` 등 라우팅 핵심부에 얽혀 있어서, 지금 지웠다가 나중에 다시 필요해지면 재작업 비용이 크고 Main 진입 흐름 자체가 불안정해질 위험이 있음.
- 대신 이번 단계에서는 **비활성화/스텁 처리** 위주로 간다 (예: UGV 관련 화면 생성을 조건부로 스킵). 실제 삭제는 최종 구조가 확정되는 Phase 6(통합 검증)에서 정리한다.

## 4. Phase 구성

| Phase | 내용 | 선행조건 | 상태 |
|---|---|---|---|
| 0 | RPi 테스트 베드 구축 | 없음 | 보류 (RPi 확보 전까지 대기) |
| 1 | 카메라 도메인 입구 (ONVIF-lite Discovery) | Phase 0 | 대기 |
| 2 | CctvControlService 직접 제어 전환 | Phase 1 | 대기 |
| 3a | 로그인/게스트 분기 (카메라 무관) | 없음 | **완료 (2026-09-03)** |
| 3b | 로컬 자격증명 캐시 | Phase 1 | 대기 |
| 4 | 이벤트 직접 수신 경로 | Phase 0 | 대기 |
| 5 | Playback (mediamtx 기반, 더미 콘텐츠) | Phase 1 | 대기 (선택) |
| 6 | 통합 검증 + 문서 정리 (UGV 등 미사용 코드 최종 정리 포함) | 전체 | 대기 |

Phase 3b, 4는 서로 독립적이라 순서 바꿔도 무방. Phase 5는 시간 나면.

---

### Phase 0 — RPi 테스트 베드

**하려는 것.** VMS가 "카메라"라고 부를 수 있는 대상을 로컬 네트워크에 만든다. 팀 프로젝트 때 쓰던 실제 CCTV가 지금은 없으니 대체재를 마련하는 단계.

- onvif_srvd(또는 동급)로 ONVIF Discovery/디바이스 프로파일 최소 응답
- Flask 등으로 CGI 스타일 zoom/focus/PTZ 엔드포인트 (OK 응답만)
- mediamtx로 RTSP 송출 (파이카메라 라이브 또는 루프 영상)
- 더미 이벤트 UDP broadcast 송신 스크립트

**확인할 것.** Wireshark/curl로 각 엔드포인트 응답 확인, RTSP를 VLC 등 외부 플레이어로 재생 확인.

---

### Phase 1 — 카메라 도메인 입구

**하려는 것.** `DeviceService`가 서버 REST 대신 ONVIF-lite로도 `AppState`의 채널 맵을 채울 수 있게 한다. 이게 이번 리팩토링에서 제일 핵심인 부분이다.

- 최소 SOAP: WS-Discovery(UDP), GetStreamUri
- 신규 클래스(가칭 `OnvifLiteClient`)를 `DeviceService` 뒤에 붙이는 구조 — `DeviceCheckScreen`은 무수정
- 서버 있음/없음에 따라 `DeviceService`가 소스를 분기

**확인할 것.** 서버 없이 DeviceCheck 화면에서 RPi가 채널로 잡히고, Main 화면 멀티뷰에서 실제 RTSP 재생됨.

---

### Phase 2 — CctvControlService 전환

**하려는 것.** zoom/focus/PTZ 요청이 서버 프록시 없이 RPi CGI로 직접 가게.

- 기존 step 검증 로직(-100~100) 유지
- base URL만 서버 → RPi로 교체 (설정 가능하게)

**확인할 것.** `CctvScreen`에서 zoom/focus 조작 시 RPi 목업이 OK 응답, UI 상태라벨 정상 반영.

---

### Phase 3a — 로그인/게스트 분기 (카메라 무관, 완료)

**하려는 것.** 로그인 없이도 메인 진입이 되게. 카메라가 아직 없어도(선택 채널 0개) `DeviceCheck` → `Main`까지 도달 가능하게 만드는 부분만 먼저 뗀다.

- 로그인 스킵 경로 추가 (게스트 진입점) — `mainwindow_auth.cpp`
- `DeviceCheckScreen`에서 채널 0개여도 "VMS 시작" 진행 가능하도록 확인/보완
- `createRuntimeScreens(...)` → `showScreen(Main)`이 빈 `gridCells`로도 정상 동작하는지 검증
- 로그인 경로/서버 연동 로직은 그대로 유지 (서버 켜져 있으면 기존과 동일하게 동작해야 함)
- UGV 등 지금 범위 밖인 화면/서비스는 삭제하지 않고 비활성화·스텁 처리로 대응

**확인할 것.** 게스트로 로그인 없이, 카메라 하나도 안 잡힌 상태에서 Main 화면까지 진입되는지. 로그인 경로도 기존과 동일하게 동작하는지.

---

### Phase 3b — 로컬 자격증명 캐시 (Phase 1 이후)

**하려는 것.** 카메라별 자격증명은 로컬(QtKeychain)에 캐시. 실제 카메라(RPi) 연결이 전제이므로 Phase 1 완료 후 진행.

- 최초 연결 시 카메라 ID/PW 입력 → QtKeychain 저장
- 이후 재연결 시 자동 사용
- 로그인한 경우 서버 동기화는 선택적 계층으로 유지 (기존 `AuthService` 그대로 활용)

**확인할 것.** 게스트로 로그인 없이 카메라 접속·재접속(비번 재입력 없음) 확인.

---

### Phase 4 — 이벤트 직접 수신

**하려는 것.** 팀 프로젝트 때 와이어샤크로 보면 CCTV가 딱히 정해진 대상 없이 이벤트를 broadcast하고 있었는데, 이걸 서버 없이도 VMS가 직접 받아서 `EventViewWidget`에 표시.

- 신규 `LocalEventListener` (QUdpSocket, broadcast bind)
- 수신 데이터 → 기존 `EventInfo`로 매핑 → `EventUiHelpers` 파이프라인 재사용
- 로그인 모드에서는 기존 `EventService`(REST+WS, 히스토리/캐시 200개)와 병행 가능하게 유지

**확인할 것.** 게스트 모드에서 RPi가 쏘는 더미 이벤트가 실시간으로 이벤트뷰에 표시됨. 앱 재시작 시 히스토리는 없음(의도된 동작).

---

### Phase 5 — Playback (선택)

**하려는 것.** SUNAPI 느낌의 단순 REST로 RPi가 "이 시간대 영상 URL"을 응답 → mediamtx로 서빙 → `PlaybackService`가 그대로 소비.

- RPi에 더미 mp4 몇 개 준비, 시간대별 매핑 REST
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

- Phase 1: WS-Discovery(UDP multicast)가 실제 환경(집 공유기 등)에서 막힐 가능성 — 안 되면 수동 IP 입력 fallback 필요
- Phase 4: broadcast 수신이 OS 방화벽에 막힐 수 있음 — 개발 중 확인 필요
- Phase 5: mediamtx가 파일 기반 반복 재생을 실제 "재생 시점 탐색"처럼 보이게 하려면 약간의 트릭(파일을 시간 오프셋별로 나눠 저장 등)이 필요할 수 있음

## 6. 다음 액션

Phase 3a부터 `docs/dev_execution_plan.md`에 세부 실행계획(작업 항목, 예상 파일, 체크리스트)을 이어서 작성하고 시작. RPi 확보되면 Phase 0으로 복귀.

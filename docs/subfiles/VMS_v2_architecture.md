# VMS v2 아키텍처 설계서 (최종 통합본)

- 최종 갱신: 2026-03-06
- 상태: 설계 확정 중
- 기반: `VMS_v2_decisions.md`, `VMS_v1_handover_v1_10b_final.md`, REST/WS API CSV, `VMS_v2_phase0_design.md`, `VMS_흐름정의서_v9.md`

> 이 문서는 v2 전체 아키텍처를 **하나의 문서**로 고정합니다.
> 서버팀 합의/프론트 결정 대기 항목은 별도 문서 → `VMS_v2_pending_decisions.md`

---

## 1. 설계 원칙 (문서 근거)

| 원칙 | 근거 |
|------|------|
| v1 리모델링 (신규 프로젝트 아님) | decisions §1.3 |
| 클라이언트 → DB 직접 접근 금지, 서버 API/JWT 경유 | decisions §8.1.1 |
| 영상 `appsink + QOpenGLWidget` 우선 전환 (1순위) | decisions §10 |
| v1 재사용 축: `MainWindow + AppState + Screens` | handover §4 |
| 화면은 서비스 호출만, API/WS 직접 호출 금지 (RTSP/HLS는 Media Layer 예외) | 본 문서 신규 원칙 |
| 토큰 메모리 only, 디스크 영속화 금지 | decisions §7.2 |
| 설정 저장: QSettings 유지 | decisions §12 (2026-03-06) |

### 보안 적용 범위 (경계 기준)

| 구간 | 프로토콜 | OpenSSL(TLS) | JWT | 비고 |
|------|----------|--------------|-----|------|
| VMS Client ↔ 서버 REST API | `https://` | ✅ 적용 | ✅ 적용 | `Authorization: Bearer` |
| VMS Client ↔ 서버 WebSocket | `wss://` | ✅ 적용 | ✅ 적용 | `Authorization: Bearer <token>` 헤더 사용 |
| VMS Client ↔ mediaMTX RTSP 스트림 | `rtsp://` (기본) | 기본 미적용 | 미적용 | URL은 서버 API(`/channel/{id}`, JWT)로 획득 |
| VMS Client ↔ mediaMTX Playback API | 사용 안 함 | N/A | N/A | v2 정책상 **직접 호출 금지** (`/list`, `/get`) |
| 서버 ↔ mediaMTX (내부) | localhost/내부망 | 기본은 내부망 보호 | 기본은 미적용 | 서버 내부 리소스로 운영 |
| 서버 ↔ 카메라 CGI/RTSP/ONVIF | 장치 프로토콜별 상이 | 환경별 | JWT 미적용 | 카메라 인증(ID/PW) 별도 |

> mediaMTX 보안/운영 세부사항은 별도 문서: `VMS_v2_mediamtx.md`

---

## 2. 계층 아키텍처

```
┌─────────────────────────────────────────────────────────────┐
│                      UI Layer                                │
│  LoginScreen │ SignupScreen │ DeviceCheckScreen               │
│  MainScreen │ CctvScreen │ UgvScreen │ PlaybackScreen        │
│  SettingsDialog │ PopupManager                                │
│                                                               │
│  ※ 화면은 Service만 호출. QNetworkAccessManager/QWebSocket    │
│    직접 사용 금지. (예외: GStreamer RTSP/HLS는 Media Layer)    │
├───────────────────────────────────────────────────────────────┤
│                   Application Layer                           │
│                                                               │
│  AuthService        │ DeviceService      │ CctvControlService │
│  (login/logout/JWT) │ (/devices,         │ (줌/포커스         │
│                     │  /channels)        │  서버 API)         │
│                     │                    │                     │
│  EventService       │ PlaybackService    │ UgvService          │
│  (REST 히스토리 +   │ (/timeline,        │ (WS 명령/ack/      │
│   WS 실시간)        │  /stream → HLS)    │  상태/GPS/RSSI)    │
├───────────────────────────────────────────────────────────────┤
│                     Infra Layer                               │
│                                                               │
│  RestClient              │ WsClient              │ TokenStore │
│  (QNetworkAccessManager  │ (QWebSocket            │ (메모리    │
│   Bearer 자동 주입       │  재연결/heartbeat       │  only)    │
│   401 공통 처리)         │  requestId 매핑)        │           │
├───────────────────────────────────────────────────────────────┤
│                     Media Layer                               │
│                                                               │
│  StreamPlayerV2           │ VideoRenderWidget                  │
│  (appsink → QImage)       │ (QOpenGLWidget + OSD 합성)        │
│                           │                                    │
│  ChannelSessionManager    │ ClipCaptureManager                 │
│  (채널-플레이어 세션)      │ (appsink 프레임 + 비동기 인코딩)  │
├───────────────────────────────────────────────────────────────┤
│                     State Layer                               │
│                                                               │
│  AppState (싱글턴) — 화면 전환, UI 상태, 연결 flag             │
│  QSettings("TeamClue", "VMS_v2") — 로컬 설정 영속화           │
└───────────────────────────────────────────────────────────────┘
```

---

## 3. 전체 시스템 아키텍처

```
┌──────────────────────────────────────────────────────────────┐
│  VMS Client (Qt C++ / Windows)                                │
│                                                                │
│  ┌─────────┐   ┌───────────┐   ┌────────────┐                │
│  │ REST    │   │ WebSocket │   │ GStreamer   │                │
│  │ Client  │   │ Client    │   │ Pipeline   │                │
│  │ (HTTPS) │   │ (WSS)     │   │ (RTSP)     │                │
│  └────┬────┘   └─────┬─────┘   └─────┬──────┘                │
└───────┼──────────────┼───────────────┼────────────────────────┘
        │              │               │
        ▼              ▼               ▼
┌──────────────────────────────────────────────────────────────┐
│  서버 (Raspberry Pi)                                          │
│                                                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────┐ │
│  │ REST API │  │ WS Server│  │mediaMTX  │  │ PostgreSQL   │ │
│  │ (JWT)    │  │ (이벤트/ │  │(RTSP     │  │ (장치/채널/  │ │
│  │          │  │  UGV)    │  │ Relay +  │  │  이벤트)     │ │
│  │          │  │          │  │ 녹화)    │  │              │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────────┘ │
│                                                                │
│  ┌──────────────┐                                              │
│  │ config 파일  │ ← 카메라 ID/PW (DB 아님)                     │
│  └──────────────┘                                              │
└──────────────────────────────┬───────────────────────────────┘
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
         ┌────────┐     ┌──────────┐     ┌──────────┐
         │ CCTV   │     │ 중계기   │     │  UGV     │
         │카메라  │     │(Gateway) │────▶│ 로봇     │
         └────────┘     └──────────┘     └──────────┘
```

---

## 4. Infra Layer 상세

### 4.1 RestClient

```
┌─────────────────────────────────────────────────┐
│  RestClient (싱글턴)                             │
├─────────────────────────────────────────────────┤
│  m_nam : QNetworkAccessManager*                  │
│  m_tokenStore : TokenStore*                      │
├─────────────────────────────────────────────────┤
│  get(endpoint, callback)                         │
│  post(endpoint, body, callback)                  │
│  put(endpoint, body, callback)                   │
│  delete_(endpoint, callback)                     │
├─────────────────────────────────────────────────┤
│  내부 동작:                                       │
│  ① 모든 요청에 Authorization: Bearer 자동 첨부    │
│  ② 401 → AuthService::handleUnauthorized() 호출  │
│  ③ 응답은 {ok, data, error} envelope 파싱 후     │
│     data 부분만 callback에 전달                   │
└─────────────────────────────────────────────────┘
```

**REST 응답 envelope 표준** (CSV 기준):
```json
{
    "ok": true,
    "data": { ... },
    "error": null
}
// 실패 시
{
    "ok": false,
    "data": null,
    "error": { "code": "ERROR_CODE" }
}
```

> ⚠️ CSV 원본에 `assessToken` 오탈자 있음 → 서버팀에 `accessToken` 확인 필요

### 4.2 WsClient

```
┌─────────────────────────────────────────────────┐
│  WsClient (싱글턴)                               │
├─────────────────────────────────────────────────┤
│  m_socket : QWebSocket*                          │
│  m_status : Disconnected|Connecting|Connected    │
│  m_reconnectCount : int                          │
│  m_reconnectTimer : QTimer* (exponential backoff)│
│  m_pendingAcks : QMap<msgId, callback>           │
├─────────────────────────────────────────────────┤
│  connectToServer(url, token)                     │
│  disconnect()                                    │
│  sendMessage(type, payload) → msgId              │
│  subscribe(filters, callback)                    │
│  unsubscribe(subscriptionId)                     │
├─────────────────────────────────────────────────┤
│  시그널:                                          │
│    connected()                                    │
│    disconnected()                                 │
│    messageReceived(type, QJsonObject)             │
│    connectionStatusChanged(status)                │
└─────────────────────────────────────────────────┘
```

**WS 연결 시점**: MainScreen 진입 시 (DeviceCheck 이후)
- 이유: 장치/권한 컨텍스트가 있어야 이벤트 구독 가능
- WS 연결 전 화면(Login/Signup/DeviceCheck)은 REST만 사용

**WS 재연결 정책**:
```
연결 끊김 → 1초 대기 → 재연결 시도
         → 실패 → 2초 대기 → 재연결
         → 실패 → 4초 대기 → ... (exponential backoff, max 30초)
         → 성공 → resumeFrom.lastEventId로 누락 이벤트 복구
                  (미지원 시 REST로 최근 N건 조회 fallback)
```

**연결 상태 UI 표시** (Topbar, ARCHIVE 상태 옆):
- 연결 끊김 즉시 → `⚠ 서버 연결 끊김 (재연결 중...)`
- 10회 연속 실패 → `⚠ 서버 연결 실패 [재연결]` (수동 재연결 버튼 노출)
- 재연결 성공 → 표시 제거


### 4.3 TokenStore

```
┌─────────────────────────────────────────────────┐
│  TokenStore (AuthService 내부)                   │
├─────────────────────────────────────────────────┤
│  m_accessToken : QString   (메모리 only)         │
│  m_expiresAt : QDateTime                         │
│  m_userId : QString                              │
├─────────────────────────────────────────────────┤
│  store(token, expiresIn)                         │
│  clear()                                         │
│  accessToken() → QString                         │
│  isValid() → bool (존재 + (만료 미설정 또는 만료 전)) │
│  userId() → QString                              │
└─────────────────────────────────────────────────┘
```

> ⚠️ **토큰 정책 결정**: refresh 메커니즘을 사용하지 않고, **로그아웃 또는 앱 종료 시 토큰을 폐기**하는 방식으로 서버에 전달 완료. (401 발생 시 재로그인 처리)

**향후 보안 강화 시 참고 (refresh 도입 시 수정 범위):**
- `AuthService`: `refresh()` 호출 흐름 추가, refresh 실패 시 로그아웃 처리
- `RestClient`(또는 공통 요청 인터셉터): `401` 발생 시 1회 refresh 후 원요청 재시도
- `RestClient`: 동시 요청 refresh race(중복 refresh) 방지 (단일 in-flight refresh 보장)
- `TokenStore`: `refreshToken`, 만료시각, 갱신 함수 추가

---

## 5. Application Layer 상세

### 5.1 AuthService

| 메서드 | REST | 설명 |
|--------|------|------|
| `login(id, pw)` | `POST /auth/login` | 토큰 저장 → `loginSuccess` 시그널 |
| `logout()` | `POST /auth/logout` | 토큰 폐기 → `loggedOut` 시그널 |
| `signup(name, id, pw)` | `POST /auth/signup` | 가입 완료 → Login 전환 |
| `handleUnauthorized()` | — | 토큰 폐기 → `tokenExpired` 시그널 |

시그널: `loginSuccess(userId)`, `loginFailed(errorMsg)`, `tokenExpired()`, `loggedOut()`

`handleUnauthorized()` 동작:
1. `TokenStore.clear()`로 현재 토큰 폐기
2. `AppState.isAuthenticated = false` 반영
3. `tokenExpired()` 시그널 발행
4. `MainWindow`에서 Login 화면으로 강제 전환

목적:
- 401 응답 처리 로직을 화면별로 중복 구현하지 않고 한 곳에서 공통 처리
- 세션 만료/무효 토큰 시나리오를 동일한 UX로 통일

### 5.2 DeviceService

| 메서드 | REST | 설명 |
|--------|------|------|
| `fetchDevices()` | `GET /devices` | 장치 목록 (type, model, health) |
| `fetchChannels(deviceId)` | `GET /device/{id}/channels` | 채널 목록 |
| `fetchChannelDetail(chId)` | `GET /channel/{chId}` | RTSP URL 포함 상세 |

> `channels.rtsp_url`은 **mediaMTX 경유 주소**. VMS는 그대로 GStreamer에 전달.

### 5.3 CctvControlService

| 메서드 | REST | 설명 |
|--------|------|------|
| `zoom(channelId, value)` | `POST /channel/{channelId}/zoom` | 값: `-100,-10,-1,1,10,100` |
| `focus(channelId, value)` | `POST /channel/{channelId}/focus` | 값: `-100,-10,-1,1,10,100` |

- VMS는 **채널명 + 제어값만 전송**, 카메라 IP/ID/PW 모름
- 서버가 config + DB 조합 → CGI URL 구성 → 호출 → 결과 리턴
- 실패 시 에러 응답 → UI 에러 표시
- `focus/reset` 엔드포인트는 v2 초기 스코프에서 보류 (후속 확장 후보)

### 5.4 EventService

**이중 경로 (REST + WS)**:

| 경로 | 용도 | API |
|------|------|-----|
| REST | 히스토리 조회/검색 | `GET /events`, `GET /event/{id}` |
| WS | 실시간 수신 | `subscribe` → `event` push |

**이벤트 구독 (WS)**:
```json
{
    "type": "subscribe",
    "msgId": "sub-001",
    "filters": {
        "deviceIds": [11, 12],
        "eventTypes": ["MOTION"]
    },
    "resumeFrom": { "lastEventId": 12345 }
}
```

**이벤트 수신 (WS)**:
```json
{
    "type": "event",
    "subscriptionId": "s-9f3c",
    "eventId": 103,
    "deviceId": 11,
    "channelId": 101,
    "eventType": "MOTION",
    "eventTime": "...",
    "cursor": 12890
}
```

### 5.5 UgvService

**WS 메시지 흐름 (⚠️ 가안 — 서버팀 합의 후 확정, `VMS_v2_pending_decisions.md` A-2 참조)**:

```
VMS Client                    서버                    중계기/UGV
    │                           │                        │
    │─── request.conn.ugv ────▶│                        │
    │◀── request.conn.ugv.ack──│                        │
    │                           │── request.conn.ugv ──▶│
    │                           │◀─ request.conn.ugv.ack│
    │                           │                        │
    │─── request.cmd.drive ───▶│         (가안)          │
    │    (VMS 래퍼 메시지)       │── cmd.drive ─────────▶│
    │                           │                        │
    │                           │◀─ telemetry.gps ──────│
    │◀── telemetry.gps ────────│  (서버가 중계)          │
    │                           │                        │
    │─── request.cmd.ptz ─────▶│         (가안)          │
    │    (VMS 래퍼 메시지)       │── cmd.ptz ───────────▶│
```

> **핵심 수정**: CSV 원본에서 `cmd.ptz`/`cmd.drive` 송신 주체는 **VMS-Server**.
> VMS Client는 `request.cmd.*` 형태의 래퍼 메시지로 서버에 요청하고,
> 서버가 `cmd.*`로 변환하여 중계기에 전달.
> ⚠️ `request.cmd.*` 타입명은 **가안**. 서버팀 합의 후 확정 (`pending_decisions.md` A-2).

### 5.6 PlaybackService

**3단계 흐름**:

```
① 날짜별 채널 조회
   GET /playback/dates/{date}/channels (JWT)
   → [{channelId, name}]

② 타임라인 조회
   GET /playback/timeline?channelId=100&date=2026-03-04 (JWT)
   → {availableRanges, gaps, eventMarkers}

③ 스트림 요청
   GET /playback/stream?channelId=100&ts=... (JWT)
   → {protocol:"HLS", uri:"/api/v1/playback/hls/req_abc.m3u8"}
   → 서버가 mediaMTX 세그먼트를 HLS로 변환 제공
```

---

## 6. Media Layer 상세

### 6.1 StreamPlayerV2 (전면 재작성)

**파이프라인 변경**:
```
v1: rtspsrc → rtph264depay → avdec_h264 → d3d11videosink (HWND)
v2: rtspsrc → rtph264depay → avdec_h264 → videoconvert → appsink
    (caps: video/x-raw,format=RGB)
```

| 항목 | v1 | v2 |
|------|----|----|
| 렌더 경로 | HWND 직접 | appsink → QImage → QOpenGLWidget |
| 화면 전환 | 파이프라인 재시작 (1~2초) | **위젯만 교체, 재연결 0** |
| 다중 셀 | 마지막 셀만 렌더링 | 동일 프레임 여러 위젯에 paint |
| 캡처 | BitBlt (창 가려지면 실패) | appsink 프레임 직접 추출 |
| OSD | 네이티브 창 위 제한적 | Qt 위젯 레이어링 (완전 투명) |

### 6.2 VideoRenderWidget (신규)

```cpp
class VideoRenderWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    void updateFrame(const QImage& frame);
    QImage lastFrame() const;  // 캡처용
protected:
    void paintEvent(QPaintEvent*) override;
    // QPainter로 QImage 렌더링 + OSD 오버레이 합성
private:
    QImage m_currentFrame;
    // OSD: 타임스탬프, 채널명, 연결상태, CLIP 타이머
};
```

### 6.3 ClipCaptureManager (수정)

| 항목 | v1 | v2 |
|------|----|----|
| 캡처 소스 | BitBlt 화면 캡처 | appsink 프레임 직접 추출 |
| 인코딩 | 동기 ffmpeg (UI 블로킹) | **비동기** `QtConcurrent::run()` |
| 완료 처리 | 동기 대기 | 완료 시그널 → UI 응답성 보장 |

---

## 7. State Layer 상세

### 7.1 AppState 필드 (v1 확장)

| 필드 | 타입 | v1? | 설명 |
|------|------|-----|------|
| `currentScreen` | int (enum) | ✅ | 현재 스택 화면 ID |
| `selectedDevices` | QStringList | ✅ | 장치확인 선택 장치 |
| `cellChannels[9]` | QString[9] | ✅ | 멀티뷰 셀 채널 매핑 |
| `activeChannel` | QString | ✅ | 전체화면 기준 채널 |
| `clipOn` | bool | ✅ | 클립 녹화 진행 여부 |
| `playbackAutoStartRequested` | bool | ✅ | Playback 자동재생 |
| `playbackTargetChannel` | QString | ✅ | Playback 대상 채널 |
| `currentLayout` | int (enum) | ✅ | 4/6/9 분할 모드 |
| `isAuthenticated` | bool | 🆕 | 인증 완료 여부 |
| `wsConnected` | bool | 🆕 | WS 연결 flag |

### 7.2 QSettings 키 (v2)

| 키 | 용도 | v1 유지? |
|----|------|----------|
| `paths/snapshotDir` | 스냅샷 저장 경로 | 🆕 |
| `paths/clipDir` | 비디오 클립 저장 경로 | 🆕 |
| `layout/defaultGrid` | 기본 분할 모드 (4/6/9) | ✅ |
| `policy/*` | 로컬 보관 정책 | ✅ |
| `devices/*` | ~~장치 목록~~ | ❌ 서버 DB로 이동 |

기본 경로 정책 (Windows 표준 경로):
- 스냅샷: `QStandardPaths::PicturesLocation` + `/snapshot`
- 클립: `QStandardPaths::MoviesLocation` + `/videoclip`
- 폴더가 없으면 시작 시 자동 생성
- v1의 `paths/saveDir` 단일 경로는 v2에서 `snapshotDir`/`clipDir`로 분리 마이그레이션

---

## 8. 화면별 v2 변경 요약

| # | 화면 | 창 크기 | v2 핵심 변경 |
|---|------|---------|-------------|
| 0 | LoginScreen | 360×320 | `AuthService.login()` → JWT 발급 |
| 1 | SignupScreen | 360×420 | `AuthService.signup()` → REST |
| 2 | DeviceCheckScreen | 500×360 | `DeviceService.fetchDevices()` + 트리 UI(`type→model→name`) |
| 3 | MainScreen | 16:9 min 1600×900 | appsink 렌더 + WS 이벤트 + 이벤트뷰 토글 |
| 4 | CctvScreen | 16:9 min 1600×900 | `CctvControlService` 줌/포커스 (이벤트뷰 없음) |
| 5 | UgvScreen | 16:9 min 1600×900 | `UgvService` WS 제어/상태/GPS (스낵바 이벤트 폐기) |
| 6 | PlaybackScreen | 16:9 min 1600×900 | `PlaybackService` 타임라인+HLS (스낵바 이벤트 폐기) |

공통 이벤트 UX 정책:
- 모든 Runtime 화면 Topbar에 `알림센터(벨)` 고정 배치
- 신규 이벤트 발생 시 벨 아이콘에 빨간 점(미확인 배지) 표시
- 이벤트 검색 버튼은 Topbar 공통 버튼으로 이동 (기존 검색 모달 재사용)
- 기존 스낵바 이벤트 알림은 전 화면에서 폐기

MainScreen 전용 정책:
- 기본 레이아웃: `채널/Playback 트리 | 스트리밍 | 이벤트뷰`
- 이벤트뷰 토글 OFF: `채널/Playback 트리 | 스트리밍` (트리 폭 유지, 스트리밍 영역 확장)
- 이벤트뷰 토글 버튼은 MainScreen 내부(전용 컨트롤)만 제공

DeviceCheckScreen 표시 규칙:
- 표시 필드: `type`, `model`, `name`, `online/health`
- 비표시 필드: `ip` (보안 정책상 메인 UI 노출 금지)
- 트리 구조: `type -> model -> name`
- 전체화면 진입 분기:
  - `type == CCTV` -> `CctvScreen` (줌/포커스 UI)
  - `type == UGV` -> `UgvScreen` (드라이브/맵 UI)

### 화면 전환 흐름

```
앱 실행 → Login
  ├─ 회원가입 → Signup → Login
  └─ JWT 성공 → DeviceCheck → VMS 시작 → MainScreen
                                            ├─ CCTV 더블클릭 → CctvScreen
                                            ├─ UGV 더블클릭 → UgvScreen
                                            ├─ 이전영상 탭 → PlaybackScreen
                                            └─ 401 Unauthorized → Login (강제)
```

---

## 9. 핵심 시퀀스 다이어그램

### 9.1 로그인 → 관제 시작

```
사용자 → LoginScreen → AuthService → RestClient → 서버
                                                    │
                        ◀── {ok, data:{accessToken}} ─┘
                        TokenStore에 저장
                        loginSuccess 시그널
         MainWindow ← 화면 전환 → DeviceCheckScreen
                        DeviceService.fetchDevices()
                        RestClient → GET /devices (Bearer)
                        ◀── 장치 목록
         사용자: 체크박스 선택 + VMS 시작
         MainWindow → MainScreen (+ WS 연결 시작)
                        WsClient.connectToServer()
                        EventService.subscribe()
```

### 9.2 실시간 영상 + 이벤트

```
GStreamer Pipeline
  │ appsink new-sample
  ▼
StreamPlayerV2 → QImage 변환
  │
  ▼
VideoRenderWidget.updateFrame() → paintEvent() 렌더링

(동시에)
WsClient ← 서버 WS
  │ event 메시지
  ▼
EventService → NotificationCenter 저장/갱신 (전 화면 공통)
            → MainScreen에서 이벤트뷰 표시 중이면 EventView 동시 업데이트
            → Topbar 벨 아이콘 빨간 점(미확인) 갱신
```

### 9.3 UGV 제어 (⚠️ `request.cmd.*` 타입은 가안 — §5.5 참조)

```
사용자 → UgvScreen D-패드 조작
  → UgvService.sendDriveCommand({forw,back,...})
  → WsClient.sendMessage("request.cmd.drive", payload)  ← 가안
  → 서버 WS
  → 서버가 cmd.drive로 변환 → 중계기 → UGV

(역방향)
중계기 → telemetry.gps → 서버 WS → WsClient
  → UgvService → UgvScreen 지도/OSD 업데이트
```

### 9.4 Playback

```
사용자: 날짜+채널 선택
  → PlaybackService.fetchTimeline(channelId, date)
  → RestClient → GET /playback/timeline (Bearer)
  ← {availableRanges, gaps, eventMarkers}
  → 타임라인 UI 표시

사용자: 시간대 클릭
  → PlaybackService.requestStream(channelId, ts)
  → RestClient → GET /playback/stream (Bearer)
  ← {protocol:"HLS", uri:"...m3u8"}
  → StreamPlayerV2에 HLS URL 전달 → 재생
```

---

## 10. 프로젝트 파일 구조

```
VMS_v2_dev/
├── CMakeLists.txt                        [MODIFY]
├── main.cpp                              [KEEP]
│
├── # ── State ──
├── app_state.h/.cpp                      [MODIFY]  필드 확장
│
├── # ── Infra ──
├── rest_client.h/.cpp                    [NEW]     REST 공통 래퍼
├── ws_client.h/.cpp                      [NEW]     WebSocket 클라이언트
│
├── # ── Application ──
├── auth_service.h/.cpp                   [NEW]     JWT + TokenStore
├── device_service.h/.cpp                 [NEW]     장치/채널 조회
├── cctv_control_service.h/.cpp           [NEW]     줌/포커스
├── event_service.h/.cpp                  [NEW]     이벤트 REST+WS
├── ugv_service.h/.cpp                    [NEW]     UGV WS 제어
├── playback_service.h/.cpp               [NEW]     Playback API
│
├── # ── UI (화면) ──
├── mainwindow.h/.cpp                     [MODIFY]  401 강제 로그인
├── screens.h                             [MODIFY]
├── login_screen.cpp                      [MODIFY]  AuthService 연동
├── signup_screen.cpp                     [MODIFY]  AuthService 연동
├── device_check_screen.cpp               [MODIFY]  DeviceService 연동
├── main_screen.cpp                       [MODIFY]  appsink + EventService
├── cctv_screen.cpp                       [MODIFY]  CctvControlService
├── ugv_screen.cpp                        [MODIFY]  UgvService
├── playback_screen.cpp                   [MODIFY]  PlaybackService + HLS
│
├── # ── Media (렌더링 핵심) ──
├── stream_player.h/.cpp                  [REWRITE] appsink + QImage
├── video_render_widget.h/.cpp            [NEW]     QOpenGLWidget
├── channel_session_manager.h/.cpp        [MODIFY]  렌더 백엔드 교체
│
├── # ── 캡처/클립 ──
├── native_capture.h/.cpp                 [DELETE]  BitBlt 제거
├── clip_capture_manager.h/.cpp           [MODIFY]  appsink + 비동기
│
├── # ── 공통 UI ──
├── common_widgets.h/.cpp                 [MODIFY]
├── common_ui.h/.cpp                      [MODIFY]
├── popup_manager.h/.cpp                  [MODIFY]
│
├── # ── 더미 ──
├── dummy_data.h/.cpp                     [CONDITIONAL]
│
├── # ── 리소스 ──
├── styles/
│   ├── v1_theme.qss                      [KEEP]
│   └── v2_theme.qss                      [NEW]
├── resources.qrc                         [MODIFY]
└── docs/
```

| 분류 | 수 | 목록 |
|------|---|------|
| **NEW** | 10 | `rest_client`, `ws_client`, `auth_service`, `device_service`, `cctv_control_service`, `event_service`, `ugv_service`, `playback_service`, `video_render_widget`, `v2_theme.qss` |
| **REWRITE** | 1 | `stream_player` |
| **DELETE** | 1 | `native_capture` |
| **MODIFY** | 13 | CMake, app_state, mainwindow, screens, 6개 screen, channel_session_manager, clip_capture_manager, common_*, popup_manager, resources.qrc |
| **KEEP** | 2 | main.cpp, v1_theme.qss |

---

## 11. 구현 순서

| 순서 | 작업 | 파일 | 선행 | 서버 의존 |
|------|------|------|------|----------|
| **1** | **StreamPlayerV2 + VideoRenderWidget** | stream_player, video_render_widget, channel_session_manager | 없음 | ❌ |
| **2** | RestClient + AuthService + TokenStore | rest_client, auth_service | 없음 | ❌ (로그인 API만) |
| **3** | 로그인→장치조회 화면 연동 | login_screen, signup_screen, device_check_screen, device_service | #2 | ❌ |
| **4** | WsClient 뼈대 (연결/재연결/ping) | ws_client | #2 (토큰) | ⚠️ UGV 래퍼 타입(A-2), 이벤트 상세(A-8) |
| **5** | CCTV 제어 연동 | cctv_control_service, cctv_screen | #2 | ❌ (엔드포인트 확정 완료) |
| **6** | 이벤트 WS + REST 통합 | event_service, main_screen | #4 | ⚠️ WS 스키마 |
| **7** | Playback 타임라인 + HLS | playback_service, playback_screen | #1 | ⚠️ Playback API 완성 |
| **8** | UGV 제어 WS | ugv_service, ugv_screen | #4 | ⚠️ WS 래퍼 타입 합의 |
| **9** | ClipCapture 비동기 + native_capture 삭제 | clip_capture_manager, native_capture | #1 | ❌ |
| **10** | 테마 + 마감 | v2_theme.qss, CMakeLists, docs | 전체 | ❌ |

> **#1~#3은 서버 합의 없이 독립 착수 가능** — 즉시 시작 가능
> **#1과 #2+#3은 병렬 진행 가능** — 렌더링과 인증/장치를 동시에 작업 가능

---

## 12. 빌드 의존성 (v2 추가)

| 모듈 | CMake | 용도 |
|------|-------|------|
| `Qt::Network` | `find_package(Qt6 REQUIRED COMPONENTS Network)` | REST API |
| `Qt::WebSockets` | `find_package(Qt6 REQUIRED COMPONENTS WebSockets)` | WS 통신 |
| `Qt::OpenGLWidgets` | `find_package(Qt6 REQUIRED COMPONENTS OpenGLWidgets)` | 영상 렌더링 |
| OpenSSL | 별도 설치 | TLS (wss://, https://) |
| GStreamer | 기존 유지 | 영상 파이프라인 |
| FFmpeg | 기존 유지 | 클립/Export 인코딩 |

> `Qt::Sql` 불필요, Qt 모듈 정책: 소켓/스레드/HTTP 전부 Qt 내장 사용

---

## 13. v2 백로그 분류 (적용 / 조건부 / 반려)

> 출처: `VMS_v2_roadmap.md`
> 아래는 v2 범위를 일정/리스크 기준으로 재분류한 실행 기준이다.

### 13.1 적용 (확정)

| 항목 | 기준 | 적용 시점 |
|------|------|-----------|
| 13.1 스트림 상태 관측성 강화 | 장애 원인 추적 시간 단축에 필수 | Phase 1 완료 직후 |
| 13.2 캡처 OSD 옵션화 | appsink 구조에서 구현 난이도 낮음 | Phase 1 이후 |
| 13.6 종료 생명주기 강화 | 종료 꼬임/잔여 작업 방지 필수 | Phase 3 이후 |
| 13.8 ONVIF Discovery (서버 수행) | 클라이언트 직접 탐색이 아닌 서버 실행으로 일원화 | P2 |
| 13.11 테스트 자동화 (최소 스모크) | 최소 회귀 경로 품질 게이트 확보 | 안정화 단계 |
| 13.12 런타임/재시작 반영 매트릭스 | 설정 반영 정책 명확화 | 설정 화면 정리 시 |
| 13.13 FFmpeg 배포 정책 | **번들 배포 확정** (사용자 수동 설치 의존 제거) | 릴리즈 준비 단계 |

### 13.2 조건부 적용 (문제 발생 시)

| 항목 | 트리거 | 비용/리스크 |
|------|--------|-------------|
| 13.3 Playback seek UX 안정화 | 탐색 튕김/불일치 재발 | 중간 |
| 13.4 Playback 마커 렌더링 최적화 | 대량 이벤트 시 UI 저하 발생 | 중간 |
| 13.5 비동기 미디어 공통 러너 | 개별 비동기 유지보수 비용 증가 시 | 중간 |
| 13.7 QThread 분리 검토 | 10ch+ 또는 입력 지연(>500ms) 재현 시 | 높음 |
| 13.10 장치 관리 고도화 | 운영 요구로 장치 CRUD 고도화 필요 시 | 높음 |

정책:
- v2 초기에는 개별 비동기(`ClipCaptureManager`, Playback Export)로 구현
- 운영/프로파일링 이슈가 재현될 때 13.5 공통 러너로 통합

### 13.3 반려 (v2 스코프 제외)

| 항목 | 결정 |
|------|------|
| 13.9 사용자 역할 분리 | v2 범위에서 제외. 단일 역할 운영 |

### 13.4 오너 결정 필요 항목

| 항목 | 결정 주체 | 상태 |
|------|----------|------|
| 13.11 스모크 테스트 범위(최소 시나리오 확정) | 프론트 팀 | 확정 |
| 13.12 설정별 즉시 반영/재시작 필요 표 확정 | 프론트 팀 | 확정 |
| 13.13 FFmpeg 번들 패키징 방식(zip/installer) 및 시작 시 진단 UX | 프론트+배포 담당 | 확정 |

13.11 테스트 운영 정책(확정):
- Phase 게이트 방식: 각 Phase 완료 시 최소 스모크 테스트 실행
- 통과 시 다음 Phase 진행, 실패 시 머지/배포 중지 후 디버깅
- 최소 스모크 시나리오:
  1. 로그인 -> DeviceCheck 진입
  2. DeviceCheck -> Main 전환
  3. RTSP 1채널 재생 시작/정지
  4. 스냅샷 저장 성공
  5. 클립 시작/종료 + 파일 생성
  6. Playback 진입 + 타임라인 1회 조회
- 합격 기준:
  - 크래시 없음
  - 의도하지 않은 에러 팝업 없음
  - 화면 전환/재생/파일 생성 결과가 기대값과 일치

13.12 현재 확정 사항:
- `paths/snapshotDir`, `paths/clipDir` 변경은 즉시 반영
- 수동 RTSP 장치 추가/삭제(API 기반)는 즉시 반영
- ONVIF Discovery 기능은 서버팀 협의 전까지 보류 (기능 제외 가능성 포함)
- TLS 관련 설정은 현 시점 추가 정의 없이 운영 이슈 발생 시 이슈 등록 후 수정

13.13 테스트 시점 정책:
- 개발 착수 전: ffmpeg 실패 경로 스모크 1회 (미설치/오경로 시 안내 모달 및 기능 비활성 확인)
- 릴리즈 직전: 클린 환경(E2E) 1회 (번들 포함 설치본 기준)

13.13 배포/런타임 정책(확정):
- 패키징 방식: installer
- 체크 시점: 앱 시작 직후 ffmpeg 헬스체크 1회
- 경로 우선순위: 번들 경로 -> 설정 경로 -> 실패 안내
- ffmpeg 미검출 시: 클립/Export 등 ffmpeg 의존 기능만 비활성화 (다른 기능은 정상 사용)
- 비활성 버튼 툴팁: `FFmpeg을 찾을 수 없습니다` 안내 문구 표시

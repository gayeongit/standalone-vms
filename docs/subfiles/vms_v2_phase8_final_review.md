# VMS v2 Phase 1~8 종합 코드 리뷰

> 리뷰 일시: 2026-03-13 (GPT-5.4 + Codex 피드백 반영) | 범위: 클라이언트 전체 + 서버 통합 확인 + 성능/클립/UI/UGV 정책

---

## 1. 전체 상태 요약

| 영역 | 평가 | 비고 |
|------|------|------|
| Phase 계획 충실도 | ✅ | 8개 Phase 구현 완료, 일부 E2E 검증은 Blocked |
| 서비스 계층 분리 | ✅ | Auth/Device/Event/CctvControl/Playback/Ugv 6개 서비스 |
| 서버 통신 정합성 | 🟡 | REST 계약은 맞지만 UGV ACK 계약 + WS send 레이스 이슈 있음 |
| 리팩토링 필요도 | 🟡 | [mainwindow.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp) 재비대화, [screens.h](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h) 거대 헤더, [AppState](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h#21-50) 비대 |
| 앱 무거움 | 🔴 | CPU decode + CPU colorspace convert + QImage 복사가 진짜 병목 |
| 클립 속도 | 🔴 | PNG 직렬 저장 + ffmpeg fork 병목 |
| UI/UX | 🟡 | 기능은 동작하나 디자인 전면 개선 필요 |

---

## 2. 파일 크기 현황 (Phase 8 완료)

| 파일 | 크기 | Phase 3.5 대비 | 판단 |
|------|------|---------------|------|
| [playback_screen.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp) | **46KB** | 신규 | 🔴 가장 큼, 분리 필요 |
| [ugv_screen.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_screen.cpp) | **38KB** | 증가 | 🟡 3개 보조파일로 분리(+7KB) — 합계 45KB |
| [mainwindow.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp) | **37KB** | 28→37KB +32% | 🔴 재비대화 |
| [main_screen.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/main_screen.cpp) | **34KB** | 유지 | 🟡 도메인 복잡도 감안 적절 |
| [common_ui.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp) | **24KB** | 20→24KB | 🟡 소폭 증가 |
| [ugv_service.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp) | 18KB | 신규 | ✅ 적절 |
| [playback_service.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_service.cpp) | 17KB | 신규 | ✅ 적절 |
| [event_service.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/event_service.cpp) | 15KB | 10→15KB | ✅ 기능 보강 수준 |
| [screens.h](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h) | **11KB / 350줄** | 증가 | 🔴 거대 헤더, 분리 검토 |

---

## 3. 서버 코드 통합 확인

### 결론: 🟡 Playback REST는 잘 맞지만, UGV WS는 계약/종료 정책 보완이 필요합니다

| API | 서버 | 클라이언트 | 정합 |
|-----|------|-----------|------|
| `GET /playback/dates/{date}/channels` | [VmsPlaybackController](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/VmsPlaybackController.cpp#133-246) - auth + 날짜 파싱 + `data[]` | `PlaybackService::fetchAvailableChannels` | ✅ |
| `GET /playback/timeline` | channelId+date 쿼리 → merged ranges+gaps+markers | `PlaybackService::fetchTimeline` | ✅ |
| `GET /playback/stream` | ts→lookup→mediaMTX URL 생성 + public URI rewrite | `PlaybackService::requestStream` | ✅ |
| `POST /playback/export` | job 생성 → mediaMTX URL 직접 생성 → DONE/FAILED | `PlaybackService::requestExport` | ✅ |
| `GET /playback/export/{jobId}` | mutex-guarded job 조회 → uri/fileName/message | `PlaybackService::fetchExportStatus` | ✅ |
| `WS /gw/ws` | gateway WS → `UgvController`/`UgvService` | `UgvService` (클라이언트) | 🟡 |

**서버 코드 주의점**: export job은 메모리 `unordered_map` 저장 — 서버 재시작 시 소멸. 개발 단계에서는 OK.

### 🔴 서버 UGV ACK 계약 왜곡 (Codex 발견)

서버 `UgvService.cpp`가 gateway 실제 처리 결과를 기다리지 않고 **즉시 ACK를 생성하여 클라이언트에 반환**하고 있음. gateway에서 들어온 `*.ack`는 클라이언트로 재전달하지 않음.

- 즉시 ACK 생성: [UgvService.cpp:481](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvService.cpp#L481), [UgvService.cpp:495](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvService.cpp#L495)
- gateway ACK 미전달: [UgvService.cpp:515](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvService.cpp#L515), [UgvService.cpp:527](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvService.cpp#L527)

**영향**: 클라이언트가 `msgId` 기준 pending 매칭 + `gatewayId/ugvId` 검증을 꼼꼼하게 만들었지만, 서버 쪽에서 "ACK = 실제 장비 처리 성공" 의미가 무력화됨

**해결 옵션**:
- A: 서버가 gateway ACK 수신 후에만 클라이언트에 전달 (진짜 ACK)
- B: 서버 즉시 ACK(수신 확인) + gateway ACK를 별도 메시지로 전달 (2단계 ACK)
- C: 현재 구조 유지 + 클라이언트가 ACK를 "수신 확인"으로만 해석 (의미 축소)

→ **success-path E2E 검증 전에 서버 담당자와 ACK 계약을 확정해야 함**

### 🔴 서버 WS send 동시 호출 레이스 (Codex 발견)

같은 소켓에 push thread와 recv thread가 둘 다 `sendText()`를 호출. 라이브러리 thread-safety가 보장되지 않으면 WS 프레임이 섞일 수 있음.

- 관련 위치: [UgvController.cpp:92](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvController.cpp#L92), [UgvController.cpp:120](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvController.cpp#L120), [UgvController.cpp:202](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/server/vmsapi/UgvController.cpp#L202)

**해결**: `sendText()` 호출부에 `std::mutex` 래핑 필요

### 🟡 로그아웃/401/종료 시 UGV disconnect handshake 우회 가능 (Codex 발견)

현재 `401`/로그아웃/앱 종료 경로는 `UgvScreen::prepareForShutdown()` 이후 곧바로 `UgvService::shutdown()`으로 소켓을 닫는 구조다.

- `401`: [mainwindow.cpp:727](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L727), [mainwindow.cpp:736](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L736)
- 로그아웃: [mainwindow.cpp:774](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L774)
- 종료: [mainwindow.cpp:1044](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L1044)
- 소켓 종료: [ugv_service.cpp:199](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp#L199)

`prepareForShutdown()`이 hide 시 disconnect를 건너뛰도록 플래그를 세우므로, 현재 구조에서는 `request.disconn.ugv` ACK 왕복 없이 연결이 끊길 수 있다.

**영향**: gateway 측이 명시적 disconnect를 세션 정리 조건으로 본다면, 운영 종료/401 경로에서 dangling session이 남을 수 있음.

**권장**:
- 운영 종료 경로를 `request.disconn.ugv` → ACK 대기 → socket close 순서로 정리
- 강제 종료가 필요한 경우에만 fallback으로 `shutdown()` 사용

---

## 4. 리팩토링 분석 — 결합도/응집도

### 🔴 최우선: [mainwindow.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp) 재비대화 (28KB → 37KB)

Phase 3.5에서 28KB로 줄였는데 Phase 5~8에서 다시 37KB로 증가했습니다. 원인:

- Phase 5: `CctvControlService` 생성/주입
- Phase 6: `PlaybackService` 생성/주입 + `channelId` helper들 + playback 진입 오케스트레이션
- Phase 8: `UgvService` 생성/주입 + UGV direct path 계산 + 맵 범위 config

**문제**: [MainWindow](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#284-297)가 7개 서비스 + 8개 화면의 생성/주입/연결/해제를 모두 담당

**권장 분리**:

```
mainwindow.cpp (현재 37KB)
├── service_orchestrator.cpp  -- 서비스 생성/설정/연결 (~8KB)
├── screen_router.cpp         -- showScreen/geometry/suspend (~5KB)
└── mainwindow.cpp            -- 나머지 (~24KB)
```

### 🔴 [screens.h](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h) 거대 헤더 (11KB / 350줄)

5개 화면 클래스가 모두 한 헤더에 있음. 한 화면의 멤버가 바뀌면 전체 재컴파일.

**권장**: 최소한 가장 큰 [PlaybackScreen](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h#270-348)과 [UgvScreen](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/screens.h#181-269)은 별도 헤더로 분리

### 🟡 [AppState](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/app_state.h#21-50) 비대 + 병렬 배열

```cpp
std::array<QString, 9> cellChannels{};      // 이름
std::array<int, 9> cellChannelIds{...};     // channelId
std::array<int, 9> cellDeviceIds{...};      // deviceId
```

3개 병렬 배열은 동기화 버그의 온상. `struct CellInfo { QString name; int channelId; int deviceId; }` 하나로 묶으면 안전.

### 🟡 [playback_screen.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp) 46KB

클립/외부 파이프라인 의존, export polling, 타임라인 렌더링이 모두 한 파일.

**권장**: `playback_export.cpp` (export polling+download)를 UGV처럼 별도 파일로 분리

### 🟡 [common_ui.cpp](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp) — 역할 과다

현재 채널/컨텍스트 helper, 캡처/저장 경로 helper, DnD helper가 한 파일에 섞여 있음. 최소 3개로 분리 권장:
- 채널/컨텍스트 helper
- 캡처/저장 경로 helper
- DnD helper

### 🟡 이름 기반 브리지 잔여 debt

`cellChannelIds`/`cellDeviceIds`를 도입했지만 일부 초기화/복원 경로에서 `selectedChannelIdForDisplayName()`로 다시 계산. 중복 이름 시 잘못 매핑 가능.

### 🟡 DnD 메타 손실 가능 (Codex 발견)

[common_ui.cpp:331](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/common_ui.cpp#L331)에서 `hasText()`면 즉시 return하여 `channelId/deviceId` MIME 데이터를 읽지 못하는 경로 존재. Phase 8의 direct path 정책(`8-C/8-D`)과 충돌 가능.

### 🟡 stale conn/disconn ACK 수용 여지 (Codex 발견)

클라이언트 [ugv_service.cpp:433](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp#L433), [ugv_service.cpp:455](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/ugv_service.cpp#L455)에서 `request.conn.ugv.ack`/`request.disconn.ugv.ack`가 pending 없을 때도 상태 전환될 수 있는 경로 있음 (지연 ACK). pending 없으면 무시하는 가드 강화 필요.

### 🟡 테스트용 UGV 주입 코드 잔존 (Codex 발견)

`DeviceCheckScreen`이 서버 응답에 UGV가 없어도 테스트용 UGV 컨텍스트를 강제로 하나 추가하고 있다.

- 테스트 UGV 생성: [login_screen.cpp:25](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp#L25)
- 실제 주입 위치: [login_screen.cpp:425](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/login_screen.cpp#L425)

이 코드는 첫 번째 선택 컨텍스트를 복사해 `deviceId/channelId`를 재사용하므로, direct path 정책과 `findSelectedChannelContextByChannelId()` 기반 UGV 식별을 흐릴 수 있다.

**영향**:
- 서버에 실제 UGV가 없어도 UGV 화면 진입 경로가 열릴 수 있음
- CCTV/UGV 식별자가 겹쳐 direct path 검증을 왜곡할 수 있음
- "Phase 8 실서버 기준 완료" 판단을 약하게 만듦

**권장**: 테스트 주입 코드는 개발자 옵션 뒤로 숨기거나 완전히 제거

### 🟡 로그아웃 후 스트림 세션이 `PAUSED`로 잔존 (Codex 발견)

현재 로그인 복귀 시 active channel을 비우고 `applyActiveChannels()`를 다시 호출하지만, 기존 스트림 세션은 `shutdown()`되지 않고 `setPaused(true)`로만 남는다.

- 로그인 복귀: [mainwindow.cpp:732](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L732), [mainwindow.cpp:805](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L805)
- active channel 적용: [mainwindow.cpp:896](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L896)
- pause 처리: [channel_session_manager.cpp:179](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp#L179)
- 실제 shutdown은 앱 종료 시점만: [mainwindow.cpp:1084](file:///c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow.cpp#L1084)

**영향**:
- 로그아웃 후에도 디코더/파이프라인 객체가 메모리에 남을 수 있음
- "앱이 무겁다"는 체감과 로그인 재진입 시 리소스 잔존 문제에 기여할 수 있음

**권장**:
- 로그아웃/401 시 `ChannelSessionManager::shutdown()` 또는 screen 단위 hard suspend 적용
- 최소한 인증 해제 경로에서는 `PAUSED`보다 더 강한 정리 단계 필요

### ✅ 서비스 계층은 잘 분리

`RestClient` → `AuthService`/`DeviceService`/`CctvControlService`/`PlaybackService`
`WsClient` → `EventService`/`UgvService`

각 서비스가 `RestClient` 또는 `WsClient`만 의존하고, UI와 서비스 사이에 signal/callback을 사용. **이 계층은 건드릴 필요 없음.**

---

## 5. 🔴 앱이 무거운 문제 — 렌더링 최적화

### 현재 병목 구조

"OpenGL 미적용"이 문제가 아니라, **현재 파이프라인이 CPU decode + CPU colorspace convert + QImage 복사**라는 점이 진짜 병목.

```
avdec_h264 (CPU decode)           ← 가장 무거운 구간
  → videoconvert (CPU colorspace) ← 두 번째로 무거운 구간
    → appsink → QImage(BGRA) 생성
      → QSharedPointer 공유 + 간헐 .copy()
        → QPainter::drawImage() ← paint 단계 (세 번째)
          → UI 스레드 블로킹
```

`QOpenGLWidget`은 이미 쓰고 있지만, 실질적으로 무거운 구간은 paint가 아니라 **decode + convert + copy**.

### Phase 4.5의 suspend/resume는 효과가 있었나?

`showScreen()` 기준 active/paused 전환을 넣었지만, **PAUSED는 디코딩만 멈추고 파이프라인 자체는 살아있음.** 실제 체감 개선은 제한적.

### 실질적 개선 방향 (우선순위순)

#### 1. 현재 구조 내 최적화 (당장 가능)

- 멀티뷰 FPS 더 낮추기 (현재 6fps → 4fps 검토)
- 비가시 화면 suspend를 `PAUSED`가 아닌 `NULL`/`READY`로 하향 (리소스 완전 해제, resume 시 1~3초 재연결 지연 UX tradeoff)
- 불필요한 `activeChannels` 재계산 줄이기

#### 2. paint 단계 GPU 전환 (1~2일, 보조 최적화)

`VideoRenderWidget::paintGL()`에서 `QPainter::drawImage()` 대신 `glTexSubImage2D()` 텍스처 업로드:

```cpp
void VideoRenderWidget::paintGL() {
    if (!m_frame) return;
    if (!m_textureId) glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 m_frame->width(), m_frame->height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_frame->bits());
    drawTexturedQuad(); // vertex + fragment shader
}
```

- `VideoRenderWidget.cpp`만 수정, 나머지 안 건드림
- **CPU paint 비용 절감** — 다만 decode/convert가 더 무거워 체감 개선은 보조 수준

#### 3. decode/copy path GPU 전환 (검증 필요, 더 큰 영향)

진짜 큰 개선이 필요하면 decode/copy path 자체를 GPU 기반으로 바꿔야 함:

- Windows: `d3d11h264dec` 계열 HW 디코더 검토
- `appsink → QImage` 복사를 줄이고 GPU sink/texture 공유 방향
- GStreamer `glupload + glcolorconvert`로 CPU colorspace convert 제거

다만 **플랫폼 호환성 검증이 필요**해서 중기 과제.

#### 정리

| 단계 | 내용 | 효과 | 비용 |
|------|------|------|------|
| 1 | FPS 추가 축소 + suspend→NULL/READY | 🟡 중간 | 낮음 |
| 2 | `glTexSubImage2D` 텍스처 업로드 | 🟡 paint만 개선 | 1~2일 |
| 3 | `d3d11h264dec` HW 디코더 + GPU path | 🟢 근본 해결 | 검증 필요 |

---

## 6. 🔴 클립 저장 속도 개선

### 현재 방식의 문제

```
1. QTimer로 매 프레임 → QImage를 QVector에 누적 (메모리 폭발)
2. stopAndEncode()에서 전체 프레임을 PNG로 디스크 저장
3. ffmpeg 프로세스 fork → PNG 시퀀스를 읽어서 MP4 인코딩
```

**느린 이유**: PNG 압축이 프레임당 100~300ms, 30초 클립이면 수십 초 소요

### 개선 방안 비교

| 방안 | 효과 | 비용 | 판단 |
|------|------|------|------|
| **ffmpeg stdin pipe** | 🟢 큼 (10X) | 중간 | ✅ 가장 유력한 중기 개선안 |
| JPEG로 바꾸기 | 🟡 중간 | 낮음 | 빠른 체감 개선용 |
| fps/버퍼 축소 | 🟡 작음 | 낮음 | 빠른 체감 개선용 |

**클립 속도 개선의 최우선 후보: ffmpeg stdin pipe**

```
현재:  프레임 → PNG 파일 → ffmpeg -i *.png
개선:  프레임 → rawvideo pipe → ffmpeg -f rawvideo -pix_fmt bgra -s WxH -i pipe:0
```

- PNG 파일 저장을 완전 제거
- 프레임을 raw pixel로 ffmpeg `stdin`에 직접 스트리밍
- **인코딩 속도 대폭 개선 가능** (PNG 압축/해제 비용 없음)
- 디스크 I/O도 최종 MP4 하나만 발생
- 구현 범위는 생각보다 작지 않음: `ClipCaptureManager` 한 파일 중심이지만, `QProcess stdin`, cancel, backpressure, 종료 시퀀스를 같이 정리해야 함
  - `start()` 시 `QProcess::start("ffmpeg", {"-f", "rawvideo", ...})`
  - `captureFrame()`에서 `process->write(frame.bits(), frame.sizeInBytes())`
  - `stop()`에서 `process->closeWriteChannel()` → 대기

빠른 체감 개선이 급하면 `JPEG + fps/버퍼 축소`를 먼저 적용할 수 있지만, 구조적으로는 **stdin pipe가 가장 유력한 중기 개선안**이다.

---

## 7. UI/UX 개선 전략

### HTML의 역할: 런타임 UI가 아닌 디자인 레퍼런스

> ⚠️ HTML을 QWidget 앱에 런타임 UI로 직접 넣으면 일관성/입력/성능/유지보수가 나빠짐.
> HTML은 **디자인 시안/프로토타입 용도**로만 쓰고, 실제 적용은 Qt/QSS.

HTML/웹에서 추출할 것:
- spacing scale, border radius, button/slider/form/card 스타일, panel 레이아웃

Qt에서 재구성할 것:
- `QSS`, 공통 위젯, 레이아웃 규칙

### QSS 호환성 정리

| 구분 | 내용 |
|------|------|
| ✅ 가능 | margin, padding, border, border-radius, background, color, font-size, font-family |
| 🟡 제한적 | box-shadow (일부), `::groove`/`::handle` pseudo |
| ❌ 불가능 | flexbox, CSS transform, media query, transition/animation, ::before/::after |

### 권장 워크플로우

1. HTML/웹(또는 Figma)으로 시안 제작 — Qt 호환 속성만 사용
2. QSS 변환 규칙표 준비 (`class → objectName`, `CSS prop → QSS prop`)
3. 공통 디자인 토큰 정의 (spacing, radius, panel color, overlay style, button state)
4. `v2_theme.qss`에 화면별 섹션으로 적용
5. 코드에서 인라인 스타일 제거 → objectName/QSS 전환

### 레이아웃 개선은 QSS만으로는 한계

- 버튼 크기/간격/색상은 QSS로 충분
- **구조적 레이아웃 변경**(사이드바 폭, 멀티뷰 비율)은 C++ 코드 수정 필요

### 화면별 진행 순서

1. **공통 디자인 토큰** → 상태 라벨 / overlay card / panel header / sidebar section 컴포넌트화
2. **로그인/회원가입** — 가장 단순, QSS만으로 충분
3. **메인 멀티뷰** — 셀 선택 테두리/OSD 스타일
4. **CCTV/UGV** — 제어 패널 레이아웃 + 버튼 스타일
5. **Playback** — 타임라인/슬라이더 정리

---

## 8. UGV 정책 변경 — 입장 제한

### 현재 문제

> UGV 전체화면에 자유롭게 진입 가능 → 출동 없이 진입하면 의미 없는 화면

### 제안한 방식에 대한 의견

**동의합니다.** 구체적으로:

| 항목 | 현재 | 변경안 |
|------|------|--------|
| 트리에서 UGV | 클릭/드래그 가능 | **UGV 타입 제거** |
| 사이드바 UGV 리스트 | 없음 | **상태만 표시** (연결됨/미연결, read-only) |
| UGV 전체화면 진입 | 자유 | **이벤트 상세 → 출동만 허용** |
| UGV 전체화면 == 출동 화면 | 분리 상태 | **통합** |
| 디버그/관리자 진입 | 없음 | **숨김 설정으로 별도 유지** |

#### 구현 범위

```
1. DeviceCheckScreen / SidebarWidget
   - 트리에서 deviceType == "UGV" 항목 제외
   - 사이드바 하단에 UGV 상태 리스트 추가 (read-only)
   - 테스트용 UGV 주입 코드 제거 또는 개발자 옵션 뒤로 이동

2. MainScreen
   - 멀티뷰 셀에 UGV 채널 배치 차단
   - 드래그앤드롭에서 UGV 타입 거부

3. EventUiHelpers (이벤트 상세)
   - "UGV 출동" 버튼 → UGV 전체화면 전환
   - activeUgvChannelId / activeUgvGatewayId 설정
   
4. UgvScreen
   - 진입 시 반드시 channelId/gatewayId 유효성 검사
   - 없으면 진입 차단 + Main으로 복귀

5. 디버그/관리자용 진입 (선택)
   - SettingsDialog에 개발자 모드 토글, 또는 숨김 키 조합
   - 운영 UX와 개발/점검 UX 분리
```

**작업량**: 1~2일

---

## 9. 현재 상태 판단

- **V2 기능 구현은 대부분 완료** — Phase 1~8 기능은 대부분 들어갔지만, 운영 마감 관점에서는 아직 닫히지 않음
- **실서버 E2E verification pending**:
  - Playback export E2E (`7-B-4`)
  - UGV success-path E2E (`gateway /ugv/ws`)
- **클라이언트 잔여 보정 필요**:
  - 테스트용 UGV 주입 제거
  - 로그아웃/401 시 스트림 세션 hard shutdown 정책 정리
  - UGV disconnect handshake 정상화
- **서버 계약 정리 필요** (E2E 전에 확정):
  - UGV ACK 계약 (즉시 ACK vs 실제 처리 ACK)
  - WS send 레이스 방어 (`std::mutex`)

---

## 10. Post-v2 로드맵 제안

### Phase 9: 성능 안정화 스프린트 (1주)

| # | 작업 | 효과 | 비용 |
|---|------|------|------|
| 1 | **ffmpeg stdin pipe** (클립 인코딩 대폭 개선) | 🟢 체감 대폭 개선 | 1일+ |
| 2 | 멀티뷰 FPS 추가 축소 + 불필요 재계산 제거 | 🟡 즉시 가능 | 0.5일 |
| 3 | suspend를 `NULL`/`READY`로 하향 + 로그아웃 시 hard shutdown | 🟢 메모리/리소스 해제 | 0.5~1일 |
| 4 | `glTexSubImage2D` 텍스처 업로드 (paint GPU 전환) | 🟡 paint 비용 절감 | 1~2일 |
| 5 | (중기) `d3d11h264dec` HW 디코더 검토 | 🟢 근본 해결 | 검증 필요 |

### Phase 10: 구조 정리 + UGV 정책 + 서버 계약 (1주)

#### Phase 10 P0 (운영/정합성 우선)

| # | 작업 |
|---|------|
| 1 | **서버 UGV ACK 계약 확정** (즉시 ACK vs 실제 처리 ACK) |
| 2 | **서버 WS send mutex 적용** |
| 3 | DnD `hasText()` 조기 return 수정 (direct path 메타 보존) |
| 4 | `ugv_service.cpp` stale `conn/disconn ack` guard 강화 |
| 5 | 로그아웃/401 시 `ChannelSessionManager` hard shutdown 정책 정리 |
| 6 | UGV disconnect handshake 정상화 (`request.disconn.ugv` 우선, `shutdown()` fallback) |
| 7 | 테스트용 UGV 주입 제거 또는 개발자 옵션 뒤로 이동 |

#### Phase 10 P1 (구조/정책 정리)

| # | 작업 |
|---|------|
| 1 | `mainwindow.cpp` → `service_orchestrator` + `screen_router` 분리 |
| 2 | `screens.h` → 화면별 헤더 분리 |
| 3 | `AppState` 병렬 배열 → `CellInfo` struct |
| 4 | `playback_screen.cpp` → `playback_export.cpp` 분리 |
| 5 | `common_ui.cpp` → 채널/컨텍스트 + 캡처/저장 + DnD 분리 |
| 6 | UGV 진입 정책 변경 (트리 제거 + 출동 전용 + 디버그 진입 유지) |
| 7 | 프로젝트명/타이틀 v2 전환 |

### Phase 11: UI/UX 디자인 스프린트 (1~2주)

| # | 작업 |
|---|------|
| 1 | 공통 디자인 토큰 정의 (spacing, radius, color, typography) |
| 2 | 화면별 HTML/Figma 시안 제작 (Qt 호환 속성만) |
| 3 | `v2_theme.qss` 전면 재작성 |
| 4 | 인라인 스타일 → objectName/QSS 전환 |
| 5 | 공통 컴포넌트화 (상태 라벨, overlay card, panel header) |

### 권장 진행 순서

```
Phase 9 (성능) → Phase 10 (구조) → Phase 11 (UI/UX)
```

이유:
- 성능이 먼저 — 무거운 앱에 디자인 올려도 체감 안 좋음
- 구조 정리가 UI/UX 전에 — `screens.h`/`mainwindow`/`common_ui` 정리 안 하고 디자인 작업하면 충돌 많음
- UI/UX가 마지막 — 안정된 구조 위에 디자인을 올려야 유지보수 가능

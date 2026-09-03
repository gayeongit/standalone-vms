# VMS v2 설계 합의 사항

- 최종 갱신: 2026-03-05
- 상태: 설계 준비 단계

> 이 문서는 v2 설계/구현 과정에서 합의된 결정 사항을 누적 기록합니다.

---

## 1. 프로젝트 구조 / Git 전략

### 1.1 브랜치 정책

| 브랜치 | 용도 | 비고 |
|--------|------|------|
| `main` | v1 코드 고정 | v1 핫픽스만 허용 |
| `v2-dev` | v2 설계/구현 | v2 전용 작업 브랜치 |

### 1.2 로컬 작업 환경 — git worktree

```
VEDA_FINAL/Qt/
├── VMS_v1/          ← main 브랜치 (현재 원본 폴더, v1 참고용)
└── VMS_v2_dev/      ← v2-dev worktree (신규 추가, v2 작업 폴더)
```

- **worktree 방식 채택 사유**: 리포 중복 없이 v1/v2 코드를 동시에 열어 참고 가능
- 현재 `VMS_v1/` 폴더가 그대로 `main` 워킹트리 역할
- `VMS_v2_dev/`만 추가로 생성

### 1.3 v2 개발 방식 — "신규 생성"이 아니라 "리모델링"

> v2를 새로 판다 = 폴더 신규 생성이 아니라, **v2-dev 브랜치에서 v1을 v2 구조로 변환**하는 것.

- `git worktree add`로 `VMS_v2_dev/` 폴더 생성 시 **v1 파일이 전부 체크아웃됨** (v2-dev가 main에서 분기했기 때문)
- Qt Creator에서 "새 프로젝트"를 만들지 않고, **기존 `CMakeLists.txt`를 열어서 수정**
- 히스토리/비교/커밋 추적이 깨끗하게 유지됨

### 1.4 Git 실행 순서

```powershell
# 1. v1 최종 커밋 (미완료 시 실행)
git checkout main
git add .
git commit -m "chore: v1_10b final"
git push origin main

# 2. v2 브랜치 생성
git checkout -b v2-dev
git push -u origin v2-dev

# 3. worktree 추가 (VMS_v1 폴더에서 실행)
git checkout main              # 원본 폴더를 main으로 복귀
git worktree add ..\VMS_v2_dev v2-dev
```

### 1.5 worktree 생성 후 실제 작업 흐름

| 단계 | 할 일 | 비고 |
|------|-------|------|
| 1 | `VMS_v2_dev/` 폴더를 Qt Creator에서 열기 | 기존 CMakeLists.txt로 열림 |
| 2 | **재작성 대상** 파일 내용 비우고 새로 작성 | `stream_player`, `native_capture` |
| 3 | **불필요한 파일** 삭제 | 필요 시 판단 |
| 4 | **유지할 파일** v2 기준으로 수정 | CMake, AppState, 공통 UI 등 |
| 5 | `docs/`에 설계서/개발계획서 추가 | 신규 문서 |
| 6 | 커밋 | `feat: initialize v2 structure` |

---

## 2. v1 → v2 파일 이관 계획

### 2.1 유지 후 수정 (재사용)

| 파일 | 수정 범위 | 비고 |
|------|----------|------|
| `CMakeLists.txt` | 타겟명/소스 목록 수정 | 빌드 골격 유지 |
| `main.cpp` | 거의 그대로 | QSS 로드 + MainWindow 생성 |
| `app_state.h/cpp` | 필드 확장 | 싱글턴 구조 유지 |
| `mainwindow.h/cpp` | 화면 전환 로직 수정 | 설정/라우팅 유지 |
| `screens.h` | 클래스 선언 수정 | 화면 구조 동일 |
| `common_widgets.h/cpp` | 위젯 수정/확장 | Topbar/Sidebar/EventView |
| `common_ui.h/cpp` | 유틸 함수 수정 | 대부분 재사용 |
| `popup_manager.h/cpp` | 거의 그대로 | 팝업 구조 동일 |
| `styles/v1_theme.qss` | → `v2_theme.qss` | 테마 기반 재사용 |

### 2.2 재작성 (v2 핵심 변경)

| 파일 | 사유 | v2 방향 |
|------|------|---------|
| `stream_player.h/cpp` | 렌더 백엔드 완전 교체 | `appsink` + `QOpenGLWidget` |
| `native_capture.h/cpp` | BitBlt 제거 | 파이프라인 프레임 직접 추출 |

### 2.3 조건부 (설계 확정 후 결정)

| 파일 | 조건 | 비고 |
|------|------|------|
| `clip_capture_manager.h/cpp` | 인터페이스 유지, 내부 비동기화 | `QtConcurrent`/`QThread` 검토 |
| `channel_session_manager.h/cpp` | 렌더 백엔드 변경에 맞춰 재설계 | 바인딩 로직 변경 |
| `dummy_data.h/cpp` | 서버 연동 범위에 따라 결정 | 서버 미연동 시 유지 |

---

## 3. v2 핵심 기술 변경 (로드맵 연동)

> 상세: `docs/VMS_v2_roadmap.md` 참조

### 3.1 확정 사항

| 항목 | v1 | v2 |
|------|----|----|
| 영상 렌더링 | `d3d11videosink` → HWND | `appsink` → `QImage` → `QOpenGLWidget` |
| OSD 오버레이 | native window 위 Qt 라벨 (제한적) | 같은 렌더 파이프에서 합성 (완전 투명) |
| 화면 캡처 | `BitBlt` (Windows 전용) | `last-sample` / 파이프라인 프레임 추출 |
| 인코딩 | 동기 ffmpeg 호출 | 비동기 ffmpeg (UI 블로킹 제거) |

### 3.2 확정 완료

| 항목 | 결정 | 확정일 |
|------|------|--------|
| 통신 프로토콜 | **WebSocket** (`wss://`) | 2026-03-04 |
| 렌더 백엔드 | **appsink + QOpenGLWidget** | 2026-03-04 |
| 서버 DB | **PostgreSQL** | 2026-03-04 |

### 3.3 미확정 사항

| 항목 | 후보 | 결정 주체 | 결정 마감 |
|------|------|----------|----------|
| 로컬 캐시 DB (SQLite) | 이벤트 히스토리, 장치 목록 오프라인 캐시 | 프론트 팀 | 설계 초기 (WS 계약 확정 직후) |
| ONVIF Discovery | gSOAP vs QUdpSocket | 프론트 팀 | v2 후반기 (우선순위 낮음) |
| QThread 분리 | 조건부 도입 | 프론트 팀 | 프로파일링 후 판단 |

> 마감 일자는 v2 스케줄 확정 후 구체 날짜로 갱신 예정.

---

## 4. CCTV 제어 (줌/포커스)

### 4.1 제어 방식
- **서버 API 경유**: VMS → 서버 API (JWT) → 서버가 CGI 호출 → 결과 리턴
- VMS는 **채널명 + 제어값만 전송**, 카메라 IP/ID/PW를 모름
- 서버가 DB(장치 IP) + config 파일(ID/PW)로 CGI URL 조합
- CGI 엔드포인트: `http://<카메라 IP>/stw-cgi/attributes.cgi/cgis`
- Zoom 제어값: `-100`, `-10`, `-1`, `1`, `10`, `100` (enum)

### 4.2 응답 검증 (필수, 서버 측)
- HTTP status code 확인 (200 OK)
- body 내 OK/에러 코드 파싱
- 타임아웃 처리 (3초 권장)
- 실패 시 VMS에 에러 응답 → UI 에러 표시

### 4.3 설계 원칙
- **VMS**: 서버 API 호출만 (`POST /api/devices/{ch}/zoom` 등)
- **서버**: config 파일에서 카메라 인증정보 참조 → CGI 호출
- VMS는 카메라 IP/ID/PW를 **모름** (서버만 알고 있음)

### 4.4 카메라 인증정보 관리

| 항목 | 위치 | 설명 |
|------|------|------|
| 장치 목록 (IP, 채널명, RTSP 경로) | **DB (PostgreSQL)** | 장치 CRUD |
| 카메라 ID/PW | **서버 config 파일** | ONVIF·CGI·RTSP 인증 공통 사용 |
| VMS 클라이언트 | **모름** | 채널명만 알고 서버 API 호출 |

> 카메라 인증정보는 DB가 아닌 서버 config 파일에 보관.
> ONVIF 탐색, CGI 제어, mediaMTX yml 생성 시 서버가 config에서 읽어 사용.

---

## 5. UGV 제어 (팬/틸트/드라이브/속도)

### 5.1 제어 방식
- 서버 경유 WebSocket (`wss://`)
- 클라이언트 3단계: **명령 발행 → ack 수신 → 상태 반영**

### 5.2 WS 메시지 계약 — 서버팀 합의 필요 ⚠️

> **이것이 v2 개발 착수 전 가장 먼저 고정해야 할 항목.**

| 계약 항목 | 내용 | 상태 |
|-----------|------|------|
| 메시지 포맷 | JSON 구조 (`type`, `payload`, `timestamp`, `requestId` 등) | 미합의 |
| UGV 명령 포맷 | 드라이브/PTZ 명령 JSON 스키마 | 미합의 |
| UGV 응답 포맷 | ack/nack + 상태 업데이트 | 미합의 |
| 에러 코드 체계 | 숫자 코드 + 메시지 (e.g. `4001: DEVICE_OFFLINE`) | 미합의 |
| 인증 토큰 전달 | 초기 `auth` 메시지? WS 헤더? URL 쿼리 파라미터? | 미합의 (§7.3 참조) |
| 재연결 상태 복구 | 서버가 마지막 상태를 push? 클라이언트가 re-subscribe? | 미합의 |
| heartbeat/ping | 주기, 타임아웃 정책 | 미합의 |
| 이벤트 메시지 포맷 | 이벤트 타입/레벨/소스/타임스탬프 JSON 스키마 | 미합의 |

> 서버팀과 합의 후 별도 WS API 명세서 문서를 만들고, 이 테이블을 확정 상태로 갱신할 것.

---

## 6. 이벤트 시스템

### 6.1 데이터 흐름
- **실시간**: WebSocket으로 수신 → UI 즉시 반영
- **히스토리**: DB/API 조회 → 이벤트 검색/필터
- 두 경로 분리 처리 (WS 스트림 ≠ DB 조회)

### 6.2 DB 업데이트
- WS 수신 이벤트는 DB에 계속 저장/누적
- 클라이언트 포맷: 서버팀과 합의 필요 (JSON 스키마)

---

## 7. 인증 (JWT)

### 7.1 기본 정책
- 로그인 시 JWT 토큰 발급
- Access token 만료/갱신 정책 필요
- 401 발생 시 재로그인 플로우

### 7.2 클라이언트 보안 정책
- **토큰 만료**: Access token / Refresh token 만료 시간은 서버팀 확정 후 반영
- **토큰 저장**: 메모리 변수 보관 (디스크 영속화 금지). 앱 재시작 시 재로그인
- 클라이언트는 토큰 저장/전달만 담당, **서명 검증은 서버 책임**

### 7.3 서버팀 합의 필요
- WS 연결 시 토큰 전달 방식 (헤더? 쿼리? 초기 auth 메시지?)
- 토큰 갱신 메커니즘 (refresh token 여부)
- Access / Refresh 만료 시간

---

## 8. 영상 아카이브 / 클립 / mediamtx

### 8.1 백업(Archive) vs 클립(Clip) 구분

| 구분 | 백업 (Archive) | 클립 (Clip) |
|------|---------------|-------------|
| 트리거 | **자동** — mediaMTX 상시 녹화 | **수동** — 사용자 버튼 클릭 |
| 대상 | 등록된 전 채널 (CCTV 상시, UGV 출동 중만) | 현재 선택/표시 채널 |
| 저장 위치 | **서버(라즈베리파이)** 로컬 디스크 | **VMS 클라이언트** 로컬 경로 |
| 저장 형식 | fmp4 세그먼트 (1분 단위) | MP4 (appsink 프레임 추출) |
| 보관 정책 | 30일 자동 삭제 (`recordDeleteAfter: 720h`) | 사용자 관리 (자동 삭제 없음) |
| VMS on/off | **무관** — mediaMTX 독립 동작 | VMS 실행 중에만 가능 |

### 8.1.1 전체 데이터 흐름

> VMS는 DB에 직접 접근하지 않음. 모든 데이터는 서버 API(JWT) 경유.
> 카메라 인증정보(ID/PW)는 서버 config 파일에 보관 (§4.4).

```
┌─────────────┐     ┌────────────┐
│ PostgreSQL  │     │ config 파일 │
│  (서버 DB)  │     │ (서버 로컬) │
│  채널명,    │     │  camera_id  │
│  IP, RTSP   │    │  camera_pass │
│  경로       │     │             │
└──────┬──────┘     └──────┬──────┘
       │                    │
       └────────┬───────────┘
                │ ① Pi 부팅 시 / VMS 동기화 요청 시
                ▼
       ┌─────────────────┐
       │  서버 (Pi)       │  DB + config 조합 → mediamtx.yml 생성
       │  mediamtx-init   │  paths:
       │                  │    ch0:
       │                  │      source: rtsp://admin:pw@IP/... ← DB+config 조합
       └────────┬─────────┘
                │ ② yml 로드 + 자동 시작 (systemd)
                ▼
       ┌─────────────────┐
       │   mediaMTX       │  RTSP 수신 + fmp4 녹화 (1분 세그먼트, 24/7)
       │   (서버/Pi)      │  recordings/ch0/2026-03-04_16-33-00.mp4
       │                  │  recordings/ch1/2026-03-04_16-33-00.mp4
       └────────┬─────────┘
                │ ③ Playback API (포트 9996)
                ▼
       ┌─────────────────┐
       │   VMS (Qt)       │  /list?path=ch0  ← 서버 API에서 받은 채널명 사용
       │   Playback       │  → [{start, duration, url}]
       │                  │  /get?path=ch0&start=... ← /list 응답의 url 사용
       │                  │  → 연속 영상 재생
       └─────────────────┘
```

```
[클립 (수동)]
  VMS → appsink 프레임 추출 → 로컬 MP4 저장 (서버/DB 무관)
```

### 8.2 아카이브 인덱싱 전략 (Phase 1 / Phase 2)

#### Phase 1: 서버 API 경유 (확정) ✅ **mediaMTX API 검증 완료 (2026-03-04)**

> **VMS는 mediaMTX를 직접 호출하지 않음.** 서버 API(JWT)를 통해 Playback 요청.
> 서버 내부에서 mediaMTX Playback API(`/list`, `/get`)를 호출하고 결과를 VMS에 전달.
> mediaMTX API 테스트 상세: `docs/research/mediamtx_playback_api_test.md`

**mediaMTX 설정 (서버 측, 필수 2가지):**

```yml
# 1. Playback 서버 활성화 (최상위 키, L230)
playback: yes
playbackAddress: :9996

# 2. 녹화 설정 (pathDefaults 하위, L514~)
pathDefaults:
  record: yes
  recordPath: ./recordings/%path/%Y-%m-%d_%H-%M-%S-%f
  recordFormat: fmp4
  recordPartDuration: 1s
  recordSegmentDuration: 1m
  recordDeleteAfter: 720h    # 30일 보관
```

> ⚠️ `playback`과 `pathDefaults`는 yml 최상위 키. `paths:` 블록 안에 넣으면 path 이름으로 해석되어 작동 안 함.

**VMS Playback 흐름 (서버 API 경유):**

> 서버팀 API 설계 참조: `docs/통신 API 설계 - RESTful API.csv`

```
1. VMS → GET /playback/dates/{date}/channels (JWT)
   ← 해당 날짜에 녹화된 채널 목록

2. VMS → GET /playback/timeline?channelId=100&date=2026-03-04 (JWT)
   ← { availableRanges, gaps, eventMarkers }
   → 타임라인 UI에 녹화 구간 + gap + 이벤트 마커 표시

3. 사용자가 시간대 클릭
   → VMS → GET /playback/stream?channelId=100&ts=... (JWT)
   ← { protocol: "HLS", uri: "/api/v1/playback/hls/req_abc.m3u8" }
   → 서버가 제공하는 HLS URL로 재생
```

**서버 내부 (VMS에서 안 보이는 부분):**

```
서버가 /playback/* 요청 수신
  → mediaMTX /list?path={ch} 호출 (localhost:9996, 서버 내부)
  → 결과를 가공하여 VMS에 응답
  → /stream 요청 시 mediaMTX /get으로 영상 획득 → HLS로 변환하여 제공
```

**VMS 클라이언트 구현 범위:**

| 항목 | 구현 내용 |
|------|----------|
| 녹화 채널 조회 | `GET /playback/dates/{date}/channels` (JWT) |
| 타임라인 조회 | `GET /playback/timeline?channelId=...&date=...` (JWT) |
| 구간 재생 요청 | `GET /playback/stream?channelId=...&ts=...` (JWT) → HLS URL 수신 |
| 타임라인 표시 | 응답의 `availableRanges` + `gaps`로 구간 시각화 |
| 이벤트 마커 | 응답의 `eventMarkers`로 타임라인 위에 이벤트 표시 |
| gap 처리 | `gaps` 배열의 구간 → NO VIDEO 표시 |

**mediaMTX API 테스트에서 확인된 사항 (서버 구현 참고):**

| 항목 | 내용 |
|------|------|
| 시간대 | `/list` 응답이 `+09:00`(로컬) 기준. `Z`(UTC)로 변환하면 실패. **응답 url 그대로 사용 권장** |
| 연속 구간 합침 | `/list`는 개별 세그먼트가 아닌 연속 구간을 하나로 합쳐서 반환 |
| yml 설정 위치 | `playback`, `pathDefaults`는 최상위 키. `paths:` 블록 안에 넣으면 작동 안 함 |

#### Phase 2: DB 인덱스 캐시 추가 (조건부)

> **Phase 1에서 아래 문제가 실제로 발생할 경우에만 전환.**

| 전환 조건 | 설명 |
|----------|------|
| `/list` 응답 느림 | 채널 수 × 30일분 조회가 느릴 때 |
| 복합 검색 필요 | 시간 + 이벤트 타입 + 채널 조합 필터링 |
| 통계/리포트 요구 | 채널별 녹화 시간 통계, 가동률 등 |
| 권한 제어 필요 | 사용자별 접근 가능 채널/시간대 제한 |

**Phase 2 설계 (필요 시):**

| 필드 | 타입 (논리) | 설명 |
|------|------------|------|
| `channel_id` | string | 채널 식별자 |
| `source_type` | enum | `cctv` / `ugv` |
| `start_time` | ISO 8601 (UTC) | 세그먼트 시작 시각 |
| `end_time` | ISO 8601 (UTC) | 세그먼트 종료 시각 |
| `file_path` | string | 파일 경로 |
| `file_size` | integer (bytes) | 파일 크기 |
| `status` | enum | `complete` / `recording` / `error` |

> Phase 2 전환 시 서버에 archive-indexer(파일→DB 동기화) 구현 필요.
> DB 스키마는 서버팀(PostgreSQL) 소관.

### 8.3 mediamtx 녹화 확정 사항

| 항목 | 결정 | 검증 |
|------|------|------|
| 녹화 주체 | **mediaMTX** (서버에서 상시 동작) | ✅ |
| 저장 포맷 | **fmp4** (fragmented MP4) | ✅ |
| 세그먼트 단위 | **1분** | ✅ |
| 보관 기간 | **30일** (자동 삭제) | — |
| 저장 위치 | 라즈베리파이 로컬 디스크 (`/data/recordings/`) | ✅ |
| Playback 서버 | `playback: yes`, 포트 9996 | ✅ |
| 인덱싱 1차 전략 | 서버 API 경유 (내부에서 mediaMTX `/list`+`/get` 사용) | ✅ **API 테스트 통과** |
| DB 인덱스 테이블 | **초기 스코프 제외** (Phase 2 조건부) | — |
| archive-indexer 서비스 | **초기 스코프 제외** (Phase 2 조건부) | — |

### 8.4 mediamtx 자동 구성 — ✅ 확정

- 서버가 DB(장치 목록) + config 파일(카메라 ID/PW)을 조합하여 yml 생성
- **확인됨**: 서버 구동 중 yml에 새 RTSP 주소 추가 시 바로 반영됨
- yml 생성 로직은 하나, 호출 시점이 두 개 (부팅 / API 요청)

### 8.5 mediamtx 관리 책임 — ✅ 확정

| 항목 | 결정 | 비고 |
|------|------|------|
| yml 생성/갱신 주체 | **서버(Pi)** | VMS는 yml 건드리지 않음 |
| mediamtx 프로세스 관리 | **systemd** (Pi 부팅 시 자동 시작) | `mediamtx.service` |
| yml 갱신 원자성 | **tmp → rename** | 부분 쓰기 방지 |
| 장치 삭제 동기화 | 동기화 트리거 시점에 DB 전체 목록 기준으로 yml 재생성 | 삭제된 장치는 자동 제거 |
| 수정/삭제 반영 | 변경 감지 시 hot-reload 시도, 미지원 시 `systemctl restart` | 추가는 확인됨, 삭제/수정은 추가 테스트 필요 |

#### 8.5.1 동기화 트리거 (폴링 없음)

| 시점 | 트리거 | 설명 |
|------|--------|------|
| Pi 부팅 | `mediamtx-init.service` (one-shot, **필수**) | DB+config → yml 생성 → mediaMTX 시작 |
| VMS 로그인 | `POST /api/mediamtx/sync` (JWT) | 장치 목록 최신화 확인 |
| ONVIF 탐색/장치 CRUD | `POST /api/mediamtx/sync` (JWT) | 장치 추가/삭제 후 즉시 반영 |

> **폴링 데몬 없음.** 장치 변경은 사용자 행위(로그인, ONVIF 탐색)에 의해서만 발생.
> 다중 운영자/외부 시스템이 DB를 직접 수정하게 되면 그때 폴링 또는 이벤트 브로커 추가 검토.

#### 8.5.2 부팅 순서

```
Pi 부팅
  → ① PostgreSQL 시작 (systemd)
  → ② mediamtx-init.service (one-shot)
       DB + config → mediamtx.yml 생성
  → ③ mediamtx.service 시작 (After=mediamtx-init)
       yml 읽고 RTSP 연결 + 녹화 시작 (VMS 없어도 24/7)
```

#### 8.5.3 VMS 클라이언트 역할 (yml 관련)

- DB CRUD: 서버 API로 장치 추가/삭제 (`POST /api/devices`, JWT)
- 동기화 요청: `POST /api/mediamtx/sync` (JWT) — "yml 새로 만들어줘"
- **VMS가 직접 yml을 수정하거나 SSH로 Pi에 접근하는 것은 금지**

---

## 9. v2 추가 의존성

v1에 없던 항목:

| 모듈 | 용도 | 비고 |
|------|------|------|
| `Qt::Network` | 서버 API 호출 (CCTV 제어, 장치 CRUD, mediaMTX sync 등) | `QNetworkAccessManager` |
| `Qt::WebSockets` | WS 통신 (UGV 제어, 이벤트, 인증) | `QWebSocket` |
| `OpenSSL` | TLS (`wss://`, `https://`) | Qt TLS 백엔드 |
| `Qt::OpenGLWidgets` | appsink 영상 렌더링 | `QOpenGLWidget` |

> **`Qt::Sql` 불필요**: VMS는 DB에 직접 접근하지 않음. 서버 API(HTTP/WS)로만 통신.
> 향후 로컬 SQLite 캐시(§3.3)를 도입할 경우에만 추가 검토.

### 9.1 OpenSSL 배포 조건
- Windows: `libssl-3-x64.dll`, `libcrypto-3-x64.dll` 필요
- 배포 방식: 앱 번들(같이 배포) vs 시스템 설치 — **v2 배포 정책 확정 시 결정**
- Qt Installer 또는 `windeployqt` 사용 시 자동 포함 가능

---

## 10. 구현 우선순위 (권장)

| 순서 | 작업 | 사유 | 선행 조건 |
|------|------|------|----------|
| **1** | **appsink + QOpenGLWidget 렌더링** | **v2 핵심 변경**. OSD 투명, 캡처 백엔드 교체 전부 이것에 의존. **반드시 v2 첫 번째 작업** | 없음 |
| 2 | WS 클라이언트 뼈대 (연결/재연결/ack) | 전체 통신 기반 | WS 메시지 계약 합의 (§5.2) |
| 3 | JWT 인증 플로우 | 로그인 후 모든 기능에 선행 | WS 인증 방식 합의 (§7.3) |
| 4 | CCTV 제어 서버 API 연동 | 서버 경유 CGI (§4) | 없음 (병렬 진행 가능) |
| 5 | 이벤트 WS 수신 + UI 반영 | WS 뼈대 완성 후 | #2 완료 |
| 6 | mediamtx-init / sync API (서버 측) | 서버(Pi)에서 yml 생성/갱신 (§8.5) | mediamtx 관리 책임 확정 (§8.5) ✅ |
| 7 | Playback — 서버 API 연동 | 서버 Playback API 기반 타임라인 + HLS 재생 (§8.2) | 서버 Playback API 완성 |

---

## 11. 서버팀 합의 필요 항목 체크리스트

> v2 개발 착수 전 서버팀과 반드시 합의해야 할 항목들

| # | 항목 | 관련 섹션 | 상태 |
|---|------|----------|------|
| 1 | WS 메시지 계약 (포맷/에러코드/재연결) | §5.2 | 📝 초안 작성됨, 서버팀 합의 대기 |
| 2 | JWT 인증 — WS 토큰 전달 방식 | §7.3 | ❌ 미합의 |
| 3 | 영상 백업 — 1분 단위 저장 주체 | §8.3 | ✅ **mediaMTX 녹화 확정** |
| 4 | DB 선택 — 서버 DB 종류 | §3.2 | ✅ **PostgreSQL 확정** |
| 5 | mediamtx yml 관리 주체 | §8.5 | ✅ **서버(Pi) 확정** |
| 6 | 이벤트 메시지 JSON 스키마 | §5.2, §6.2 | 📝 초안 작성됨, 서버팀 합의 대기 |

---

## 12. 합의 이력

| 날짜 | 항목 | 결정 |
|------|------|------|
| 2026-03-04 | 브랜치 전략 | `main`(v1) + `v2-dev`(v2), worktree 채택 |
| 2026-03-04 | 폴더 구조 | 같은 리포, v1/v2 폴더 분리 (worktree) |
| 2026-03-04 | v2 시작 방식 | v1 리모델링 (백지 X, 전체 복사 X) |
| 2026-03-04 | 파일 이관 범위 | §2 리스트 기준 |
| 2026-03-04 | 렌더 백엔드 | appsink + QOpenGLWidget 확정 |
| 2026-03-04 | OSD | v2에서 완전 투명 합성으로 해결 |
| 2026-03-04 | 캡처 | BitBlt 제거, 파이프라인 프레임 추출 |
| 2026-03-04 | 인코딩 | 비동기 전환 확정 |
| 2026-03-04 | 통신 프로토콜 | **WebSocket 확정** |
| 2026-03-04 | v2 개발 방식 | v1 리모델링 방식 확정 |
| 2026-03-04 | worktree 폴더 구조 | VMS_v1(그대로) + VMS_v2_dev(추가) |
| 2026-03-04 | Qt Creator 사용법 | 새 프로젝트 X, 기존 CMakeLists.txt 열어서 수정 |
| 2026-03-04 | CCTV 제어 | HTTP CGI 호출 (→ 03-05에 서버 API 경유로 변경, §4 참조) |
| 2026-03-04 | UGV 제어 | 서버 경유 WebSocket |
| 2026-03-04 | 이벤트 | WS 수신 + DB 누적 |
| 2026-03-04 | 인증 | JWT 적용 확정 |
| 2026-03-04 | 영상 백업 | mediaMTX 상시 녹화 (fmp4, 1분 세그먼트, 30일) |
| 2026-03-04 | mediamtx | DB IP → yml → 자동 구동, 동적 반영 확인됨 |
| 2026-03-04 | OpenSSL | TLS 통신 계층 (wss, https CGI) |
| 2026-03-04 | 빌드 의존성 | Qt Network/WebSockets/OpenGL + OpenSSL 추가 (Sql은 03-05에 불필요로 확정) |
| 2026-03-04 | appsink 구현 시점 | **v2 첫 번째 작업으로 확정** |
| 2026-03-04 | WS 메시지 계약 | 서버팀 합의 필요 항목 목록화 (§5.2, §11) |
| 2026-03-04 | mediamtx 관리 책임 | yml 갱신/프로세스 관리/동기화 검토 항목 정리 (§8.5) |
| 2026-03-04 | 서버 DB | **PostgreSQL 확정** |
| 2026-03-04 | JWT 보안 정책 | 토큰 메모리 보관, 디스크 영속화 금지 (§7.2) |
| 2026-03-04 | OpenSSL 배포 | 배포 방식 결정 항목 추가 (§9.1) |
| 2026-03-04 | 녹화 주체 | **mediaMTX 확정** — fmp4, 1분 세그먼트, 30일 보관 |
| 2026-03-04 | 아카이브 인덱싱 | Phase 1: mediaMTX API 검증 완료 (→ 03-05에 서버 API 경유로 확정) |
| 2026-03-04 | 백업 vs 클립 분리 | 백업=서버 자동(mediaMTX), 클립=사용자 수동(VMS 로컬) |
| 2026-03-04 | **Playback API 테스트** | `/list` + `/get` 검증 완료. 녹화/조회/재생/세그먼트 stitch 모두 정상 (`docs/research/`) |
| 2026-03-05 | CCTV 제어 방식 변경 | 직접 CGI → **서버 API 경유** (VMS가 카메라 IP/ID/PW 모름) |
| 2026-03-05 | 카메라 인증정보 관리 | DB가 아닌 **서버 config 파일** 보관 (ONVIF·CGI·RTSP 공통) |
| 2026-03-05 | mediamtx 관리 책임 확정 | **서버(Pi)** — systemd 자동 시작, VMS는 API 호출만 |
| 2026-03-05 | mediamtx 동기화 트리거 | 폴링 없음. ① Pi 부팅 1회(필수) ② 로그인 시 ③ ONVIF/장치 CRUD 시 |
| 2026-03-05 | yml 갱신 방식 | 원자적(tmp→rename), 변경 시만 reload/restart |
| 2026-03-05 | VMS→DB 직접 접근 | **금지** — 서버 API(JWT) 경유만 허용 |
| 2026-03-05 | `Qt::Sql` 의존성 | 클라이언트 불필요 (DB 직접 접근 안 함). 로컬 SQLite 시만 검토 |
| 2026-03-05 | JWT 적용 범위 | 우리 서버 API/WS만. 카메라 CGI·mediaMTX·RTSP는 각자 인증 |
| 2026-03-05 | Playback 경로 확정 | **서버 API 경유(B안)** — VMS→서버 API(JWT)→서버 내부에서 mediaMTX 호출. VMS가 mediaMTX 직접 호출 금지 |
| 2026-03-05 | Playback 프로토콜 | 서버가 mediaMTX 세그먼트를 **HLS로 변환 제공** (서버팀 API 설계 기준) |
| 2026-03-06 | REST API 변경 | `GET /devices` 응답에 channels 배열 포함 (JOIN 응답) |
| 2026-03-06 | RTSP URL 정책 | VMS에는 mediaMTX 경유 URL만 반환. 카메라 직접 주소 노출 금지 |
| 2026-03-06 | 설정 저장 방식 | **QSettings 유지** 확정 — 마이그레이션 코스트 0 우선 |
| 2026-03-06 | 화면 크기 변경 | 로그인 360×320, 회원가입 360×420, 장치확인 500×360 |
| 2026-03-06 | Qt 모듈 정책 | 소켓/스레드/HTTP 전부 Qt 내장 사용. 외부 라이브러리는 GStreamer/FFmpeg만 |

---

### 13.1 API 호출 방식 — CSV 원본 유지 (API 분리)

장치 확인 화면에서 devices + channels + device_health 3개 테이블이 필요하므로,
응답을 한 번에 합치기보다 **기존 CSV 설계대로 API를 분리**하는 것이 깔끔함.

| API | 용도 | 사용 화면 |
|-----|------|----------|
| `GET /devices` | 장치 목록 (type, model, name, health) | 장치 확인 |
| `GET /device/{deviceId}/channels` | 채널 목록 (channelId, name, rtspUrl) | 메인 영상, 설정 |
| `GET /channel/{channelId}` | 채널 상세 (rtspUrl 포함) | 영상 연결 |

> `channels.rtsp_url`에는 **mediaMTX 경유 주소**가 저장됨.
> VMS는 이 주소를 그대로 GStreamer에 전달.


### 13.2 RTSP URL 정책 — ✅ 확정

| 구분 | URL | 저장 위치 | 누가 사용 |
|------|-----|----------|----------|
| 카메라 직접 주소 | `rtsp://192.168.0.69:554/0/onvif/...` | DB `devices.addr` | mediaMTX만 (init 스크립트가 yml 생성 시 사용) |
| **mediaMTX 경유 주소** | `rtsp://서버IP:8554/{path명}` | DB `channels.rtsp_url` | **VMS 클라이언트** (API 응답으로 받아서 사용) |


### 13.3 장치 확인 화면 표시 항목

| 항목 | 출처 |
|------|------|
| 타입 (CCTV/UGV) | `devices.type` |
| 모델명 | `devices.model` |
| 장치명 | `devices.name` 또는 `devices.uuid` |
| 온라인 여부 | `device_health.conn_state` |
| IP 주소 | **표시하지 않음** (보안) |

### 13.4 설정 > 장치 수정 표시 항목

| 항목 | 출처 |
|------|------|
| 장치명 | `channels.name` (수정 가능) |
| 종류 | `devices.type` |
| RTSP URL | mediaMTX 경유 URL (읽기 전용) |

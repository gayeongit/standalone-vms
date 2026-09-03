# VMS v2 개발 실행 계획

- 기준 문서:
  - `VMS_v2_architecture.md`
  - `VMS_v2_pending_decisions.md`
- 범위: UGV 래퍼 타입(A-2) 최종 확정 전까지는 UGV 세부 제어를 제외하고 진행

## 문서 운용 기준

- 메인 문서(실행 기준):
  - `VMS_v2_dev_execution_plan.md`
- 참고 문서(설계/결정 확인):
  - `VMS_v2_architecture.md`
  - `VMS_v2_pending_decisions.md`
- API 기준 문서:
  - `통신 API 설계 - RESTful API`
  - `통신 API 설계 - WebSocket 이벤트 구독_events_ws API`
  - `통신 API 설계 - WebSocket Client to UGV제어_ gw_ws API`
  - `통신 API 설계 - WebSocket Server to UGV 제어_ugv_ws API`

> 개발 진행 중 의사결정은 메인 문서에 먼저 반영하고, 필요 시 참고 문서를 동기화한다.

---

## v1 -> v2 파일 정리 기준 (확정본)

### 1) 유지 후 수정 (재사용)

| 파일 | 작업 |
|------|------|
| `CMakeLists.txt` | v2 소스/타겟 반영 |
| `main.cpp` | 기본 구조 유지, 초기화 정책 반영 |
| `app_state.h/.cpp` | 필드 확장(`isAuthenticated`, `wsConnected`, 경로 분리 등) |
| `mainwindow.h/.cpp` | 화면 전환/인증 복귀/런타임 라우팅 업데이트 |
| `screens.h` | 현재 구조 기준 Signup/DeviceCheck 선언 및 화면 클래스 시그널/멤버 정리, 필요 시 후속 파일 분리 |
| `login_screen.cpp` | 로그인 화면 서비스 호출 구조 반영 |
| `screens.cpp` | 현재 구조 기준 Signup/DeviceCheck 화면 로직 수정, 필요 시 후속 파일 분리 |
| `main_screen.cpp` | 이벤트뷰 토글 + 알림센터 연계 |
| `cctv_screen.cpp` | CCTV 제어 API 연동, 이벤트뷰 제거 정책 반영 |
| `ugv_screen.cpp` | UGV 제어/상태 연동(스낵바 이벤트 제거) |
| `playback_screen.cpp` | Playback API/HLS/알림센터 정책 반영 |
| `common_widgets.h/.cpp` | Topbar 알림 버튼/배지, 공통 상태 슬롯, 검색 진입점 |
| `common_ui.h/.cpp` | 공통 유틸 보강 |
| `popup_manager.h/.cpp` | 오류/안내 팝업 재사용 |
| `channel_session_manager.h/.cpp` | appsink 렌더 백엔드에 맞춰 조정 |
| `clip_capture_manager.h/.cpp` | `QtConcurrent` 비동기 기준으로 수정 |
| `resources.qrc` | 아이콘/리소스 등록 업데이트 |
| `styles/v1_theme.qss` | 필요 스타일 유지 참조, `v2_theme.qss`로 이관 |

### 2) 재작성/삭제

| 파일 | 작업 |
|------|------|
| `stream_player.h/.cpp` | 재작성 (`appsink + QOpenGLWidget`) |
| `native_capture.h/.cpp` | 삭제 방향 확정, 실제 삭제는 Phase 1-B 조건 충족 시 수행 |

### 3) 신규 추가

| 파일 | 역할 |
|------|------|
| `rest_client.h/.cpp` | REST 공통 래퍼 |
| `ws_client.h/.cpp` | WS 연결/재연결/메시지 라우팅 |
| `auth_service.h/.cpp` | 로그인/로그아웃/토큰 정책 |
| `device_service.h/.cpp` | 장치/채널 조회 |
| `cctv_control_service.h/.cpp` | 줌/포커스 제어 |
| `event_service.h/.cpp` | 이벤트 REST/WS 통합 + 캐시 |
| `playback_service.h/.cpp` | Playback API |
| `ugv_service.h/.cpp` | UGV 제어/상태 (Phase 8) |
| `video_render_widget.h/.cpp` | QOpenGLWidget 기반 신규 렌더 위젯(Phase 1 핵심) |
| `styles/v2_theme.qss` | v2 스타일 (생성/전환: Phase 3 이후 공통 UX 안정화 시점) |

### 4) 조건부 항목 최신 상태

| 항목 | 최신 결정 |
|------|-----------|
| `clip_capture_manager` 비동기 방식 | `QtConcurrent`로 시작, 문제 발생 시 `QThread` 전환 |
| `channel_session_manager` | 조건부 아님, v2에서 수정 확정 |
| `dummy_data.h/.cpp` | 서버 연동 진행 기준으로 단계적 제거 |

## Phase별 파일 매트릭스

| Phase | 핵심 수정 파일 | 신규 추가 파일 | 연쇄 수정 파일 | 삭제 후보 파일 | 삭제 조건 | 더미 제거 범위 |
|------|----------------|----------------|----------------|----------------|-----------|----------------|
| Phase 1-A 렌더링 코어 | `stream_player.h/.cpp`, `channel_session_manager.h/.cpp` | `video_render_widget.h/.cpp` | `CMakeLists.txt` | 없음 | N/A | 없음 |
| Phase 1-B 캡처/저장 경로 | `clip_capture_manager.h/.cpp`, 경로/설정 처리 코드 | 없음 | `app_state.h/.cpp`(필요 시), `common_ui.h/.cpp`(필요 시), `CMakeLists.txt`(삭제 반영 시) | `native_capture.h/.cpp` | 스냅샷/클립 캡처가 `appsink` 최신 프레임 기반으로 완전히 대체되고, 호출부/빌드 설정에서 더 이상 참조하지 않을 때 | 캡처/클립 관련 더미 경로 또는 테스트용 저장 분기 제거 |
| Phase 2 인증/장치 | `rest_client.h/.cpp`, `auth_service.h/.cpp`, `device_service.h/.cpp`, `login_screen.cpp`, `screens.h`, `mainwindow.h/.cpp`, `app_state.h/.cpp` | `app_config.json`(생성 시) | `CMakeLists.txt`(신규 서비스 소스 반영 시) | 장치 더미 문자열 흐름, `DummyData::devices()` 사용 경로 | `DeviceService` 결과 기반 `DeviceCheck/AppState/MainWindow` 흐름이 닫히고, 런타임 화면이 더미 장치명 없이 동작할 때 | 장치 목록/로그인 성공 흐름 더미 제거 |
| Phase 3 공통 이벤트 UX | `common_widgets.h/.cpp`, `main_screen.cpp` | `styles/v2_theme.qss`(생성 시) | `common_ui.h/.cpp`, `popup_manager.h/.cpp`(필요 시), `resources.qrc`(테마 리소스 반영 시) | 구 스낵바 이벤트 UI 코드(존재 시) | Topbar 알림센터 UI 셸이 전 Runtime 화면에 반영되고, 스낵바 이벤트가 더 이상 호출되지 않을 때 | 이벤트 배지/알림 UI 더미 제거 |
| Phase 4 이벤트 수신/복구 | `ws_client.h/.cpp`, `event_service.h/.cpp`, `main_screen.cpp`, `common_widgets.h/.cpp` | 없음 | `CMakeLists.txt`(신규 서비스 소스 반영 시) | 이벤트 더미 주입 코드, 스낵바 이벤트 경로 잔여 코드 | WS/REST 기반 이벤트 수신과 복구가 동작하고, UI가 `EventService` 캐시만 구독할 때 | 이벤트 리스트/배지/알림 더미 제거 |
| Phase 5 CCTV 제어 | `cctv_control_service.h/.cpp`, `cctv_screen.cpp` | 없음 | `CMakeLists.txt`(신규 서비스 소스 반영 시) | CCTV 제어 mock/임시 분기(존재 시) | 줌/포커스가 REST 왕복으로 대체되고 임시 버튼 동작이 사라질 때 | CCTV 제어 더미 제거 |
| Phase 6 Playback | `playback_service.h/.cpp`, `playback_screen.cpp`, `stream_player.h/.cpp`(HLS 보강 시) | 없음 | `CMakeLists.txt`(신규 서비스 소스 반영 시) | Playback 더미 타임라인/샘플 데이터 코드 | `/playback/*` API 응답으로 타임라인/HLS 재생이 동작할 때 | Playback 더미 제거 |
| Phase 7 클립 캡처/다운로드 운영 안정화 | `clip_capture_manager.h/.cpp`, Playback export 호출부, 관련 저장/다운로드 코드 | 없음 | 관련 UI 버튼부 | 구 ffmpeg 토스트/임시 에러 처리 코드 | 클립 저장/Playback export 운영 검증이 완료되고, 저장 실패/비활성화 UX가 새 정책으로 대체될 때 | ffmpeg mock/임시 안내 제거 |
| Phase 8 UGV | `ugv_service.h/.cpp`, `ugv_screen.cpp` | 없음 | `ws_client.h/.cpp`(필요 시) | UGV 더미 상태/위치/mock 데이터 코드 | 서버 합의된 래퍼 타입으로 명령/상태/GPS가 E2E 연동될 때 | UGV 더미 제거 |

더미 데이터(`dummy_data.h/.cpp`) 단계별 제거 기준:
- DeviceCheck가 `DeviceService.fetchDevices()`와 실제 서버 응답만으로 동작하면 장치 더미 제거
- 이벤트 목록/배지가 `EventService` REST+WS 캐시만으로 동작하면 이벤트 더미 제거
- Playback 타임라인/스트림이 `PlaybackService` 실제 응답만으로 동작하면 Playback 더미 제거
- UGV 상태/GPS/명령 응답이 실제 WS 연동으로 대체되면 UGV 더미 제거
- 위 항목이 모두 완료되면 `dummy_data.h/.cpp` 전체 삭제 검토

---

## 0. 실행 원칙

1. Phase 게이트 방식으로 진행한다.
2. 각 Phase 종료 시 최소 스모크 테스트를 수행한다.
3. 실패 시 다음 Phase로 넘어가지 않고 해당 Phase에서 수정 후 재검증한다.
4. 서버 합의 미완료 항목(A-2)은 마지막 Phase에서 처리한다.

---

## Phase 1. 렌더링 코어 전환 (최우선)

### 1-A. StreamPlayerV2 전환
- 대상:
  - 핵심 수정: `stream_player.*`, `channel_session_manager.*`
  - 신규 추가: `video_render_widget.*`
  - 연쇄 수정: `CMakeLists.txt`
- 작업:
  - `appsink -> QImage -> QOpenGLWidget` 경로로 렌더링 교체
  - 기존 d3d11/BitBlt 의존 경로 제거 준비
  - 상태 전이 로그(Idle/Connecting/Playing/Error) 기본 추가

### 1-B. 캡처/클립 기본 경로 및 저장 정책
- 대상:
  - 핵심 수정: `clip_capture_manager.*`, 설정 경로 처리 코드
  - 연쇄 수정: `app_state.*`(필요 시), `common_ui.*`(필요 시), `CMakeLists.txt`(삭제 반영 시)
  - 삭제 후보: `native_capture.*`
- 작업:
  - 기본 경로를 Windows 표준 경로로 설정
    - snapshot: `Pictures/snapshot`
    - videoclip: `Videos/videoclip`
  - 폴더 미존재 시 자동 생성
  - 기존 `paths/saveDir` -> `paths/snapshotDir`, `paths/clipDir` 마이그레이션
  - `native_capture.*` 참조 제거 조건 충족 시 삭제
  - 캡처/클립 관련 더미 경로 또는 테스트용 저장 분기 제거

Phase 1 완료 기준:
- 1채널 이상 스트리밍 정상 표시
- 화면 전환 시 렌더링 깨짐/크래시 없음
- 스냅샷/클립 기본 경로 자동 생성 확인
- 스냅샷 저장 정상 (클립 파일 생성 검증은 Phase 7에서 게이트)

---

## Phase 2. 인증/REST 인프라

### 2-A-1. RestClient + AuthService 기반 연결
- 대상:
  - 핵심 수정: `rest_client.*`, `auth_service.*`, `login_screen.cpp`, `screens.h`, `mainwindow.*`, `app_state.*`
  - 신규 추가: `app_config.json`(API base URL 분리 시)
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 작업:
  - `RestClient` 기본 뼈대 추가(`GET/POST`, base URL, 공통 헤더 주입)
  - `AuthService` 기본 인터페이스 추가(login/signup 호출 진입점)
  - 로그인/회원가입 화면을 더미 emit 대신 서비스 호출 구조로 연결
  - 인증 상태(`isAuthenticated`, `accessToken`)를 `AppState`에 추가
  - `MainWindow`에 인증 상태 기반 화면 전환 흐름 연결
  - API 주소는 하드코딩하지 않고 `app_config.json` 또는 동등 설정 파일에서 읽도록 분리
  - `app_config.json` 누락/파싱 실패 시 개발용 기본 URL로 조용히 fallback 하지 않고, 로그인 시도 전에 명확한 설정 오류를 표시
  - 설정 파일 오류 상태에서는 인증 호출을 막고, 설정 파일 위치/형식을 안내하는 UX를 우선 적용
  - 실제 API 응답 파싱은 최소 수준 또는 임시 파서로 유지 가능

2-A-1 완료 기준:
- `RestClient/AuthService/AppState/MainWindow/LoginScreen` 연결 완료
- 로그인 버튼 클릭 시 더미 성공 emit이 아니라 서비스 호출 경로로 진입
- API base URL이 코드 하드코딩이 아니라 외부 설정에서 읽힘
- 실제 로그인 API 스펙 미확정이어도 구조적으로 `2-A-2`를 바로 이어갈 수 있음

### 2-A-2. 로그인 API + 토큰/401 처리
- 대상:
  - 핵심 수정: `rest_client.*`, `auth_service.*`, `login_screen.cpp`, `mainwindow.*`, `app_state.*`
  - 연쇄 수정: `common_ui.*`(진행/실패 UX 보강 시), `CMakeLists.txt`(필요 시)
- 작업:
  - 실제 로그인 endpoint, 요청 body, 응답 파싱 반영
  - `Authorization: Bearer <token>` 공통 적용
  - `RestClient` 응답 경로에서 `httpStatus == 401` 감지 시 `MainWindow::handleUnauthorized()` 실제 연결
  - 인증 후 일반 REST 호출에서 발생한 401은 공통 처리(`handleUnauthorized()`)로 Login 강제 복귀
  - 로그인 endpoint 자체의 401/403 실패는 전역 로그아웃 처리 대신 로그인 실패 UX로 처리
  - 토큰 저장/복구 정책 반영(refresh 미사용, 무효 시 로그인 강제 복귀)
  - 로그인 실패 메시지/진행 상태 UX 정리

2-A-2 완료 기준:
- 로그인 -> DeviceCheck 화면 전환 성공
- 저장된 토큰이 유효하지 않으면 Login으로 강제 복귀
- 인증 실패/401 경로가 더미가 아니라 실제 응답 기준으로 동작

### 2-A-3. 회원가입 API + 화면 UX 정리
- 대상:
  - 핵심 수정: `auth_service.*`, `login_screen.cpp`, `screens.h`, `mainwindow.*`
  - 연쇄 수정: `common_ui.*`, `popup_manager.*`(필요 시)
- 작업:
  - 실제 회원가입 endpoint 및 요청/응답 파싱 반영
  - 회원가입 성공/실패 UX 정리
  - 회원가입 후 로그인 화면 복귀 흐름 고정
  - 회원가입 더미 완료 emit 제거

2-A-3 완료 기준:
- 회원가입 성공/실패가 실제 API 응답 기준으로 동작
- 회원가입 후 Login 복귀 UX가 정리됨
- Login/Signup 화면 더미 완료 흐름이 제거됨
- 주의: `2-A-1`에서 회원가입 서비스 진입선(호출 구조)은 먼저 연결될 수 있으며, API 스키마 확정/검증은 `2-A-3`에서 최종 마감

### 2-B-1. DeviceService + 장치 응답 모델
- 대상:
  - 핵심 수정: `device_service.*`
  - 연쇄 수정: `app_config.json`(경로/엔드포인트 조정 시), `mainwindow.*`(서비스 초기화 연결 시)
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 작업:
  - 우선 `/devices` 응답만 연동하고, 여기서 `type/model/name` 메타데이터가 충분한지 확인
  - `/device/{id}/channels`, `/channel/{id}`는 `/devices` 응답만으로 부족할 때만 후속 추가
  - 응답을 `DeviceSummary`, `ChannelSummary` 또는 동등 모델로 정리
  - 서버 응답 스키마를 UI와 분리해서 파싱/검증

2-B-1 완료 기준:
- `/devices` 응답을 코드에서 안정적으로 읽고 모델로 변환
- 서버 응답 스키마 변경이 `DeviceCheck` UI까지 바로 번지지 않도록 서비스 계층이 분리됨

### 2-B-2. DeviceCheckScreen 실제 데이터 UI 전환
- 대상:
  - 핵심 수정: `login_screen.cpp`, `screens.h`
  - 연쇄 수정: `device_service.*`, `common_ui.*`(로딩/오류 표시 보강 시)
- 작업:
  - `DummyData::devices()` 기반 체크박스 목록 제거
  - `type -> model -> name` 트리 UI로 전환
  - 표시 필드: `type/model/name/online-health`, `ip` 비표시
  - 선택 결과는 `QStringList` 대신 최소 컨텍스트(`deviceId/channelId/displayName/deviceType`) 기반으로 관리
  - 필요 시 `SelectedChannelContext` 같은 최소 struct 도입

2-B-2 완료 기준:
- 더미 체크박스 없이 실제 서버 데이터가 트리에 표시됨
- 선택/비선택 상태가 실제 장치/채널 컨텍스트 기준으로 동작
- 제한사항(임시): `MainWindow` 진입 연결은 `displayName` 호환 경로를 잠시 사용하며, 동명이인/동명 채널 충돌 해소는 `2-B-3`에서 완료

### 2-B-3. AppState/MainWindow 연결 + 더미 제거
- 대상:
  - 핵심 수정: `app_state.h/.cpp`, `mainwindow.h/.cpp`, `screens.h`
  - 연쇄 수정: `login_screen.cpp`, `dummy_data.h/.cpp`(장치 더미 제거 시)
- 작업:
  - 선택 장치/채널 컨텍스트를 `AppState`에 저장하고 Main 진입 시 반영
  - `MainWindow` 런타임 재생성 로직을 더미 장치명 문자열이 아니라 실제 선택 컨텍스트 기준으로 전환
  - `2-B-2` 임시 호환 로직(`SelectedChannelContext -> displayName` 평탄화)을 제거하고, 컨텍스트 원형(deviceId/channelId/deviceType)을 그대로 전달
  - `configuredDeviceNames()`, `applyDeviceChangesToRuntimeState()`, `pruneStateDeviceSelection()` 등 더미 장치명 보정 흐름 제거 또는 축소
  - `DummyData::devices()` 의존 경로 제거

2-B-3 완료 기준:
- 서버 장치 선택 -> Main 반영까지 실제 컨텍스트 기준으로 이어짐
- 장치 선택/재구성 흐름에서 더미 장치명 문자열 의존이 제거됨

Phase 2 완료 기준:
- 로그인 -> DeviceCheck -> Main 전환 성공
- 토큰 미유효 시 로그인 강제 복귀 동작

---

## Phase 3. 공통 이벤트 UX + 메인 레이아웃

### 3-A-1. Topbar / 알림센터 셸 공통화
- 대상:
  - 핵심 수정: `common_widgets.*`
  - 연쇄 수정: `common_ui.*`, `popup_manager.*`(필요 시)
  - 삭제 후보: 구 스낵바 이벤트 UI 코드(존재 시)
- 작업:
  - `TopbarWidget`에 알림 버튼, 배지, 공통 상태 슬롯 추가
  - 알림센터 UI 셸 추가(이 단계는 UI 셸/토글만, 실제 데이터 연결은 Phase 4)
  - 알림 버튼 Topbar 진입점 공통화
  - 셸 검증을 위해 임시 연결이 필요하면 기존 검색 모달 재사용은 가능하되, 최종 방향은 전용 모달 기준으로 유지
  - 이벤트 배지/알림 UI 더미 제거

### 3-A-2. Runtime 화면 연결 정리
- 대상:
  - 핵심 수정: `main_screen.*`, `cctv_screen.*`, `ugv_screen.*`, `playback_screen.*`
  - 연쇄 수정: `common_widgets.*`
- 작업:
  - 각 화면의 `TopbarWidget` signal 연결을 공통 규칙으로 정리
  - 요구 구체화 반영: 알림 버튼 클릭 시 `알림센터 전용 모달` 연결(이벤트 검색 모달과 분리)
  - 전용 모달 필터: `최근 12시간 / 1일 / 3일`
  - 전용 모달 목록 정렬: 최신순
  - 배지 정책 정리: 이벤트뷰가 보이지 않는 상태에서 신규 이벤트 발생 시 배지 ON
  - 배지 해제 조건 정리: 사용자가 알림센터를 열어 확인하면 배지 OFF
  - 현재 저장 피드백은 각 화면 왼쪽 사이드바의 액션 버튼 상단 로컬 상태 라벨로 유지
  - 로컬 상태 라벨은 `Main/CCTV/UGV/Playback` 화면별 문맥 피드백 용도로 사용
  - 화면 전환 중에도 저장 상태 유지가 필요하면 Topbar 또는 공통 status 영역으로 전역 승격
  - 전역 승격 시 기존 로컬 상태 라벨은 즉시 제거하지 않고, 로컬 액션 확인용 보조 피드백으로 유지 여부 결정

### 3-A-3-1. 공통 스타일 / QSS 정리
- 대상:
  - 핵심 수정: `styles/v2_theme.qss`(생성 시)
  - 연쇄 수정: `resources.qrc`(테마 리소스 반영 시), `common_widgets.*`, `common_ui.*`, `login_screen.cpp`
- 작업:
  - 공통 UX 안정화 시점에 `v2_theme.qss` 생성 및 적용 시작
  - 로그인/회원가입/DeviceCheck 상태 라벨과 액션 상태 라벨의 인라인 스타일을 `v2_theme.qss`로 이동
  - `openNotificationCenterDialog()` 인라인 스타일을 `v2_theme.qss`로 이동
  - Topbar/알림센터/공통 상태 라벨 스타일을 토큰 기준으로 정리
  - `common_ui.cpp`의 알림센터/이벤트검색/캡처 유틸 분리는 선택적 후속 정리로 관리
  - 기능 변경보다 스타일 수렴 중심으로 처리

### 3-A-3-2. 화면별 OSD / 인라인 스타일 정리
- 대상:
  - 핵심 수정: `main_screen.*`, `cctv_screen.*`, `ugv_screen.*`, `playback_screen.*`
  - 연쇄 수정: `styles/v2_theme.qss`, `common_widgets.*`
- 작업:
  - 멀티뷰 셀 OSD(채널명/상태/선택 테두리) 인라인 스타일을 QSS/속성 기반으로 이동
  - CCTV/UGV fullscreen OSD 라벨 색상/폰트 인라인 스타일을 QSS로 이동
  - UGV control/dpad/missionEnd 등 화면 전용 인라인 스타일을 QSS로 이동
  - 기능 변경보다 스타일 수렴 중심으로 처리

### 3-B. MainScreen 이벤트뷰 토글
- 대상:
  - 핵심 수정: `main_screen.*`
  - 연쇄 수정: `common_widgets.*`
- 작업:
  - Main/CCTV/Playback/UGV 공통 레이아웃 골격과 디자인 토큰 기준 확정
  - 기본: `트리 | 스트리밍 | 이벤트뷰`
  - 토글 OFF: `트리 | 스트리밍` (트리 폭 유지, 스트리밍 확장)
  - 이벤트뷰 토글 버튼은 Main 전용 배치
  - CCTV 이벤트뷰 제거 정책 적용

Phase 3 완료 기준:
- `3-A-1`: 모든 Runtime 화면에서 공통 Topbar / 알림센터 UI 셸 접근 가능
- `3-A-2`: Main 이벤트뷰 토글 시 레이아웃 정상 동작, Runtime 화면별 Topbar signal 연결 정상, 중복 signal 연결 없음, 알림센터 전용 모달(12시간/1일/3일·최신순) 접근 가능, 배지 ON/OFF 규칙이 문서/구현 기준으로 일치
- `3-A-3-1`: Topbar/알림센터/상태 라벨 인라인 스타일이 `v2_theme.qss` 기준으로 정리됨
- `3-A-3-2`: Main/CCTV/UGV/Playback 화면별 OSD/인라인 스타일이 `v2_theme.qss` 기준으로 정리됨
  - 기본: 사이드바 로컬 상태 라벨
  - 확장: 화면 전환 유지가 필요할 때 Topbar/공통 status 전역 표시로 승격
- 주의: 실제 이벤트 데이터 연결/배지 갱신 완료는 Phase 4 완료 기준에서 검증
- Known issue: `Main` 이벤트뷰 hidden 상태에서 unread 배지 자동 점등은 더미 이벤트 임시 로직 기준으로 동작이 불안정할 수 있으며, 최종 unread lifecycle은 Phase 4 `EventService` 연동 시 재검증/보완
- Known issue: `applyNotificationUnreadState()`는 3-A-2 임시 정책으로 `unreadCount`를 표시하지 않고 global status를 clear 처리하며, unread count 기반 상태 메시지는 Phase 4 이벤트 연동 시 최종 정책으로 재정의

---

## Phase 3.5. Phase 4 진입 전 최소 구조 정리

이 단계는 리팩토링 전용 Phase가 아니라, `Phase 4`에서 `WsClient`, `EventService`, 재연결/복구 로직이 `MainWindow`와 공용 유틸 파일에 과도하게 누적되기 전에 최소한의 구조 정리를 수행하는 스프린트다.

범위는 아래로 제한한다.

- `MainWindow`의 과도한 비대화 완화
- 설정 다이얼로그와 config/theme loader 분리
- 런타임 화면 재생성 중복 통합
- `common_ui.cpp`는 전체 분리 대신 이벤트/알림 관련 유틸만 분리 시작

다음 항목은 이 단계에서 하지 않는다.

- `AppState` 재설계
- `StreamPlayer`/렌더링 구조 재작성
- UGV/Playback 도메인 정책 재설계
- 대규모 서비스/매니저 계층 재편

### 3.5-A. SettingsDialog 분리
- 대상:
  - 핵심 수정: `mainwindow.*`
  - 신규 추가: `settings_dialog.*`
- 작업:
  - `openSettingsDialog()` 내부 UI/탭 구성 코드를 `SettingsDialog` 클래스로 이동
  - `MainWindow`는 열기/결과 반영만 담당하도록 축소
  - 장치 관리 탭은 제거하지 않더라도, 서버 장치 조회 이후에도 유지가 필요한지 역할을 다시 확인

### 3.5-B. Config / Theme Loader 분리
- 대상:
  - 핵심 수정: `mainwindow.*`
  - 신규 추가: `app_config_loader.*` 또는 동등 역할 유틸 파일
- 작업:
  - `loadAppConfig()`를 별도 유틸/로더로 이동
  - `loadThemeFromRelativePaths()` / `loadMergedThemeFromRelativePaths()`를 별도 유틸/로더로 이동
  - `MainWindow`는 결과만 받아서 적용

### 3.5-C. 런타임 화면 재생성 중복 통합
- 대상:
  - 핵심 수정: `mainwindow.*`
- 작업:
  - runtime screen 제거/생성/insert/connect 패턴을 공통 함수로 통합
  - `finalize` 계열 람다와 `rebuildRuntimeScreens()` 중복 제거

### 3.5-D. `common_ui.cpp` 이벤트/알림 묶음 분리 시작
- 대상:
  - 핵심 수정: `common_ui.*`
  - 신규 추가: `event_ui_helpers.*` 또는 동등 역할 파일
- 작업:
  - 알림센터/이벤트 검색/이벤트 다이얼로그 관련 유틸만 먼저 분리
  - 캡처/저장 경로 유틸은 이 단계에서 유지
  - 전체 파일 분리가 아니라, `Phase 4` 진입 전 최소 분리만 수행

Phase 3.5 완료 기준:
- `mainwindow.cpp`에서 설정 다이얼로그 대형 UI 블록이 제거됨
- app config/theme loader가 `MainWindow` 바깥으로 이동함
- 런타임 화면 재생성 중복이 공통 함수로 통합됨
- 이벤트/알림 관련 유틸이 `common_ui.cpp`에서 최소 1개 별도 파일로 분리됨
- 위 정리 후에도 `Phase 1~3` 기능 회귀 없음

---

## Phase 4. 이벤트 수신/복구

### 4-A. WsClient 뼈대
- 대상:
  - 핵심 수정: `ws_client.*`
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 작업:
  - WS 헤더 인증(`Authorization: Bearer <token>`)
  - 재연결(backoff) + heartbeat(30s ping / 60s timeout) 초기값 적용
  - raw text/json 메시지 수신까지만 구현하고, `msgId`/ack/nack 의미 해석은 `4-B EventService`로 이관

### 4-B-1. EventService 코어 추가
- 대상:
  - 핵심 수정: `event_service.*`
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 작업:
  - 초기 API는 `ingestWsMessage(QJsonObject)`, `reset()`, `events()`, `unreadCount()`, `markAllRead()` 수준으로 시작하고 필요한 REST 복구 메서드만 최소 추가
  - `WsClient` raw JSON 수신을 해석하는 `ingestWsMessage(QJsonObject)` 추가
  - 내부 캐시 `QVector<EventInfo>` + `unreadCount` + `lastEventId` 소유
  - 신호: `eventsUpdated()`, `unreadChanged(int)`, `serviceError(QString)`
  - 중복 제거 키는 `eventId` 우선, 없으면 `timestamp + channel + type` 임시 키 사용
  - WS 재연결 후 복구는 `lastEventId` 미지원 시 REST `/events` 최근 N건 fallback
- 완료 기준:
  - `EventService` 단독으로 WS raw JSON -> 이벤트 캐시/unread 갱신이 가능함
  - 로그아웃/401 시 `EventService` 캐시와 unread가 즉시 초기화되는 reset 경로가 준비됨

### 4-B-2. MainWindow 오케스트레이션 연결
- 대상:
  - 핵심 수정: `mainwindow.*`
  - 연쇄 수정: `CMakeLists.txt`
- 작업:
  - `m_eventService` 생성/소멸 관리
  - `WsClient::jsonMessageReceived` -> `EventService::ingestWsMessage` 연결
  - 로그인 성공 시 `EventService` 시작(초기 REST pull + WS 수신 준비)
  - 로그아웃/401 시 `EventService` 캐시/unread 초기화 + `WsClient` disconnect
  - 다음 기능 추가 시 인증/session, 장치 선택, settings 로직을 `MainWindow`에 더 모으지 않기
  - 새 로직은 보조 클래스/헬퍼 함수로 분리 우선

### 4-B-3. UI 데이터 소스 전환
- 대상:
  - 핵심 수정: `main_screen.*`, `event_ui_helpers.*`
  - 후속 수정: `common_ui.*`, `common_widgets.*`, 필요 시 `cctv_screen.*`, `ugv_screen.*`, `playback_screen.*`
  - 삭제 후보: 이벤트 더미 주입 코드, 스낵바 이벤트 경로 잔여 코드
- 작업:
  - `Main`의 EventView/Topbar unread를 `EventService` 기준으로 먼저 전환
  - 알림센터/이벤트검색/상세모달의 목록 소스를 `DummyData::events()` -> `EventService` 캐시로 전환
  - `/events`는 이벤트뷰/알림센터/검색 목록의 기본 데이터 소스로 사용하고, 서버 반영 후 `channelId` 기준 채널명 매핑까지 목록 단계에서 처리
  - `/event/{eventId}`는 상세 모달 전용 보강 호출로 사용하고, `summary`/`bestshotId` 및 후속 `channelId` 필드를 상세 화면에 반영
  - 컴포넌트 소유자 확정: `EventService`가 알림 목록 메모리 캐시를 소유하고 UI는 시그널 구독만 수행
  - `common_ui.h/.cpp`의 이벤트/알림 forwarding wrapper는 이 단계에서 호출부를 `EventUiHelpers::` 또는 후속 서비스/헬퍼 직접 호출로 전환하면서 제거/최소화
  - `CCTV/UGV/Playback`의 배지/알림센터 표시도 `EventService::unreadChanged` 기준으로 통일
  - 스낵바 이벤트 알림 완전 제거
  - 이벤트 리스트/배지/알림 더미 제거
  - `common_ui.cpp`는 채널/선택, 이벤트 다이얼로그/검색, 캡처/저장 경로 유틸 단위로 단계적 분리 고려
  - UI 데이터 소스 전환 우선순위는 `Main -> event_ui_helpers(알림센터/검색/상세) -> CCTV/UGV/Playback`

Phase 4 완료 기준:
- `DummyData::events()`를 이벤트 UI 경로에서 더 이상 사용하지 않음
- 이벤트 실시간 수신 + 알림센터 반영 + 배지 갱신
- WS 재연결 후 이벤트 목록 복구 동작 확인(REST fallback 포함)
- `Main/CCTV/UGV/Playback` 알림 배지 동작이 일관되고, 로그아웃/401 시 이벤트 캐시/unread가 즉시 초기화됨

---

## Phase 4.5. 성능 안정화

이 단계는 전면 최적화 Phase가 아니라, `Phase 5~6` 진입 전에 앱이 계속 사용 가능한 수준을 유지하도록 하는 최소 성능 안정화 스프린트다.

핵심 목표는 아래 두 가지로 제한한다.

- 화면 전환 시 비활성 화면의 스트리밍 파이프라인을 계속 돌리지 않도록 suspend/resume 정책 도입
- 이벤트 모델이 `dummy_data.h`에 묶여 있는 임시 의존을 최소한으로 정리

다음 항목은 이 단계에서 하지 않는다.

- `VideoRenderWidget` GPU 경로 전면 재작성
- `StreamPlayer` 구조 재설계
- 멀티뷰 품질 프로파일 재튜닝 Phase 수행
- `common_ui`/`event_ui_helpers` 추가 리팩토링
- 대규모 threading 재구성

### 4.5-A. 비활성 화면 파이프라인 suspend/resume
- 대상:
  - 핵심 수정: `channel_session_manager.*`, `mainwindow.*`
  - 후속 수정: 필요 시 `main_screen.*`, `cctv_screen.*`, `ugv_screen.*`, `playback_screen.*`
- 작업:
  - `showScreen()` 기준으로 현재 화면의 채널만 active, 이전 화면 채널은 `PAUSED` 정책 적용
  - `Main -> CCTV/UGV/Playback` 전환 시 Main 멀티뷰 파이프라인 일괄 suspend
  - `CCTV/UGV/Playback -> Main` 복귀 시 Main 채널 resume
  - `ChannelSessionManager`에 `suspendAllExcept(...)`, `resume...` 또는 동등 역할 API 추가
  - resume 직후 첫 프레임까지의 짧은 검은 화면은 허용하되, 장시간 멈춤/재연결 루프는 금지

### 4.5-B. `EventInfo` 분리
- 대상:
  - 핵심 수정: `dummy_data.h`, `event_service.*`, `event_ui_helpers.*`
  - 신규 추가: `event_types.h` 또는 동등 역할 공용 타입 파일
- 작업:
  - `EventInfo`를 `dummy_data.h`에서 분리해 공용 타입으로 이동
  - `EventService`가 더미 헤더를 직접 include 하지 않도록 정리
  - 이벤트 UI 경로와 더미 데이터 경로의 타입 의존만 최소한으로 끊고, 동작 자체는 변경하지 않음

Phase 4.5 완료 기준:
- 화면 전환 시 비활성 화면 파이프라인이 suspend/resume 정책대로 동작함
- `Main -> CCTV/UGV/Playback` 전환 시 체감 CPU/프리징이 줄어듦
- `EventInfo`가 `dummy_data.h` 밖 공용 타입으로 분리됨
- 위 변경 후 `Phase 1~4` 기능 회귀 없음

---

## Phase 5. CCTV 제어

후속 적용:
- `5-A` 초반에 이벤트 시간 파싱을 ISO8601, 공백 구분 timestamp, timezone offset까지 허용하는 다중 포맷 기준으로 고정한다.
- `5-A` 초반에 `/event/{eventId}` 상세 조회를 UI 헬퍼 직접 호출이 아니라 `EventService` 경유로 통일한다.
- `5-A` 초반에 `eventId` 없는 예외 이벤트의 dedupe fallback 키(`timestamp|channel|type`)를 보강한다.
- `5-A` 후순위 정리로 로그인 직후 `/events` 복구와 `WsClient connected` 시점 복구의 중복 호출을 1회로 줄인다.

### 5-A. CctvControlService
- 대상:
  - 핵심 수정: `cctv_control_service.*`
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 엔드포인트(확정):
  - `POST /channel/{channelId}/zoom` `{ "value": ... }`
  - `POST /channel/{channelId}/focus` `{ "value": ... }`

### 5-B. CctvScreen 연동
- 대상:
  - 핵심 수정: `cctv_screen.*`
  - 삭제 후보: CCTV 제어 mock/임시 분기(존재 시)
- 작업:
  - 줌/포커스 버튼을 REST 호출로 연결
  - 실패 시 사용자 에러 표시
  - `focus/reset`은 후속 확장 후보로 보류
  - CCTV 제어 더미 제거

Phase 5 완료 기준:
- CCTV 화면에서 줌/포커스 제어 왕복 성공

---

## Phase 6. Playback

### 6-A. PlaybackService
- 대상:
  - 핵심 수정: `playback_service.*`
  - 연쇄 수정: `CMakeLists.txt`(신규 서비스 소스 반영 시)
- 작업:
  - 서버 Playback API(`/playback/dates/{date}/channels`, `/playback/timeline`, `/playback/stream`) 전용 서비스 계층 추가
  - `PlaybackService`는 mediaMTX를 직접 호출하지 않고, 서버 응답의 `protocol`, `uri`, `ts`만 사용
  - `playback/stream` 응답의 상대 `uri`는 `apiBaseUrl` 기준 절대 URL로 보정하는 helper 추가
  - CSV 기준 선개발, 서버 구현 완료 후 필드 재검증
  - `PlaybackChannelSummary`, `PlaybackTimelineResult`, `PlaybackMarker`, `PlaybackStreamResult` 등 DTO를 서버 응답 기준으로 정리
  - `PlaybackService` 결과 모델에 `channelId`를 기본 포함해 문자열 채널명 브리지 의존을 줄이기
  - `app_config.json` / `app_config_loader.*`에 Playback 경로(`channelsByDatePathTemplate`, `timelinePath`, `streamPath`) 추가
  - 현재 `channelId = -1` 호환 경로를 실제 API 값으로 전환
  - 런타임 고유 라벨이 필요할 때 `deviceId + channelId` 기준으로 안정화
  - `activeChannelsForScreen()` 등 런타임 활성 채널 판정의 문자열 채널명 의존을 `channelId` 중심으로 전환
  - `rtspUrlForChannel(name)` 의존을 점진 제거하고 `SelectedChannelContext` 또는 별도 채널 레지스트리 기준 조회로 이동
  - `6-A`에서는 `PlaybackScreen` UI 전면 전환보다 서비스/설정/주입 구조를 먼저 고정하고, 실제 재생 전환은 `6-B`에서 처리

### 6-B. PlaybackScreen
- 대상:
  - 핵심 수정: `playback_screen.*`
  - 연쇄 수정: `stream_player.*`, `common_ui.*`(Playback source 표시 보강 시)
  - 삭제 후보: Playback 더미 타임라인/샘플 데이터 코드
- 작업:
  - 타임라인/이벤트 마커 표출
  - 로컬 파일 기반 재생에서 서버 Playback URL 기반 재생으로 전환
  - `StreamPlayer`가 `http://` / `https://` playback URL을 `uridecodebin` 경로로 재생하도록 보강
  - 배속/seek는 서버 Playback 응답 포맷이 seek 가능한 VOD일 때만 지원, 미지원 시 UX fallback 정의
  - Export는 서버 `/playback/export` job + direct download 기준으로 전환하고, 저장 경로/다운로드 UX는 Phase 7에서 운영 검증
  - 스낵바 이벤트 알림 제거, 알림센터 통합 사용
  - Playback 더미 제거

Phase 6 완료 기준:
- 날짜/채널 선택 -> 타임라인 조회 -> 서버 Playback URL 재생 성공
- seek/배속 가능 여부가 실제 응답 포맷 기준으로 확인되고, 미지원 시 UX fallback이 정의됨
- Export가 서버 `/playback/export` 응답 URL 또는 확정된 다운로드 경로 기준으로 동작 확인

---

## Phase 7. 클립 캡처/다운로드 운영 안정화

### 7-A. 클립 캡처 운영 정리
- 대상:
  - 핵심 수정: `clip_capture_manager.*`
  - 연쇄 수정: 관련 UI 버튼부, 저장 경로 처리 코드
- 작업:
  - 클립 저장 성공/실패 콜백 정리
  - 인코딩/저장 중 재진입 방지 재확인
  - 저장 경로 유효성/쓰기 가능 여부 검증
  - 실패 시 상태 라벨/팝업/비활성화 UX 정리
  - ffmpeg 의존이 실제 남아 있다면 이 경로에서만 진단/안내 유지
  - ffmpeg 의존이 제거된 경우 관련 mock/임시 메시지 제거

### 7-B. Playback export 운영 검증
- 대상:
  - 핵심 수정: `playback_screen.*`, `playback_service.*`
  - 연쇄 수정: 서버 `/playback/export*` 응답 계약, 관련 다운로드 처리 코드
- 작업:
  - `7-B-1 Polling 안정화`
    - `pollExportStatus()` 중복 호출 방지 플래그 정리
    - 이전 요청 응답 역전 방지를 위한 generation/token 가드 추가
    - `QUEUED/PROCESSING/DONE/FAILED` 상태 분기 고정
    - timeout / 상태 초기화 경로 통일
  - `7-B-2 다운로드 안정화`
    - `DONE` 시 direct download 저장 경로 동작 확인
    - 부분 파일 방지를 위한 원자적 저장 경로(`QSaveFile`) 적용
    - 다운로드 진행 중 재요청 차단/안내
    - invalid uri / network fail / partial file 정리
  - `7-B-3 UX 마감`
    - export 진행 중 버튼 비활성화/완료 시 복구
    - 저장 경로 유효성 실패 시 파일 다이얼로그 fallback 전 안내 명확화
    - 로그아웃/종료 시 export polling 중단, download abort 정책 통일
    - 브라우저 openUrl 방식이 아닌 앱 직접 다운로드 정책 기준 유지
  - `7-B-4 검증 체크`
    - 상태: `Blocked (서버 export 코드 배포 대기)`
    - `jobId -> DONE -> 다운로드 저장` 연속 성공 확인
    - `FAILED`, timeout, invalid uri UX 확인
    - 저장 경로 권한 없음/사용자 취소 동작 확인
    - 서버 `/playback/export*` 최신 코드 반영 후 통합 검증 진행

Phase 7 완료 기준:
- 클립 저장이 운영 환경에서 성공/실패 UX까지 포함해 안정 동작
- Playback export가 `jobId -> DONE -> 다운로드 저장`까지 확인
- 저장 경로 문제/미지원 환경에서 사용자 안내가 명확함
- ffmpeg가 실제 남아 있는 클립 캡처 경로가 있으면 정상/미설치 두 경로 모두 UX 확인, 없으면 관련 임시 처리 제거

현재 상태 메모:
- `7-A` 완료
- `7-B-1 ~ 7-B-3` 완료
- `7-B-4`는 서버 export 코드 배포 전이라 검증 `Blocked`

---

## Phase 8. UGV (마지막)

### 8-A. UgvGatewayService
- 대상:
  - 핵심 수정: `ugv_service.*`
  - 연쇄 수정: `ws_client.*`, `app_config_loader.*`, `app_config.json`(필요 시)
- 역할:
  - 클라이언트는 `/gw/ws` + `Authorization Bearer` + `vms.gw.v1`만 사용
  - config 키는 `ugv.gatewayWsUrl`, `ugv.gatewayWsSubprotocol(vms.gw.v1)`로 고정
  - `request.conn.ugv`, `request.disconn.ugv`, `cmd.drive`, `cmd.ptz` 송신
  - `telemetry.gps`, `telemetry.rssi`, 각종 `*.ack` 수신/파싱
  - `msgId` 기준 pending command / ack timeout / telemetry ack 처리
  - `telemetry.gps`, `telemetry.rssi` 수신 시 서비스 계층에서 자동 ACK 송신
- 정책:
  - `UGV 화면 진입`과 `출동(request.conn.ugv)`은 분리
  - 출동 성공 기준은 `WebSocket connected`가 아니라 `request.conn.ugv.ack`
  - `drive`는 press / release 모두 명령으로 분리
  - auto reconnect는 UGV 정책과 충돌하지 않도록 기본 비활성 또는 제어 가능 옵션으로 정리
  - ACK timeout 기본값:
    - `request.conn.ugv`, `request.disconn.ugv`: 5s
    - `cmd.drive`, `cmd.ptz`: 1~2s

### 8-B. UgvScreen 실제 연동
- 대상:
  - 핵심 수정: `ugv_screen.*`
  - 연쇄 수정: `screens.h`, `mainwindow.cpp`, `common_ui.*`, `app_config_loader.*`, `app_config.json`(맵 범위 설정 시)
  - 삭제 후보: UGV 더미 상태/위치/mock 데이터 코드
- 작업:
  - `UgvGatewayService`를 `UgvScreen`에 주입
  - 연결 전에는 drive/PTZ disabled, 상태 라벨 `연결 안됨`
  - 명시 액션(예: 출동/임무 시작)에서만 `request.conn.ugv`
  - `QGraphicsScene/QGraphicsView` 기반 현재 맵 구조를 유지하고 최소 GPS 시각화부터 적용
  - `telemetry.gps`를 지도 marker / heading / speed 표시에 반영하고 최근 경로 polyline 누적
  - 좌표 변환은 설정값 기반(`ugv.mapBounds` 또는 동등 키)으로 고정해 `lat/lon -> scene(x,y)` helper로 처리
  - `telemetry.rssi`를 OSD overlay에 반영
  - drive/PTZ 버튼과 slider를 실제 `cmd.drive`, `cmd.ptz`로 연결
  - 화면 이탈/로그아웃/종료 시 `request.disconn.ugv` 및 WS 정리
- 상태:
  - 구현 완료
  - 성공 경로 E2E 검증은 `Blocked (gateway /ugv/ws 미구현 또는 미연결)` 상태

### 8-C. 식별자/컨텍스트 정책 고정
- 대상:
  - 핵심 수정: `ugv_service.*`, `ugv_screen.*`
  - 연쇄 수정: `app_state.h`, `common_ui.*`, `mainwindow.cpp`
- 작업:
  - `displayName` 브리지 대신 `deviceId(gatewayId)` + `channelId(ugvId)` direct path 사용
  - `selectedChannelContexts` 원본 기준으로 gateway/channel 식별자 전달
  - `UGV 화면 선택`, `Playback 연계`, `activeChannelsForScreen()`과의 정합성 유지

### 8-D. 운영 안정화
- 상태: `8-A Closed / 8-B 구현 완료 (success-path verification Blocked) / 8-C Closed / 8-D In Progress`
- `8-D-1. command ACK 검증 강화`
  - `cmd.drive.ack`, `cmd.ptz.ack`도 `msgId` 기준 pending 매칭과 `gatewayId/ugvId` 대상 검증을 수행
  - timeout 기준은 `conn/disconn 5s`, `drive/ptz 1~2s`로 시작하고 운영 로그 기준 보정
  - 유효 ACK만 `commandAck`로 전달하고, mismatch/timeout은 `serviceError`로 처리
- `8-D-2. main_screen UGV 진입 direct path 마감`
  - `main_screen` 일부 UGV 진입 경로에 남아 있는 이름 기반 해석 제거
  - 트리/셀/출동 진입에서 `channelId/deviceId` direct path 우선 사용
  - direct 식별자가 없으면 UGV 진입 차단 + 안내
- `8-D-3. 화면 전환/종료 해제 정책 정리`
  - 연결 실패 / 장치 오프라인 / gateway bridge 실패 UX 정리
  - 재연결/중복 명령/화면 전환 중 해제 정책 정리
  - `mission 종료`, `hideEvent`, `logout`, `close` 경로에서 disconnect/shutdown 정책을 1회성으로 고정
- `8-D-4. UgvScreen 책임 분리`
  - 외부 지도 엔진(API) 도입 여부 최종 결정(필요 시 고도화), 미도입 시 현재 Qt 맵 유지
  - 외부 지도 엔진 도입 여부는 Phase 8 완료의 필수 조건이 아님
  - `ugv_screen.cpp` 비대화 완화: telemetry/map/command 핸들러를 분리(`ugv_screen_telemetry.*`, `ugv_screen_map.*`, `ugv_screen_commands.*` 또는 동등 구조)
  - UGV 더미 제거

Phase 8 완료 기준:
- `request.conn.ugv -> ack` 성공
- `cmd.drive`, `cmd.ptz` 왕복 ack 확인
- GPS/RSSI 실시간 수신 + UI 반영
- 화면 전환/로그아웃/종료 시 정상 해제(`request.disconn.ugv`)
- 재연결/오류 경로에서 크래시/무한루프 없음

---

## 게이트 테스트 (Phase별)

공통 최소(모든 Phase):
1. 앱 실행/화면 전환 중 크래시 없음
2. 의도하지 않은 에러 팝업 없음

Phase 추가 항목:
- Phase 1: RTSP 1채널 재생 시작/정지, 스냅샷 저장
- Phase 2: 로그인 -> DeviceCheck -> Main 전환, 401 시 Login 강제 복귀
- Phase 3: Topbar/알림센터 UI 셸 접근, Main 이벤트뷰 토글 정상, 멀티뷰 `4/6/9` 전환/진입/셀 선택 시 프리징/강종 없음
- Phase 3: Main/CCTV/Playback/UGV 공통 레이아웃 골격과 디자인 토큰 적용 확인
- Phase 3.5: Phase 1~3 스모크 테스트 전체 재통과 (기능 회귀 없음 확인)
- Phase 4: 이벤트 실시간 수신, 배지 갱신, 재연결 후 REST `/events` 복구
- Phase 4.5: Main -> CCTV/UGV/Playback 전환 시 체감 프리징 감소 확인, Phase 1~4 스모크 테스트 재통과
- Phase 5: CCTV 줌/포커스 제어 왕복
- Phase 6: Playback 진입, 타임라인 조회, 서버 Playback URL 재생, seek/배속/Export 정책 확인
- Phase 7: 클립 캡처 운영 검증 + Playback export polling/download 저장 경로 확인 + ffmpeg 잔존 경로가 있으면 그 경로만 진단/비활성화 UX 확인
- Phase 8: `/gw/ws` 기반 UGV 연결/해제, drive/PTZ ack, GPS/RSSI 반영 E2E

---

## 현재 착수 가능 여부

- 즉시 착수 가능: Phase 1~5
- 조건부 진행: Phase 6 (서버 Playback/API 상태 확인 후), Phase 7 (`7-A` 완료, `7-B-4`는 서버 export 코드 배포 후 검증 진행)
- 현재 진행: Phase 8 (`8-D-1` 진행 가능, `8-B` success-path verification은 gateway `/ugv/ws` 준비 전까지 Blocked)

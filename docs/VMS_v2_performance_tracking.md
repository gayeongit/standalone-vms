# VMS v2 Performance Tracking

기준:
- 관련 계획 문서: `VMS_v2_post_phase8_execution_plan.md`
- 관련 구현 이력 문서: `VMS_v2_implementation_history.md`

문서 목적:
- 성능 이슈를 `문제 -> 원인 -> Before 측정 -> 패치 -> After 측정` 흐름으로 기록
- 발표/공유 시 바로 사용할 수 있는 before/after 비교 자료 축적
- 기능 구현 문서와 분리해서 성능 개선만 별도 관리

측정 방식 고정:
- 인앱 계측: `clip_capture_manager.cpp`, `stream_player.cpp`, `video_render_widget.cpp`, `mainwindow_navigation.cpp`
- 외부 측정: Windows `PerfMon` 또는 동등 도구

빠른 측정 순서:
1. 앱 실행 후 PerfMon 수집 시작
2. 로그인 -> 메인 진입
3. 메인 멀티뷰 30초 유지
4. `Main -> CCTV -> Main` 전환 3회 반복
5. 필요 시 `Main -> CCTV -> UGV -> Playback -> Login` 전환 3회 반복
6. 동일 조건으로 10초 클립 저장 3회
7. 필요 시 동일 조건으로 15초 클립 저장 3회
8. PerfMon 수집 중지
9. 인앱 로그(`clip_total_ms`, `render_fps`, `screen_transition_ms`)와 PerfMon 수치를 `Before / After` 표에 기록

PerfMon 카운터:
- `Process -> % Processor Time`
- `Process -> Working Set - Private`
- `Process -> Private Bytes`
- `Process -> ID Process`

PerfMon 측정 메모:
- 기본 샘플 간격은 1초
- 시나리오당 30초 유지 후 평균 기록
- 인스턴스 목록에 `VMS_v2`가 바로 안 보이면 `All instances`로 수집해도 무방
- 이 경우 분석 시 현재 실행 파일 기준 카운터(`Process(VMS_v2)`)를 사용
- 참고: `10-P1-9` 이전에 측정한 과거 baseline은 `Process(VMS_v1)` 표기를 유지한다

---

## 1. 측정 원칙

1. 같은 조건에서 `Before / After`를 비교한다.
2. 각 측정은 최소 3회 반복 후 평균값을 남긴다.
3. 수치는 반드시 테스트 조건과 함께 기록한다.
4. 사람 손측정은 참고값으로만 쓰고, 가능하면 인앱 계측 + 외부 도구를 함께 사용한다.
5. 클립 저장 Before/After의 공식 비교값은 동일 시나리오의 수동 측정값을 우선한다.
6. `clip_total_ms`는 `path`, `frames`, `code` 확인을 위한 보조 지표로 기록하고, 개선율 계산의 유일한 공식 기준으로는 사용하지 않는다.
7. Debug는 원인 파악용, Release는 최종 보고용으로 사용한다.

---

## 2. 측정 환경 / 도구

| 항목 | 값 |
|------|----|
| 측정 일시 | 수시 업데이트 |
| 측정자 |  |
| 브랜치 / 커밋 | 로컬 작업 트리 기준 |
| 빌드 타입 | Debug / Release 분리 |
| OS | Windows |
| CPU |  |
| RAM |  |
| GPU |  |
| 디스플레이 해상도 |  |
| 네트워크 환경 |  |
| 서버 환경 |  |
| 외부 측정 도구 | PerfMon |
| 인앱 계측 사용 여부 | Yes (`qInfo()` perf metric 로그) |

인앱 계측 주요 항목:
- `screen_transition_ms`
- `first_frame_after_transition_ms`
- `first_live_frame_after_transition_ms`
- `render_fps`
- `clip_total_ms`
- `active_apply_ms`

관련 코드:
- [mainwindow_navigation.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/mainwindow_navigation.cpp)
- [video_render_widget.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- [channel_session_manager.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)
- [clip_capture_manager.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)

PerfMon 카운터:
- `Process(VMS_v2)\% Processor Time`
- `Process(VMS_v2)\Working Set - Private`
- `Process(VMS_v2)\Private Bytes`
- `Process(VMS_v2)\ID Process`

메모:
- 과거 baseline은 `Process(VMS_v1)` 기준 수치와 함께 해석해야 한다.
- 이번 baseline은 `4-view / 6-view / 9-view`를 완전히 분리하지 않은 통합 측정이다.

---

## 3. 현재 성능 이슈 목록

### 3-1. 렌더링 / 스트리밍

설명:
- 메인 멀티뷰에서 FPS 저하와 체감 버벅임이 발생한다.
- 화면 전환 함수 자체보다 첫 프레임 도착 지연이 체감 성능에 더 크게 영향을 준다.

현재 추정 원인:
1. `decode + videoconvert + appsink/QImage copy` 경로 비용
2. 멀티뷰 복귀 시 다중 스트림 재활성화 비용
3. hidden/visible 상태 전환 시 dispatch 부담

관련 코드:
- [stream_player.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- [video_render_widget.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- [channel_session_manager.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/channel_session_manager.cpp)

### 3-2. 클립 저장

설명:
- 10초 클립 저장이 과거 기준 비정상적으로 오래 걸렸다.
- 특히 전체화면 캡처 시 UI 체감 저하가 컸다.

현재 추정 원인:
1. PNG 임시 파일 기반 경로
2. 디스크 I/O 과다
3. 전체화면 합성 캡처 비용

관련 코드:
- [clip_capture_manager.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)

### 3-3. Playback / UGV 경로

설명:
- Playback 타임라인은 이벤트 마커 수가 많을 때 UI 스레드 부하가 커졌다.
- `UGV -> Playback` 전환은 첫 프레임 지연 편차가 여전히 크다.

현재 추정 원인:
1. Playback 이벤트 마커 수 과다
2. Playback 초기 진입 시 timeline/stream 요청 중첩
3. UGV -> Playback 전환 시 첫 프레임 도착 지연 편차

관련 코드:
- [playback_screen_timeline.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen_timeline.cpp)
- [playback_screen.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/playback_screen.cpp)

---

## 4. Before 측정

### 4-1. 렌더링 / 스트리밍 baseline

테스트 조건:
- 통합 baseline 측정
- `4-view / 6-view / 9-view`를 완전히 분리하지 않고 실제 사용 흐름 기준으로 수집
- PerfMon 수집 구간: `2026-03-16 13:57:36 ~ 14:11:10`

측정 결과:

| 항목 | 값 | 측정 조건 | 비고 |
|------|----|-----------|------|
| 프로세스 CPU 평균 | 215.9 | `Process(VMS_v1)\% Processor Time` | 통합 baseline |
| 프로세스 CPU 최대 | 446.3 | `Process(VMS_v1)\% Processor Time` | 통합 baseline |
| Working Set - Private 평균 (MB) | 3656.2 | `Process(VMS_v1)\Working Set - Private` | 통합 baseline |
| Working Set - Private 최대 (MB) | 6283.5 | `Process(VMS_v1)\Working Set - Private` | 통합 baseline |
| Private Bytes 평균 (MB) | 3727.1 | `Process(VMS_v1)\Private Bytes` | 통합 baseline |
| Private Bytes 최대 (MB) | 6360.9 | `Process(VMS_v1)\Private Bytes` | 통합 baseline |
| GPU 사용률 (%) | N/A | 미측정 |  |
| Main 멀티뷰 FPS 범위 | 0.2 ~ 17.6 | `render_fps` 인앱 로그 기준 | 멀티뷰 복귀 시 급격한 저하 확인 |
| 단일 화면 FPS 범위 | 23.6 ~ 32.3 | `render_fps` 인앱 로그 기준 | CCTV / UGV / Playback 단일 화면 |
| frame drop rate (%) | N/A | 계측 미구현 |  |
| 로그인 -> 메인 | 0.209초 | `screen_transition_ms` 로그 기준 | `device_check -> main` |
| 메인 -> CCTV | 0.048 ~ 0.209초 | `screen_transition_ms` 로그 기준 | 반복 측정값 |
| CCTV / Playback -> Main | 0.017 ~ 0.559초 | `screen_transition_ms` 로그 기준 | 복귀 경로 포함 |
| 메인 -> Playback | 0.093 ~ 0.139초 | `screen_transition_ms` 로그 기준 | 반복 측정값 |

해석:
- CPU와 메모리 모두 높은 편이었고, 특히 `Working Set - Private` / `Private Bytes` 최대치가 6GB대까지 상승했다.
- 단일 화면은 대체로 30fps 근처를 유지했지만, Main 멀티뷰 복귀 시 `0.2 ~ 17.6fps`까지 크게 저하됐다.
- 화면 전환 함수 자체는 수십 ms 수준인 경우가 많았지만, 복귀 경로에서 최대 `559ms`까지 증가하는 구간이 있었다.
- 따라서 baseline 시점 체감 성능 저하의 핵심은 단일 화면 렌더링보다 멀티뷰 복귀 및 다중 스트림 활성화 경로로 판단했다.

### 4-2. 클립 저장 baseline

테스트 조건:
- 모든 측정은 10초 클립 저장 기준
- 채널 캡처와 전체화면(Widget Composite) 캡처를 구분해서 기록
- baseline 시점은 PNG 시퀀스 기반 인코딩 경로

측정 결과:

| 항목 | 값 | 측정 조건 | 비고 |
|------|----|-----------|------|
| 10초 클립 저장 #1 | 55.00초 | 채널 캡처 / 약 1.1MB |  |
| 10초 클립 저장 #2 | 154.00초 | 채널 캡처 / 약 981KB |  |
| 10초 클립 저장 #3 | 339.00초 | CCTV 전체화면 / 약 4.8MB |  |
| 10초 클립 저장 #4 | 469.00초 | CCTV 전체화면 / 약 5.04MB |  |
| 10초 클립 저장 #5 | 40.52초 | 채널 캡처 / 약 269KB |  |
| 10초 클립 저장 #6 | 64.00초 | 채널 캡처 / 약 480KB |  |
| 10초 클립 저장 #7 | 34.85초 | 채널 캡처 / 약 363KB |  |
| 10초 클립 저장 #8 | 418.00초 | CCTV 전체화면 / 약 9.73MB |  |
| 10초 채널 캡처 평균 | 69.67초 | #1, #2, #5, #6, #7 평균 | 채널 캡처 5회 |
| 10초 전체화면 캡처 평균 | 408.67초 | #3, #4, #8 평균 | 전체화면 캡처 3회 |
| 저장 중 UI 응답성 | 저하 큼 | 체감상 장시간 대기 발생 | baseline |

해석:
- 10초 클립 저장 시간이 채널 캡처도 `34.85초 ~ 154초`, 전체화면 캡처는 `339초 ~ 469초`로 비정상적으로 길었다.
- 병목은 `PNG 시퀀스 저장 -> ffmpeg 재읽기` 경로로 보는 것이 타당했고, 전체화면 녹화에서 비용이 특히 크게 증가했다.

---

## 5. 패치 / 변경 이력

### 5-1. Phase 9-A

핵심 조치:
1. `ffmpeg stdin pipe`를 기본 인코딩 경로로 전환
2. 메모리 프레임(`QImage`)을 `rawvideo(bgra)`로 직접 `ffmpeg` stdin에 전달
3. 기존 PNG 직렬 저장 경로는 `png_fallback`으로 격리

관련 코드:
- [clip_capture_manager.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.cpp)
- [clip_capture_manager.h](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/clip_capture_manager.h)

체크포인트:
- `clip_total_ms`가 초 단위가 아니라 ms 단위 지표로 내려오기 시작
- 채널 캡처 기준 2~3초대 수동 측정 확보
- 전체화면 캡처는 추가 개선 여지가 남아 있었음

### 5-2. Phase 9-C

핵심 조치:
1. `StreamQualityProfile` 도입 (`Normal / QuadGrid / DenseGrid`)
2. visible target 기준 render dispatch
3. hidden 상태 throttling
4. `first_frame_after_transition_ms`, `first_live_frame_after_transition_ms` 추가

관련 코드:
- [stream_player.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.cpp)
- [stream_player.h](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/stream_player.h)
- [video_render_widget.cpp](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.cpp)
- [video_render_widget.h](c:/Users/1-13/Desktop/VEDA_FINAL/Qt/VMS_v2/video_render_widget.h)

체크포인트:
- `Main -> CCTV` 체감 첫 프레임 개선
- 멀티뷰 steady-state가 DenseGrid 기준 약 6fps 선으로 안정화
- fullscreen 첫 프레임 지연은 별도 추적 대상으로 분리

---

## 6. After 측정

이 섹션은 최종 Release 수치가 아니라, 최신 Debug 체크포인트를 우선 기록한다.

### 6-1. 2026-03-23 Debug 체크포인트

기준 파일:
- `c:\Users\1-13\Downloads\after.blg`
- `c:\Users\1-13\Downloads\인라인측정기록.txt`

메모:
- Debug 빌드를 디버거 없이 실행한 체크포인트 기준
- Release에서도 동일 인앱 로그를 수집할 수 있음
- 최종 발표 수치는 같은 시나리오로 Release 재측정 후 확정

#### 렌더링 / 스트리밍

| 항목 | Before | After | 차이 | 비고 |
|------|--------|-------|------|------|
| 프로세스 CPU 평균 (%) | 215.9 | 244.8 | +28.9 | `after.blg`, `Process(VMS_v2)\% Processor Time` 평균 |
| 프로세스 CPU 최대 (%) | 446.3 | 937.2 | +490.9 | Debug 스파이크 포함 |
| Working Set - Private 평균 (MB) | 3656.2 | 718.2 | -2938.0 | `after.blg` 기준 |
| Working Set - Private 최대 (MB) | 6283.5 | 3993.0 | -2290.5 | `after.blg` 기준 |
| Private Bytes 평균 (MB) | 3727.1 | 781.6 | -2945.5 | `after.blg` 기준 |
| Private Bytes 최대 (MB) | 6360.9 | 4082.0 | -2278.9 | `after.blg` 기준 |
| Main 멀티뷰 FPS 범위 | 0.2 ~ 17.6 | 5.5 ~ 7.7 | steady-state 안정화 | `DenseGrid` 기준, `480x270 @ 6fps` 협상 확인 |
| 전체 `render_fps` 평균 | N/A | 7.0 | N/A | 전체 로그 1523건 평균, 단일/멀티뷰 혼합값 |
| 로그인 -> 장치확인 (ms) | N/A | 9 ~ 22 (avg 13.0) | N/A | `screen_transition_ms` |
| 장치확인 -> 메인 (ms) | 209 | 457 ~ 533 (avg 485.3) | +276.3 | 장치 바인딩/멀티뷰 구성 포함 |
| 메인 -> CCTV (ms) | 48 ~ 209 | 20 ~ 76 (avg 38.7) | 개선 | `screen_transition_ms` |
| CCTV -> 메인 (ms) | 17 ~ 559 | 14 ~ 51 (avg 21.6) | 개선 | `screen_transition_ms` |
| 메인 -> UGV (ms) | N/A | 147 ~ 225 (avg 179.0) | N/A | `screen_transition_ms` |
| UGV -> Playback (ms) | N/A | 20 ~ 31 (avg 25.0) | N/A | `screen_transition_ms` |
| Playback -> 메인 (ms) | N/A | 12 ~ 17 (avg 14.5) | N/A | `screen_transition_ms` |
| 메인 -> 로그인 (ms) | N/A | 212 ~ 273 (avg 242.5) | N/A | 로그아웃/화면 정리 포함 |
| `first_live_frame_after_transition_ms` `device_check->main` | N/A | 33 ~ 80 (avg 55.7) | N/A | 라이브 첫 프레임 기준 |
| `first_frame_after_transition_ms` `device_check->main` | 614 ~ 982 | 406 ~ 603 (avg 486.0) | 개선 | 첫 렌더 기준 |
| `first_live_frame_after_transition_ms` `main->cctv` | 약 95 ~ 188 | 135 ~ 248 (avg 180.6) | 비슷 | 체감 병목은 여전히 첫 프레임 계열 |
| `first_frame_after_transition_ms` `main->cctv` | 약 1989 ~ 2158 | 175 ~ 304 (avg 242.1) | 큰 폭 개선 | CCTV 첫 프레임 지연 감소 |
| `first_live_frame_after_transition_ms` `cctv->main` | N/A | 9 ~ 53 (avg 31.0) | N/A | 복귀는 비교적 빠름 |
| `first_frame_after_transition_ms` `cctv->main` | 21 ~ 50 | 41 ~ 89 (avg 61.8) | 소폭 증가 | 복귀 직후 첫 렌더 변동 존재 |
| `first_live_frame_after_transition_ms` `ugv->playback` | N/A | 11 ~ 2971 (avg 1377.3) | N/A | 편차가 매우 큼 |
| `first_frame_after_transition_ms` `ugv->playback` | N/A | 25 ~ 2980 (avg 1392.3) | N/A | 동일 |

판단:
- 메모리 지표는 과거 baseline 대비 크게 개선됐다.
- `Main -> CCTV` 첫 프레임 지연은 과거 약 2초 수준에서 `175 ~ 304ms`까지 줄어 체감 개선이 컸다.
- `device_check -> main`은 평균 약 `485ms`라 Release에서 같은 시나리오 재측정이 필요하다.
- `UGV -> Playback` 첫 프레임 편차는 여전히 크므로 추적 대상이다.

#### 클립 저장

메모:
- 초기 6건(2026-03-23): `clip_total_ms` 기준, 채널/전체화면 구분 없이 기록.
- 2026-03-30 추가 측정: 멀티뷰 셀 캡처(6뷰/9뷰) 4건 + 전체화면 캡처 4건.
- 이번 수치는 Debug 빌드 체크포인트 기준. 최종 보고용 수치는 Release에서 재확인 필요.

| 항목 | Before | After | 차이 | 비고 |
|------|--------|-------|------|------|
| 초기 측정 6건 범위 | 34.85초 ~ 469.00초 | 728ms ~ 5,400ms | 큰 폭 개선 | `clip_total_ms`, 채널/전체화면 미구분 |
| 초기 측정 6건 평균 | N/A | 3,148ms (3.15초) | N/A | `5400, 3865, 3774, 3450, 728, 3676` ms |
| 멀티뷰 셀 캡처 - 6뷰 #1 | N/A | 6,528ms (6.5초) | N/A | frames:77, 406KB, 2026-03-30 |
| 멀티뷰 셀 캡처 - 6뷰 #2 | N/A | 5,549ms (5.5초) | N/A | frames:60, 325KB, 2026-03-30 |
| 멀티뷰 셀 캡처 - 9뷰 #1 | N/A | 3,338ms (3.3초) | N/A | frames:73, 476KB, 2026-03-30 |
| 멀티뷰 셀 캡처 - 9뷰 #2 | N/A | 3,682ms (3.7초) | N/A | frames:64, 419KB, 2026-03-30 |
| **멀티뷰 평균** | **69.67초** | **4.77초** | **-93%** | 위 4건 평균 vs Before 채널 캡처 5건 평균 |
| 전체화면 캡처 #1 | N/A | 4,573ms (4.6초) | N/A | frames:274, 3.53MB, 2026-03-30 |
| 전체화면 캡처 #2 | N/A | 9,189ms (9.2초) | N/A | frames:251, 3.81MB, 2026-03-30 |
| 전체화면 캡처 #3 ⚠️ | N/A | 9,189ms (9.2초) | N/A | frames:272, 11.0MB, 포커스 조작 중 촬영 — 이상값 |
| 전체화면 캡처 #4 | N/A | 3,693ms (3.7초) | N/A | frames:249, 2.70MB, 2026-03-30 |
| **전체화면 평균** (정상 3건) | **408.67초** | **5.95초** | **-98%** | #3 이상값 제외 |
| 저장 중 UI 응답성 | 저하 | 체감상 양호 | 개선 | 최종 평가는 Release 재확인 권장 |

판단:
- 멀티뷰 셀 캡처: **3.3 ~ 6.5초** (평균 4.8초), Before 35~154초 대비 **93% 감소**.
- 전체화면 캡처: **3.7 ~ 9.2초** (평균 6.0초), Before 339~469초 대비 **98% 감소**.
- 9뷰가 6뷰보다 빠른 이유: DenseGrid(480×270) 프레임 크기 작아서 인코딩 비용 낮음.
- 전체화면 #3(11MB, 9189ms)은 포커스 조작으로 화면 변화량 급증 → H.264 키프레임 폭증 → 파일 크기 및 시간 증가. 이상값으로 처리.
- Release 라운드에서 동일 시나리오 재측정으로 최종 수치 확정 예정.


---

## 7. 발표 / 공유용 요약

### 핵심 before / after

| 항목 | Before | After | 개선폭 |
|------|--------|-------|--------|
| Working Set - Private 최대 | 6283.5 MB | 3993.0 MB | -2290.5 MB |
| Private Bytes 최대 | 6360.9 MB | 4082.0 MB | -2278.9 MB |
| `main->cctv` first frame | 약 1989 ~ 2158 ms | 175 ~ 304 ms | 큰 폭 개선 |
| Main 멀티뷰 DenseGrid FPS | 0.2 ~ 17.6 fps (변동 큼) | 5.5 ~ 7.7 fps | steady-state 안정화 |
| 10초 클립 저장 시간 | 34.85초 ~ 469.00초 | 728ms ~ 5400ms | 큰 폭 개선 |

발표 문구 예시:
- `Main -> CCTV 첫 프레임 지연을 약 2초 수준에서 200ms대까지 낮췄다.`
- `프로세스 메모리 최대치를 6GB대에서 4GB 수준으로 줄였다.`
- `클립 저장 경로를 stdin pipe 기반으로 바꾸면서 10초 클립 저장 시간이 수십 초~수분대에서 수 초대로 내려왔다.`
- `멀티뷰는 DenseGrid 기준 약 6fps steady-state로 안정화됐다.`

---

## 8. Blocked / 유의사항

- Playback export:
  - 현재는 체감상 즉시 다운로드되어 기본 성능 추적 대상에서는 제외
  - 이후 서버 처리 지연 또는 다운로드 저장 병목이 관측될 때만 별도 재측정
- UGV success-path E2E:
  - 서버 최신 코드 / 운영 환경 상태에 따라 편차가 있어, 최종 수치 확정 전 환경을 같이 기록할 필요가 있음
- Release 측정 미완료:
  - 현재 문서의 최신 After 값은 Debug 빌드를 디버거 없이 실행한 체크포인트 기준
  - 최종 발표 수치로는 Release 재측정이 필요

---

## 9. 다음 측정 예정 항목

- [ ] Release 빌드 기준 동일 시나리오 재측정
- [ ] `4-view / 6-view / 9-view` 분리 측정
- [ ] `clip_total_ms` 포함한 10초 / 15초 클립 저장 시나리오 재측정
- [ ] `UGV -> Playback` first frame 편차 재측정
- [ ] 필요 시 HW decode / GPU 경로 재확인

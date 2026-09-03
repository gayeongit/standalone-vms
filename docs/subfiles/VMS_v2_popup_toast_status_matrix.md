# VMS v2 Popup/Toast/Status 호출 매트릭스

작성일: 2026-03-24  
기준: `PopupManager`, `showToastMessage`, `showActionStatus`, `showPersistentStatusMessage` 호출 전수 스캔

## 1) 수집 결과 요약

- `PopupManager::*`: 42건
- `showActionStatus(...)`: 60건
- `showToastMessage(...)`: 0건
- `showPersistentStatusMessage(...)`: **호출 0건** (정의만 존재)

## 2) 파일별 전수 인벤토리 (라인 기준)

| 파일 | PopupManager 라인 | showActionStatus 라인 | showToastMessage 라인 | showPersistentStatusMessage 라인 |
|---|---|---|---|---|
| `capture_storage_helpers.cpp` | 166 | 159, 177 | - | - |
| `cctv_screen.cpp` | 298, 308 | 319, 353, 359, 573, 577, 585, 593, 596, 612, 616, 624, 632, 635 | - | - |
| `login_screen.cpp` | 565 | - | - | - |
| `main_screen.cpp` | 903, 918, 961 | 922, 927, 939, 941, 954, 972, 1003, 1008, 1012 | - | - |
| `mainwindow_auth.cpp` | 118, 140, 145, 192, 217, 574, 579 | - | - | - |
| `mainwindow_navigation.cpp` | 156, 169 | - | - | - |
| `playback_screen.cpp` | 327, 349, 360, 381 | 443 | - | - |
| `playback_screen_timeline.cpp` | 102 | 105, 112, 132, 172, 189, 197, 224 | - | - |
| `playback_screen_export.cpp` | 138, 292, 339, 370, 379, 424 | 96, 125, 129, 134, 185, 192, 204, 218, 225, 266, 278, 281, 295, 306, 315, 325, 329, 418, 427 | - | - |
| `settings_dialog.cpp` | 162, 196, 207, 229, 244, 326, 330, 335, 340 | - | - | - |
| `ugv_screen.cpp` | 650, 771, 1388 | 661, 695, 701, 758, 763 | - | - |
| `ugv_screen_feedback.cpp` | - | 164, 166, 168, 183 | - | - |

## 3) 화면/트리거/조건/메시지/표시방식 매트릭스

| 화면 | 트리거 | 조건 | 메시지(요약) | 표시방식 | 위치 |
|---|---|---|---|---|---|
| 가입 | 회원가입 성공 | 서버 응답 `ok` | 가입 완료 후 로그인 안내 | 팝업(`showInfo`) | `mainwindow_auth.cpp:118` |
| 장치확인 진입 | 시작 버튼 클릭 | `DeviceService` 없음 | 장치 서비스 미연결 | 팝업 | `mainwindow_auth.cpp:140` |
| 장치확인 진입 | 시작 버튼 클릭 | 선택 정규화 결과 없음 | 선택 채널 없음 | 팝업 | `mainwindow_auth.cpp:145` |
| 장치확인 진입 | RTSP 상세 조회 완료 | 조회 성공 채널 0건 | RTSP 조회 실패 안내 | 팝업 | `mainwindow_auth.cpp:192` |
| 장치확인 진입 | RTSP 상세 조회 완료 | 일부 실패 | 일부 조회 실패, 성공 채널만 진입 | 팝업 | `mainwindow_auth.cpp:217` |
| 로그아웃 | 로그아웃 클릭 | 작업 상태별(`clipOn/exportOn`) | 로그아웃 확인 | 확인 팝업(`confirm`) | `mainwindow_auth.cpp:574` |
| 로그아웃 후처리 | 로그아웃 확정 | 클립 작업 진행 중 | 클립 저장 취소 안내 | 팝업 | `mainwindow_auth.cpp:579` |
| 앱 종료 | 윈도우 close | 클립/내보내기 진행 중 | 종료 시 작업 중단 확인 | 확인 팝업 | `mainwindow_navigation.cpp:156,169` |
| 장치확인 화면 | VMS 시작 클릭 | 선택 장치 없음 | 장치 미선택 시 진입 불가 | 팝업 | `login_screen.cpp:565` |
| 메인 이벤트뷰 | UGV 출동 요청 | gateway/channel 식별 실패 | 연결 가능한 UGV 식별자 없음 | 팝업 | `main_screen.cpp:903` |
| 메인 이벤트뷰 | UGV 출동 요청 | 출동 전 | 출동 확인(클립 저장 중이면 경고 포함) | 확인 팝업 | `main_screen.cpp:918` |
| 메인 이벤트뷰 | UGV 출동 + 클립 자동저장 | 인코딩 진행 | 클립 저장 중 | 상태라벨(`progress`) | `main_screen.cpp:922` |
| 메인 이벤트뷰 | UGV 출동 + 클립 자동저장 | prepare/encode 실패 | 자동 저장 실패 상세 | 상태라벨(`error`) | `main_screen.cpp:927,939` |
| 메인 이벤트뷰 | UGV 출동 + 클립 자동저장 | encode 성공 | 클립 자동 저장됨 | 상태라벨(`success`) | `main_screen.cpp:941` |
| 메인 하단 | 스냅샷 클릭 | 캡처 소스 없음 | 캡처 소스 없음 | 상태라벨 | `main_screen.cpp:954` |
| 메인 하단 | 스냅샷 실패 | 저장 경로 설정 필요 | 설정 이동 여부 확인 | 선택 팝업(`confirmWithLabels`) | `main_screen.cpp:961` |
| 메인 하단 | 스냅샷 실패 | 기타 오류 | 에러 메시지 표시 | 상태라벨 | `main_screen.cpp:972` |
| 메인 하단 | 클립 클릭 | 인코딩 중 | 인코딩 진행 중 안내 | 상태라벨 | `main_screen.cpp:1003` |
| 메인 하단 | 클립 시작 | 소스 없음/시작 실패 | 시작 실패 안내 | 상태라벨 | `main_screen.cpp:1008,1012` |
| CCTV 화면 | UGV 트리 더블클릭 | UGV 타입 | UGV 진입 정책 안내 | 팝업 | `cctv_screen.cpp:298` |
| CCTV 하단 | 스냅샷 실패 | 저장 경로 설정 필요 | 설정 이동 여부 확인 | 선택 팝업 | `cctv_screen.cpp:308` |
| CCTV 하단 | 스냅샷/클립 | 오류/중복 작업 | 스냅샷 오류, 클립 인코딩 중, 시작 실패 | 상태라벨 | `cctv_screen.cpp:319,353,359` |
| CCTV 제어 | Zoom/Focus 요청 | 채널 없음/서비스 없음 | 제어 불가 사유 | 상태라벨 | `cctv_screen.cpp:573,577,612,616` |
| CCTV 제어 | Zoom/Focus 요청 | 요청중/성공/실패 | 진행/성공/실패 상태 및 실패 상세 | 상태라벨 | `cctv_screen.cpp:585,593,596,624,632,635` |
| UGV 화면 | 스냅샷 실패 | 저장 경로 설정 필요 | 설정 이동 여부 확인 | 선택 팝업 | `ugv_screen.cpp:650` |
| UGV 화면 | 스냅샷/클립 | 오류/중복 작업 | 스냅샷 오류, 인코딩 중, 시작 실패 | 상태라벨 | `ugv_screen.cpp:661,695,701` |
| UGV 화면 | 임무 버튼 | 서비스 미준비/대상 없음 | UGV 준비/대상 채널 오류 | 상태라벨 | `ugv_screen.cpp:758,763` |
| UGV 화면 | 임무 종료 | 연결 세션 상태 | 임무 종료 확인 | 확인 팝업 | `ugv_screen.cpp:771` |
| UGV 화면 | 화면 이동 | 연결 세션 상태 | UGV 연결 종료 후 화면 이동 확인 | 확인 팝업 | `ugv_screen.cpp:1388` |
| UGV 상태패널 | 세션 상태 변경 | connected/connecting/disconnected | Connected/Connecting/Disconnected | 상태라벨 | `ugv_screen_feedback.cpp:164,166,168` |
| UGV 상태패널 | 서비스 에러 표시 | classify 결과 | 에러 메시지(표준화된 display 포함) | 상태라벨 | `ugv_screen_feedback.cpp:183` |
| Playback 화면 | UGV 트리 더블클릭 | UGV 타입 | UGV 진입 정책 안내 | 팝업 | `playback_screen.cpp:327` |
| Playback 화면 | 스냅샷 실패 | 저장 경로 설정 필요/기타 | 설정 이동 확인 또는 오류 | 선택 팝업/팝업 | `playback_screen.cpp:349,360` |
| Playback 화면 | 재생 버튼 | 재생 소스 없음 | 먼저 재생 항목 선택 | 팝업 | `playback_screen.cpp:381` |
| Playback 화면 | 플레이어 오류 | `errorOccurred` | 재생 오류 메시지 | 상태라벨 | `playback_screen.cpp:443` |
| Playback 타임라인 | 타임라인 조회 | 서비스 없음 | PlaybackService 미연결 (첫 1회 팝업, 이후 상태라벨) | 팝업 + 상태라벨 | `playback_screen_timeline.cpp:102,105` |
| Playback 타임라인 | 타임라인/URL 요청 | 조회/요청 lifecycle | 조회 중, 조회 실패, URL 요청 중/실패/준비됨 | 상태라벨 | `playback_screen_timeline.cpp:112,132,189,197,224` |
| Playback 타임라인 | 자동 재생 시작 | playable range 없음 | 재생 가능한 구간 없음 | 상태라벨 | `playback_screen_timeline.cpp:172` |
| Playback 내보내기 | 취소/중복 실행 | busy 상태 | 취소됨, 이미 진행 중 안내 | 상태라벨 | `playback_screen_export.cpp:96,125,129,329` |
| Playback 내보내기 | 요청 전 검증 | 미선택/미연결/시간오류 | 선택/시간 검증은 상태라벨, 서비스 미연결은 팝업 | 상태라벨 + 팝업 | `playback_screen_export.cpp:134,138,185,192` |
| Playback 내보내기 | 요청~조회 | request/poll lifecycle | 요청중, 실패, 준비중, 조회실패, 시간초과 | 상태라벨 | `playback_screen_export.cpp:204,218,225,266,278,281` |
| Playback 내보내기 | 상태 처리 | DONE/FAILED/UNKNOWN | URL 없음은 팝업, FAILED/UNKNOWN은 상태라벨 | 팝업 + 상태라벨 | `playback_screen_export.cpp:292,295,306,315` |
| Playback 내보내기 | 다운로드 | init/url/path/network/commit 오류 | URL/경로/실파일 저장 실패는 팝업, 네트워크 실패는 상태라벨 | 팝업 + 상태라벨 | `playback_screen_export.cpp:325,339,370,379,418,424,427` |
| 설정(장치관리) | 편집/추가/삭제 | 입력 누락/미선택/중복 | 장치 입력/선택/중복 안내 | 팝업 | `settings_dialog.cpp:162,196,207,229,244` |
| 설정(저장경로) | 저장 버튼 | 경로/권한 검증 실패 | 경로 유효성/권한 오류 | 팝업 | `settings_dialog.cpp:326,330,335,340` |
| 공용 인코딩 에러 핸들러 | 클립 인코딩 실패 처리 | 취소/경로오류/기타오류 | 취소/기타 오류는 상태라벨, 경로 오류만 설정 이동 확인 | 상태라벨 + 선택 팝업 | `capture_storage_helpers.cpp:159,166,177` |

## 4) 표시방식 분류 규칙 (현재 코드 상태)

| 표시방식 | 현재 사용 목적 |
|---|---|
| 팝업 (`PopupManager::showInfo`) | 즉시 사용자 확인이 필요한 오류/제약/안내 |
| 확인 팝업 (`confirm`, `confirmWithLabels`) | 파괴적/전환성 작업 전 사용자 의사 확인 |
| 토스트 (`showToastMessage`) | **현재 미사용** |
| 상태라벨 (`showActionStatus`) | 화면 내 진행/성공/실패 상태를 짧게 표시 |
| 지속 상태 (`showPersistentStatusMessage`) | **현재 호출 없음** |

## 5) 확인 메모

- `showToastMessage(...)`는 정의만 있고 호출이 없습니다.
- `showPersistentStatusMessage(...)`도 정의만 있고 호출이 없습니다.
- Main / CCTV / UGV 스냅샷·클립 실패는 상태라벨 중심으로 정리되었습니다.
- CCTV Zoom / Focus는 상태라벨만 사용합니다.
- Playback 타임라인 미연결은 `first-hit popup + 이후 status`로 정리되었습니다.
- 문서 상단의 호출 건수는 raw grep count가 아니라 실제 callsite 기준입니다.

# CLAUDE.md

이 파일은 Claude Code가 이 레포에서 작업할 때 참고하는 컨텍스트 파일이다.

## 프로젝트 개요

`standalone-vms`는 팀 프로젝트 [VEDA3-CLUE-VMS](https://github.com/gayeongit/VEDA3-CLUE-VMS)(`v2` 브랜치)의 VMS 클라이언트 파트를 기반으로, 서버 의존성을 제거하는 방향으로 재설계하는 개인 프로젝트다.

기존 구조는 서버가 없으면 로그인도, 장치 조회도, 카메라 제어도 안 되는 구조였다. 이 프로젝트의 목표는 서버 없이 `VMS ↔ 카메라(현재는 RPi로 대체)`가 직접 통신해서 핵심 기능이 동작하게 만들고, 서버는 선택적 부가 기능(계정 동기화, 이벤트 히스토리)으로 내리는 것이다.

RPi를 당장 쓸 수 없는 상황이라, 카메라 의존이 없는 작업(로그인/게스트 분기)부터 먼저 진행 중이다. 자세한 배경은 `docs/roadmap.md`의 "진행 순서 변경" 절 참고.

## 참고 문서

- 전체 로드맵: `docs/roadmap.md`
- Phase별 세부 실행계획: `docs/dev_execution_plan.md` — 단일 파일, Phase 진입 시 해당 Phase 섹션을 이어서 추가한다. Phase마다 별도 파일을 만들지 않는다.
- 코드 구조 가이드: `docs/core_classes.md` — 리팩토링 시작 시점 스냅샷으로 시작한다. 전체 재작성 금지, Phase 완료 후 실제로 바뀐 클래스 섹션만 부분 수정한다. 이전 버전이 필요하면 git 히스토리(`git log -p docs/core_classes.md`)로 확인한다.

## 설계 원칙

1. `AppState`를 접점으로 삼는다 — `selectedChannelContexts`, `channelRtspByName/Id`, `channelVideoCodecByName/Id` 등 기존 필드를 채우는 방식으로 새 입구를 만들고, 화면/미디어 계층(`ChannelSessionManager`, `StreamPlayer`, `MainScreen` 등)은 건드리지 않는다.
2. 서비스 인터페이스는 유지하고 구현만 교체한다 — `CctvControlService`, `DeviceService` 등 시그니처 변경 금지, 내부 통신 대상만 서버 → 카메라로 바꾼다.
3. 로그인 여부는 "데이터가 오는지 안 오는지"를 가르지 않는다 — 로그인 없이도 카메라 데이터는 항상 직접 받는다. 로그인은 부가 기능(자격증명 동기화, 이벤트 히스토리)만 얹는다.
4. 카메라(RPi) 쪽은 정교하게 만들 필요 없다 — HTTP mock 응답, 더미 UDP 이벤트, mediamtx 스트림 정도로 충분. 목적은 VMS 로직 검증이지 카메라 재현이 아니다.

## 스코프 아님 (건드리지 말 것)

- ONVIF 풀스펙 구현 (Discovery/GetStreamUri/PTZ 최소 범위만)
- 실제 짐벌/모터 하드웨어 제어
- UGV 관련 기능 — 삭제하지 말고 비활성화/스텁 처리로 대응 (`docs/roadmap.md` 3.5절 참고), 최종 정리는 Phase 6에서
- Playback 영상 퀄리티 (프로토콜 흐름만 맞으면 됨)
- 자체 암호화 구현 (QtKeychain 등 OS 네이티브 저장소 사용)

## 빌드 / 실행

```bash
# TODO(사용자 확인 필요): 실제 빌드 명령어로 교체
cmake -B build
cmake --build build
```

## 코드 컨벤션

- 기존 v2 코드 스타일 따름
- Qt 6, C++17 이상 (TODO: 실제 `CMakeLists.txt` 기준으로 확정 필요)
- 신규 카메라 통신 관련 클래스 배치 위치: TODO(확인 필요) — `camera/` 신설 or 기존 `services/` 재사용

## 문서 갱신 규칙

- `docs/core_classes.md`는 Phase 종료 시 전체 재작성하지 않는다. 실제로 바뀐 클래스에 해당하는 섹션만 부분 수정하고, 신규 클래스는 해당 위치에 소제목을 추가한다.
- 화면/미디어 계층처럼 설계 원칙상 건드리지 않는 섹션은 그대로 둔다.
- Phase 진행 중 로드맵 순서나 스코프에 변화가 생기면 `docs/roadmap.md`도 함께 갱신한다.

## 현재 상태

- Phase 3a(로그인/게스트 분기, 카메라 무관) 진행 예정 — 세부 계획은 `docs/dev_execution_plan.md` 참고
- Phase 0(RPi 테스트 베드) / Phase 1(카메라 도메인 입구)은 RPi 확보 전까지 보류

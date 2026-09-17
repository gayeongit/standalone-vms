# standalone-vms

서버 의존성을 제거한 VMS 클라이언트. 배경/설계 원칙은 `CLAUDE.md`, `docs/roadmap.md` 참고.

## 설정 파일

`app_config.json`은 서버(로그인/이벤트/Playback) 설정을 담는 파일이라 `.gitignore`되어 있다.
`app_config.example.json`을 복사해서 만들면 되고, **이 파일 자체가 없어도 게스트로 로컬 카메라(ONVIF)
경로는 정상 동작한다** — `apiBaseUrl`이 없으면 로그인/서버 이벤트/서버 Playback만 비활성화되고,
`DeviceCheckScreen`의 ONVIF discovery와 Main의 RTSP 재생/PTZ 제어는 그대로 쓸 수 있다.

```bash
cp app_config.example.json app_config.json
# 서버를 쓸 계획이 있으면 apiBaseUrl 등을 채운다. 없으면 그대로 두고 "게스트로 시작"만 쓰면 된다.
```

## 게스트로 실행

1. `mock_camera_host/`를 먼저 띄운다 (`onvif_mock.exe`, `mediamtx.exe`+ffmpeg — 실행 방법/포트는 `docs/dev_execution_plan.md`의 Phase 0 섹션 참고).
2. `VMS_v2`를 실행하고 로그인 화면에서 "게스트로 시작"을 누른다.
3. 장치 확인 화면에서 ONVIF discovery로 발견된 채널을 선택하고 "VMS 시작"을 누르면 Main으로 진입한다.
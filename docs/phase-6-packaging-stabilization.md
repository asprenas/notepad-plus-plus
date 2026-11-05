# 6단계 세부 계획: 패키징 / 안정화

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장, 5.4절, 6장
- 기간: 3–4주
- 선행: [5단계 패널/부가 기능](phase-5-panels-extras.md) 완료
- 다음 단계: [7단계 플러그인 API](phase-7-plugin-api.md)


> 개발 범위: 현재 대상 플랫폼은 **Windows·Linux**. macOS는 차후 개발 예정이므로 macOS 관련 작업·테스트는 완료 기준에서 제외하고 별도 문서 [macOS 지원 세부 계획](phase-macos.md)에서 관리한다.

---

## 1. 목표

Windows·Linux에서 **설치하거나 풀어서 바로 쓰는 배포본**을 만들고, 성능·안정성을 베타 수준으로 끌어올린다.
GitHub은 저장소로만 쓰므로 빌드·서명·업로드는 로컬에서 수행한다 ([계획서 11.1절](qt-cross-platform-port-plan.md)).

## 2. 범위

| 영역 | 내용 |
|---|---|
| 배포본 | Windows 설치본 + 포터블 zip, Linux AppImage(+ 선택: Flatpak, deb), macOS `.app` + `.dmg` |
| OS 통합 | 파일 연결, 탐색기/파인더 "Notepad++로 열기", `.desktop`/MIME, 아이콘 |
| 안정성 | 크래시 리포트, 비정상 종료 후 복구 확인 |
| 성능 | 대용량 파일, 많은 탭, 시작 시간, 메모리 |
| 권한 | 관리자 권한 필요 파일 저장 |
| 릴리스 절차 | 버전 규칙, 로컬 릴리스 스크립트, GitHub Releases 수동 업로드 |

## 3. 설계

### 3.1 Windows

- 런타임 배포: `windeployqt` (0단계에서 `NPP_DEPLOY_QT_RUNTIME` 옵션으로 이미 사용) + MSVC 런타임(`vc_redist` 동봉 또는 앱 폴더 배치).
- 설치본 도구: **Inno Setup** 권장 (스크립트 단순, 로컬 실행 쉬움). 대안 WiX(MSI 필요 시).
- 설치본 기능: 설치 경로, 시작 메뉴, 파일 연결(선택), 탐색기 컨텍스트 메뉴(선택, `platform/win`), 기존 설정 유지, 제거 시 설정 삭제 여부 질문.
- 포터블: zip + `doLocalConf.xml` 포함.
- 관리자 권한 저장: 저장 실패(접근 거부) 시 권한 상승 도우미 프로세스로 저장 (원본 동작 대응).
- 서명: 외부 배포 시 코드 서명 인증서 필요. 개인 사용 단계에서는 미서명.

### 3.2 Linux

- **AppImage** (`linuxdeploy` + `linuxdeploy-plugin-qt`)를 1차 배포 형식으로.
- 빌드는 기존 Docker 이미지(Ubuntu 26.04)에서 수행. 구형 배포판 지원이 필요하면 더 오래된 glibc 기반 이미지 + 직접 빌드한 Qt로 별도 구성.
- `.desktop` 파일, MIME 등록(`text/plain` 외 주요 소스 형식), 아이콘(hicolor 여러 크기).
- 저장 시 원본 파일 권한·소유자 유지, 권한 부족 시 `pkexec` 기반 저장 도우미.
- 선택: Flatpak(파일 접근 권한 `--filesystem=host` 필요), deb/rpm.

### 3.3 macOS

- `macdeployqt`로 `.app` 번들, Universal 2(arm64 + x86_64).
- `Info.plist`: `CFBundleDocumentTypes`(열 수 있는 형식), 번들 ID, 버전.
- `.dmg` 생성(`create-dmg` 또는 `hdiutil`).
- 서명·공증: 외부 배포 시 Apple Developer 계정 필요. 개인 사용은 ad-hoc 서명(`codesign -s -`).

### 3.4 크래시 리포트

- 원본 `MISC/Exception`(Windows SEH, MiniDump)을 대체.
- 1차: 플랫폼별 최소 구현 — Windows `MiniDumpWriteDump`, Linux/macOS 시그널 핸들러 + 스택 로그. 덤프는 설정 폴더 `crash/`에 저장.
- 비정상 종료 감지 → 다음 시작 시 백업 복원 안내 (2단계 백업 기능과 연결).
- 2차(선택): crashpad 또는 sentry-native.

### 3.5 성능 목표

| 항목 | 목표 | 측정 방법 |
|---|---|---|
| 시작 시간 (세션 탭 20개) | 원본 Notepad++ 대비 1.5배 이내 | 실행 → 첫 화면 표시 시간 로그 |
| 500MB 텍스트 파일 열기 | 원본과 같은 방식으로 열림 (대용량 제한 설정 적용), 스크롤·검색 가능 | 수동 + 시간 로그 |
| 탭 200개 | 탭 전환 지연 체감 없음 | 수동 |
| 메모리 (빈 문서 1개) | 원본 대비 2배 이내 (Qt 기본 비용 고려) | OS 도구 |
| 파일에서 찾기 1만 파일 | 원본과 같은 수준 | 시간 측정 |

측정 결과와 원본 수치를 `docs/performance.md`에 기록한다.

### 3.6 릴리스 절차

1. 버전 규칙: `0.x.y` (베타 전 `0.`), 원본 기준 버전은 문서에 별도 표기.
2. `ci/release.sh` (로컬): 깨끗한 트리 확인 → 로컬 CI 전체 → 플랫폼별 패키지 생성 → 체크섬(SHA-256) → `dist/`에 결과.
3. git 태그 `v0.x.y` 생성 후 push.
4. GitHub Releases에 `gh release create`로 수동 업로드 (서버 빌드 없음).
5. 변경 기록 `docs/CHANGELOG.md`.

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P6-01 | Windows: windeployqt 정리, Inno Setup 스크립트, 포터블 zip | 1 |
| P6-02 | Windows: 파일 연결·탐색기 메뉴(`platform/win`), 관리자 권한 저장 | 1–2 |
| P6-03 | Linux: AppImage, `.desktop`/MIME/아이콘, 권한 저장 | 2 |
| P6-04 | (차후 구현 항목: macOS 패키징) | — |
| P6-05 | 크래시 리포트, 비정상 종료 후 복구 | 3 |
| P6-06 | 성능 측정·개선 | 3–4 |
| P6-07 | `ci/release.sh`, 릴리스 문서, 첫 베타 | 4 |

## 5. 테스트

- **깨끗한 환경 설치 테스트**: Windows 새 VM, Ubuntu/Fedora 새 컨테이너(GUI 포함) 또는 VM, macOS 다른 계정에서 설치 → 실행 → 파일 연결로 열기 → 제거.
- **업그레이드 테스트**: 이전 베타 설정이 유지되는지.
- **크래시 테스트**: 숨은 디버그 명령으로 강제 크래시 → 덤프 생성 → 재시작 시 복구 안내.
- **장시간 사용**: 1주일 일상 사용 (개발자 본인).

## 6. 완료 기준

- [ ] Windows·Linux 배포본이 깨끗한 환경에서 설치·실행·제거됨
- [ ] 성능 목표 충족 또는 차이 원인 문서화
- [ ] 크래시 덤프 생성과 복구 안내 동작
- [ ] `ci/release.sh`로 베타 릴리스 1회 수행

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| AppImage가 일부 배포판에서 실행 안 됨 (glibc, FUSE) | 대상 배포판 목록 명시, `--appimage-extract-and-run` 안내 |
| 서명 없는 배포본의 보안 경고 | 개인 사용 단계에서는 허용, 외부 배포 전 서명 계획 수립 |
| Wayland에서 권한 상승 도우미 UI | `pkexec` 표준 대화상자 사용 |

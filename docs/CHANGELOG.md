# Notepad++ Qt — 변경 기록

버전 규칙: `0.x.y` (베타 전 `0.`). 원본 기준 버전은 각 항목에 표기한다.
릴리스 절차는 [release.md](build/release.md), 빌드는 [build.md](build/build.md).

## [Unreleased]

## [0.2.1] - 2026-11-09

### 화면
- 환경설정 UI 언어 목록: 로케일 기준 정렬(한국어 등 비라틴 이름이 맨 끝으로 밀리지 않음), 스크롤되는 팝업(최대 20개 표시).
- 파일 > 현재 폴더 열기: OS별 이름(Windows 탐색기·cmd / macOS Finder·Terminal / Linux File Manager·Terminal),
  Windows 밖에서는 `pwsh`가 있을 때만 PowerShell 표시. 탭 메뉴·폴더 워크스페이스 패널도 같음.
  파일 관리자는 터미널과 같은 조건(제목 없는 문서는 홈 폴더), macOS Finder는 파일 선택 표시(`open -R`).
  Linux 터미널: `$TERMINAL` 우선, xfce4-terminal·kitty·alacritty 추가.
- 파일 대화상자 필터 `All types (*)`: Windows 밖에서 `*.*`가 숨기던 확장자 없는 파일(`/etc/hosts` 등) 표시.

### 저장
- 보호된 파일(`/etc/hosts` 등)을 원본처럼 편집 가능하게 열기: 읽기 전용은 읽기 전용 속성(POSIX: 쓰기 비트 없음)일 때만.
- 변경 없는 파일의 저장은 아무것도 하지 않음(권한 상승 요청 없음).
- Linux 권한 저장: GVfs `admin://`(gio-2.0) 사용 시 세션당 비밀번호 1회, 없으면 저장마다 `pkexec`.
- macOS 권한 저장: `osascript` 관리자 권한(저장마다 암호). macOS 빌드 미확인.

### 배포본
- Linux 앱 ID `io.github.asprenas.notepadpp_qt`(`.desktop`·metainfo 파일 이름, Wayland 앱 ID). metainfo 검증 경고 해결.
- AppImage 빌드 이미지에 `pkg-config`·`libglib2.0-dev`(GVfs 권한 저장 포함, 없으면 빌드 실패).
- `ci/package-linux.sh --local`: Docker 없이 Linux 호스트에서 AppImage. Docker 오류 안내를 OS·원인별로.

## [0.2.0] - 2026-11-05

원본 기준: Notepad++ 8.9.8.1. 대상: 0.1.0과 같음 (Windows 10/11 x64, Linux x86_64 glibc 2.35+).

### 플러그인 API (7단계)
- 새 플러그인 인터페이스 v1: C ABI(`app/plugin-api/include/npp_plugin_api.h`), Windows `.dll` / Linux `.so`.
  플러그인 폴더 `<앱>/plugins/<이름>/`, `<설정 폴더>/plugins/<이름>/` (Linux 설치본 `<prefix>/lib/notepadpp-qt/plugins`).
  `-noPlugin` 명령줄 옵션.
- 플러그인 메뉴(플러그인별 하위 메뉴, 체크 표시), 기본 단축키 + 단축키 매퍼 "플러그인 명령" 탭(`shortcuts.xml` `<PluginCommands>` 원본 형식),
  도킹 패널(QWidget), 툴바 버튼, `NPPN_*` 19종 / `SCN_*` 알림, 플러그인 간 메시지(`NPPM_MSGTOPLUGIN`).
- 원본 `NPPM_*` 메시지 76개 호환(`npp_compat.h`, `npp_compat_win32.h`): 인기 플러그인 10개 조사에서 상위 50개 중 44개 지원,
  6개(도킹·툴바 아이콘·메뉴 핸들)는 새 API로 대체 ([plugin-api-nppm.md](plugins/plugin-api-nppm.md)).
- 오류 격리: 로드 실패·API 버전 불일치·원본 Win32 플러그인·초기화 실패·C++ 예외는 해당 플러그인만 비활성화하고 알림(모달 아님).
- 샘플 플러그인: Text Case(C, UI 없음), Document Info(Qt 도킹 패널). 공개 플러그인 MIME Tools 3.2 이식.
  개발 문서 [plugin-development.md](plugins/plugin-development.md).
- 원본 Win32 플러그인 DLL 은 바이너리 호환되지 않는다 (소스 이식 필요).

## [0.1.0] - 2026-11-05

첫 베타. 원본 기준: Notepad++ 8.9.8.1 (이 저장소에서 병합한 upstream master).
대상: Windows 10/11 x64, Linux x86_64 (glibc 2.35 이상: Ubuntu 22.04+, Debian 12+, Fedora 36+).

### 배포본
- Windows: 설치본(Inno Setup, 현재 사용자 또는 모든 사용자, 탐색기 "Edit with Notepad++ Qt" 메뉴,
  파일 연결/기본 앱 등록 선택, 업그레이드 시 설정 유지, 제거 시 설정 삭제 여부 질문),
  포터블 zip(`doLocalConf.xml`: 설정을 프로그램 폴더에 저장), 크래시 덤프용 PDB zip.
- Linux: AppImage(Qt 6.12 포함, X11·Wayland), `.desktop`/MIME/아이콘 포함.
- 서명 없음: Windows SmartScreen 경고가 나올 수 있다.

### 기능 (1–5단계)
- 원본 메뉴 명령 480개 전부(기능 체크리스트 480/480), 설정 파일(config.xml, langs.xml, stylers.xml,
  shortcuts.xml, session.xml, contextMenu.xml, UDL) 원본과 양방향 호환.
- 검색/바꾸기(원본 Boost 정규식 엔진), 파일에서 찾기, 매크로, 실행, 자동 완성, 함수 목록(47개 파서),
  문서 지도, 프로젝트, 작업 공간 폴더, 문서 목록, 클립보드 기록, 문자 패널, UDL, 테마/다크 모드,
  번역(nativeLang), 인쇄, 트레이 아이콘.
- 도킹: Qt Advanced Docking System(기본) 또는 QDockWidget.

### 안정성 (6단계)
- 크래시 리포트: Windows minidump(`<설정 폴더>/crash/npp-*.dmp`), Linux 시그널 핸들러 + 백트레이스
  (`npp-*.log`).
- 비정상 종료 감지: 다음 시작 때 세션과 저장 안 한 문서(스냅숏 백업)를 복원하고 안내를 표시.
  스냅숏 모드에서는 백업 주기마다 session.xml 도 갱신한다.
- 보호된 파일 저장: 저장이 권한 때문에 실패하면 관리자 권한 도우미(UAC / pkexec)로 저장.

### 성능 (6단계, [performance.md](reference/performance.md))
- 대용량 파일 제한을 원본과 같게 적용: 스타일 없는 문서, 구문 강조·괄호 강조·스마트 강조 끔,
  줄 바꿈 끔, 스냅숏 백업 안 함. 500MB 파일이 1.5초에 열림(이전: 끝없이 사용 중).
- 탭 200개: 탭 전환 362ms → 19ms, 열기 75초 → 4.4초.
- 파일에서 찾기 1만 파일: 2.0초 → 1.2초. 세션 복원·시작 시 테마 적용 시간 단축.

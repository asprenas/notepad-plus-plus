# Notepad++ Qt — 변경 기록

버전 규칙: `0.x.y` (베타 전 `0.`). 원본 기준 버전은 각 항목에 표기한다.
릴리스 절차는 [release.md](release.md), 빌드는 [build.md](build.md).

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

### 성능 (6단계, [performance.md](performance.md))
- 대용량 파일 제한을 원본과 같게 적용: 스타일 없는 문서, 구문 강조·괄호 강조·스마트 강조 끔,
  줄 바꿈 끔, 스냅숏 백업 안 함. 500MB 파일이 1.5초에 열림(이전: 끝없이 사용 중).
- 탭 200개: 탭 전환 362ms → 19ms, 열기 75초 → 4.4초.
- 파일에서 찾기 1만 파일: 2.0초 → 1.2초. 세션 복원·시작 시 테마 적용 시간 단축.

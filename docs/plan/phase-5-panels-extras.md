# 5단계 세부 계획: 패널 / 부가 기능

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장
- 기간: 4–6주
- 선행: [4단계 설정/테마](phase-4-settings-theme.md) 완료
- 다음 단계: [6단계 패키징/안정화](phase-6-packaging-stabilization.md)

---

## 1. 목표

도킹 패널과 나머지 다이얼로그·도구를 이식해 **기능 체크리스트 90% 이상**을 달성한다.
모든 패널은 `DockManager`를 통해서만 도킹한다 (ADS 직접 사용 금지, [부록 B.4](qt-cross-platform-port-plan.md)).

## 2. 범위

### 2.1 도킹 패널

| 패널 | 원본 | 규모 | 패널 ID |
|---|---|---|---|
| Function List | `FunctionList/` + `installer/functionList/*.xml` 48개 | 2.1k | `npp.functionList` |
| Document Map | `DocumentMap/` | 1.1k | `npp.documentMap` |
| Folder as Workspace | `FileBrowser/` | 2.2k | `npp.folderAsWorkspace` |
| Project Panel 1–3 | `ProjectPanel/` | 1.7k | `npp.project1`–`3` |
| Document List | `VerticalFileSwitcher/` | 1.5k | `npp.documentList` |
| Character Panel (ASCII 표) | `AnsiCharPanel/` | 1.2k | `npp.charPanel` |
| Clipboard History | `ClipboardHistory/` | 0.5k | `npp.clipboardHistory` |
| 검색 결과 | 3단계에서 완료 | — | `npp.searchResults` |

### 2.2 다이얼로그·도구

| 기능 | 원본 | 규모 |
|---|---|---|
| 사용자 정의 언어(UDL) 다이얼로그 | `UserDefineDialog.*` | 2.1k |
| 인쇄, 인쇄 미리보기 | `Printer.*` | 0.5k |
| 창 목록(Windows…) 다이얼로그 | `WindowsDlg/` | 2.6k |
| 문서 전환기 (Ctrl+Tab) | `TaskList/` | 0.7k |
| 실행(Run) 다이얼로그, 사용자 명령 | `RunDlg/` | 0.5k |
| 해시 도구 (MD5, SHA-1, SHA-256, SHA-512: 텍스트/파일) | `HashFromTextDlg`, `HashFromFilesDlg`, `MISC/md5` 등 | 2k |
| 정보, 디버그 정보, 명령줄 인자 | `AboutDlg/` | 1.7k |
| 날짜/시간 삽입, 기타 `IDM_EDIT_*` 잔여 | | — |
| 트레이 아이콘 (최소화 시) | `TrayIcon/` | 0.1k |

## 3. 설계

### 3.1 Function List

- 파서 정의(`functionList/*.xml`, 언어별 정규식 기반)를 코어로 이식 → 같은 boostregex로 실행.
- 트리 표시(`QTreeView`), 정렬, 필터, 클릭 시 이동, 문서 변경 시 지연 갱신(백그라운드).
- **기존 테스트 재사용**: `PowerEditor/Test/FunctionList` 45개 언어 테스트(입력 + `unitTest.expected.result`)를 CTest로 실행 → 결과 비교.

### 3.2 Document Map

- 축소된 두 번째 Scintilla 뷰(같은 문서 공유, 확대율 최소, 편집 불가) + 현재 보이는 영역 표시 오버레이.
- 클릭/드래그로 스크롤. 줄 바꿈·접기 상태 동기화.

### 3.3 Folder as Workspace / Project Panel

- `QFileSystemModel` 기반 트리, 여러 루트 폴더, 세션 저장(`FileBrowserRootsInfo`).
- 프로젝트 패널: 원본 `.xml` 프로젝트 파일 형식 그대로 (가상 폴더, 파일 추가/삭제/이동).
- 파일 작업(새 파일, 이름 변경, 휴지통으로 삭제 `QFile::moveToTrash`, 탐색기에서 열기 → 플랫폼별 `QDesktopServices`/OS 명령).

### 3.4 UDL 다이얼로그

- 탭 4개(폴더·기본, 키워드 목록 8그룹, 주석·숫자, 연산자·구분자) + 스타일 지정 하위 다이얼로그.
- 저장 형식: `userDefineLang.xml` 및 `userDefineLangs/*.xml` (원본 그대로, 가져오기/내보내기).
- 렉서는 Lexilla `user`(Npp UDL 렉서, 0단계에서 링크 확인)에 속성으로 전달.
- 도킹 가능 다이얼로그(원본도 도킹 가능) → `DockManager` 패널로.

### 3.5 인쇄

- `QPrinter` + `QPrintDialog` + `QPrintPreviewDialog`.
- Scintilla `SCI_FORMATRANGEFULL`로 페이지 렌더링 (Qt 레이어의 `PlatQt` 그리기 사용).
- 머리글/바닥글 변수(파일명, 날짜, 페이지 번호), 색 모드(그대로/반전/흑백/배경 없음), 줄 번호 — 원본 `PrintSettings`.

### 3.6 문서 전환기·창 목록

- Ctrl+Tab: 최근 사용 순서(MRU) 목록 팝업 (원본 `TaskList`). 설정에 따라 MRU/탭 순서.
- 창 목록 다이얼로그: 열린 문서 표(이름, 경로, 형식, 크기), 정렬, 활성화/저장/닫기.

### 3.7 실행 명령

- `$(FULL_CURRENT_PATH)`, `$(CURRENT_WORD)` 등 원본 변수 치환을 코어로.
- 실행은 `QProcess::startDetached`. Windows `cmd /c`, Linux/macOS `sh -c` 차이는 플랫폼 함수로.
- 저장: `shortcuts.xml` `<UserDefinedCommands>`.

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P5-01 | Function List + 45개 언어 테스트 편입 | 1–2 |
| P5-02 | Document Map | 2 |
| P5-03 | Folder as Workspace, Project Panel | 2–3 |
| P5-04 | Document List, Character Panel, Clipboard History | 3 |
| P5-05 | UDL 다이얼로그 | 4 |
| P5-06 | 인쇄, 미리보기 | 4–5 |
| P5-07 | 문서 전환기, 창 목록, 실행, 해시 도구, 정보 | 5 |
| P5-08 | 패널 배치 저장(`config.xml` `DockingManager` 항목과 대응), 체크리스트 점검 | 6 |

### 4.1 패널 배치 저장

원본 `config.xml`의 `<GUIConfig name="DockingManager">`(`FloatingWindow`, `PluginDlg`, `ActiveTabs`)와 이 앱의 `DockLayout`(패널 ID, 표시 여부, 영역)을 대응시킨다.
ADS 고유 상태는 별도 파일에 보조 저장 (부록 B.4 규칙 4).

## 5. 테스트

- Function List: 기존 45개 언어 테스트 100% 통과.
- UDL: 원본 `userDefineLangs` 및 커뮤니티 UDL 파일 10개 이상 가져오기 → 강조 결과 스크린샷 확인.
- 인쇄: PDF 프린터로 출력해 페이지 수·머리글 확인 (자동).
- 패널: 각 패널 열기/닫기/도킹 위치 변경/재시작 복원을 ADS와 QT 백엔드 모두에서 확인.

## 6. 완료 기준

- [x] 기능 체크리스트 90% 이상 완료 (제외 항목은 사유 기록) — [feature-checklist.md](../reference/feature-checklist.md) 전체 480/480(100%), 5단계 52/52. 생성 스크립트가 기본 컨텍스트 메뉴 표의 ID까지 "구현"으로 세던 문제를 고쳐 실제 핸들러만 센다(이 수정으로 드러난 인쇄 미구현을 이 단계에서 구현). 원본과 동작이 다른 항목은 8장 "계획과 다르게 한 점"에 사유 기록(업데이트·프록시: 자동 업데이트 없음)
- [x] Function List 테스트 100% — 원본 `PowerEditor/Test/FunctionList`의 언어 폴더·하위 폴더 47개 사례 전부 통과(`core.funclist`, 결과 JSON을 원본 `unitTest.ps1`과 같은 방식으로 비교). 계획서의 "45개"는 폴더 수 기준이었고 하위 폴더 사례까지 모두 포함
- [x] 모든 패널이 두 도킹 백엔드에서 동작, 도킹 추상화 검사 통과 — `ui.panels`(23개 테스트)가 ADS와 QDockWidget 빌드 양쪽에서 통과. `allPanelsOpenMoveCloseAndRestore`: 10개 패널 모두 열기/닫기/위치 변경/재시작 복원, 백엔드 상태 파일 없이 `config.xml`만으로 복원. `ci/check-docking-abstraction.sh` 통과
- [x] 로컬 CI 통과 — Windows MinGW/MSVC, Linux Docker × ADS/QT (`ci/local-ci.sh`)

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| Function List 파서가 대형 파일에서 느림 | 지연 갱신(편집 후 1초), 패널이 보일 때만 파싱. 원본 Notepad_plus.cpp(약 9천 줄) 디버그 빌드 0.8초(`core.funclist` `largeFileParseTime`, 3초 상한) |
| Document Map 동기화 버그 | 원본 동작을 단계별로 이식, 접기/줄바꿈 시나리오 테스트 |
| 인쇄 결과의 플랫폼 차이 | PDF 출력 비교로 확인, 차이는 문서화 |

## 8. 진행 현황

| ID | 상태 | 구현 위치 / 비고 |
|---|---|---|
| P5-01 | 완료 | `core/funclist/FunctionParser`(원본 `functionParser.cpp`의 unit/zone/mix 파서와 `overrideMap.xml`을 `IEditor` 위로 1:1 이식, 같은 Boost.Regex 엔진), `ui/panels/FunctionListPanel`(클래스/함수 트리, 필터, 정렬, "기본으로 정렬" 설정 `MISC sortFunctionList`, 문서별 트리 상태, 편집 후 갱신, 현재 함수 표시). 규칙 파일은 `data/functionList/`(설치본) → 사용자 `functionList/` 순 |
| P5-02 | 완료 | `ui/panels/DocumentMapPanel`(같은 문서를 공유하는 축소 Scintilla 뷰, 보이는 영역 오버레이, 클릭·드래그·휠 스크롤, 줄 바꿈·접기 동기화, 편집기 스타일 복사, 입력 차단) |
| P5-03 | 완료 | `core/project/Workspace`(원본 워크스페이스 XML, 상대 경로), `ui/panels/ProjectPanel`(프로젝트 패널 1–3: 가상 폴더, 파일 추가·폴더에서 추가, 이동, 원본 규칙의 끌어 놓기, 종료 시 저장 확인, `config.xml <ProjectPanels>`), `ui/panels/FolderAsWorkspacePanel`(여러 루트, 지연 목록, 파일 시스템 감시, 새 파일/폴더·이름 변경·휴지통 삭제·경로 복사·탐색기/터미널, `config.xml <FileBrowser>` 저장·복원). Find in Projects 탭(`fifProjectPanel1-3`) |
| P5-04 | 완료 | `ui/panels/DocumentListPanel`(이름/확장자/경로 열·보기별 그룹 `MISC fileSwitcher*`, 정렬, 선택 닫기·저장, 가운데 클릭), `ui/panels/SmallPanels`(문자 패널: 원본 ASCII/ANSI 표와 HTML 이름·10진·16진 엔티티 — 이름 표는 `tools/gen-html-char-names.py`로 원본에서 생성 —, 문서 코드 페이지로 삽입 / 클립보드 기록) |
| P5-05 | 완료 | `ui/panels/UdlPanel`(도킹 패널: 만들기·다른 이름으로·가져오기·내보내기·이름 변경·삭제, 4개 탭과 Styler 대화상자, 원본 주석·구분자 목록 인코딩 `udlEncodeFields`/`udlFieldOf`), `UserLanguageRegistry::saveAll`(원래 파일로 다시 쓰기, 설치본 UDL은 사용자 `userDefineLangs/`로), Language 메뉴의 UDL 목록(이전 단계에서 빠져 있던 것), `config.xml UserDefineDlg` |
| P5-06 | 완료 | `ui/PrintCommands.cpp`: `QPrinter`에 Scintilla `SCI_FORMATRANGEFULL`(Qt 플랫폼 계층이 장치의 활성 `QPainter` 사용), 머리글/바닥글 변수, 색 모드, 줄 번호, 여백, 폼 피드 페이지 나누기, 선택/페이지 범위, 인쇄 미리보기. PDF 출력 테스트(`printToPdf`: 페이지 수, 선택, 범위) |
| P5-07 | 완료 | `ui/WindowCommands.cpp`(Window 메뉴: 정렬 10종, Windows... 대화상자, 열린 문서 목록; Ctrl+Tab 문서 전환기 MRU/탭 순서 `TaskList`/`MRU`), `ui/RunCommands.cpp` + `core/run/RunCommand`(Run 대화상자, `$(VARIABLE)` 치환, 사용자 명령 메뉴), `ui/ToolCommands.cpp` + `core/util/Hash`(MD5/SHA-1/SHA-256/SHA-512 텍스트·파일·선택, 요약, 모니터링(tail -f), 명령줄 인자, 디버그 정보, `shortcuts.xml` 무결성 검사), `ui/TrayCommands.cpp`(트레이 아이콘 `TrayIcon` 0–3, `-systemtray`) |
| P5-08 | 완료 | 패널 배치: 정확한 상태는 백엔드 파일(`dockLayout-<backend>.bin`), 백엔드 무관 배치는 원본 `config.xml <GUIConfig name="DockingManager">`의 `PluginDlg pluginName="Notepad++::InternalFunction" id=<원본 명령 ID>`와 컨테이너 번호(0 왼쪽 1 오른쪽 2 위 3 아래 4+ 떠 있음)로 저장, 상태 파일이 없을 때 적용. 플러그인 항목은 보존. ADS 백엔드가 사용자가 옮긴 실제 위치를 보고하도록 수정. 체크리스트 점검(6장) |

테스트: `core.funclist`(원본 47개 사례 + JSON 이스케이프 + 대형 파일 시간), `core.config`(ProjectPanels, FileBrowser, 워크스페이스 파일), `core.lang`(UDL 목록 인코딩, 설치본 UDL 왕복, 레지스트리 편집·저장), `core.util`(해시 시험 벡터, Run 변수), `ui.panels`(패널·대화상자 23개), `ui.panels.udlscreenshots`(Windows 실제 플랫폼에서 UDL 11개 강조 스크린샷, `bin/udl-screenshots/`).

교차 확인으로 고친 점:
- 원본 Function List 테스트는 Windows 체크아웃(`core.autocrlf`)의 CRLF 문서에서 만들어졌다. bash 규칙은 함수 본문이 주석 영역 바로 앞에서 끝날 때 EOL 길이에 따라 결과가 달라서, 테스트는 문서를 CRLF로 바꿔 실행한다(LF 그대로면 bash 1개 사례만 다름).
- 메뉴 생성기가 `IDR_WINDOWS_MENU` 기반 ID를 풀지 못해 Window 메뉴가 비어 있었다(정렬·창 목록 누락). 생성기 수정.
- 체크리스트 생성기가 기본 컨텍스트 메뉴 표에 적힌 ID를 구현으로 셌다(인쇄가 구현된 것으로 표시됨).
- Language 메뉴에 UDL 목록이 들어가지 않고 있었다(액션만 만들어지고 메뉴에 없음).
- 병합한 원본 master에 생긴 Structured Text(`L_STTXT`)가 언어 표에 없어 메뉴 1개가 미구현이었다. 언어·렉서 표를 이 저장소의 원본 소스에서 다시 생성.
- QDockWidget 백엔드에서 숨겨진 떠 있는 패널이 있으면 검색 결과 패널을 열 때 수 초 멈췄다(`ui.find.responsiveness`). UDL 패널 기본 위치를 원본 기본값(docked)과 같은 아래쪽으로.

계획과 다르게 한 점:
- Function List 파싱은 백그라운드 스레드 대신 UI 스레드에서 지연 실행한다. Scintilla 문서는 스레드 안전하지 않아 복사가 필요하고, 측정상 큰 파일도 1초 미만이라 패널이 보일 때만, 편집이 1초 멈춘 뒤에 파싱한다.
- UDL 가져오기 시험의 "커뮤니티 UDL 파일"은 라이선스가 제각각이라 저장소에 넣지 않고, 기능별로 직접 만든 UDL 9개(`app/tests/data/udl/`)와 설치본 UDL 2개로 대신했다.
- 인쇄 미리보기는 원본에 없는 기능이라 File 메뉴의 "인쇄" 다음에 항목을 하나 추가했다(원본 메뉴 ID 없음).
- "Update Notepad++"는 릴리스 페이지를 열고, "Set Updater Proxy..."는 내장 업데이트가 없다는 안내만 한다(원본은 WinGup 실행. 배포 방식은 6단계).
- `shortcuts.xml` 무결성 검사는 원본과 같은 HMAC-SHA256(키: 머신 GUID)이다. Windows에서는 원본과 같은 레지스트리 값이라 원본이 `config.xml`에 쓴 `shortcutsXmlHMAC`이 그대로 맞는다(설정 호환 실행의 원본 값과 대조, `shortcutsXmlIntegrity`). Linux/macOS는 `QSysInfo::machineUniqueId()`.
- 트레이가 없는 데스크톱에서는 창을 숨기지 않고 최소화만 한다(돌아올 방법이 없어지므로).

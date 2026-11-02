# Notepad++ Qt 크로스플랫폼 포팅 개발 계획서 (초안 v2)

- 작성일: 2026-11-02 (v1: Linux 포팅 초안 → v2: Windows/Linux/macOS Qt 통합으로 개정)
- 대상 소스: Notepad++ v8.9.8.1 (`PowerEditor/src/resource.h` 기준)
- 프로젝트 성격: **개인 프로젝트 (독립 포크)** — 업스트림(notepad-plus-plus) 기여 목표 없음
- 목표 플랫폼: Windows, Linux (즉시) / macOS (2026-11-09 주 추가 예정)
- 상태: 초안 v2.1 — 주요 결정 사항 확정 (2026-11-02, 4.2절)

---

## 1. 목표 및 범위

### 1.1 목표
1. Windows / Linux / macOS 모두 **단일 Qt 6 코드베이스**로 빌드되는 Notepad++ 계열 편집기를 만든다. 기존 Win32 UI는 유지하지 않는다.
2. 설정 파일(XML: `config.xml`, `langs.xml`, `stylers.xml`, `shortcuts.xml`, `session.xml`, UDL, 테마, 다국어 파일) **호환성 유지** — 기존 Notepad++ 사용자 설정을 그대로 가져와 쓸 수 있도록 한다.
3. 기존 코드의 편집 로직(Scintilla 제어, 버퍼/인코딩, 검색, 설정 모델)은 최대한 재사용하고 UI만 새로 작성한다.

### 1.2 범위 외 (1차 릴리스 기준)
- 기존 Win32 DLL 플러그인 바이너리 호환 (구조적으로 불가 — 7장 참조)
- Windows 쉘 확장(Explorer 컨텍스트 메뉴), 기존 NSIS/MSI 인스톨러 스크립트
- Windows 전용 다크모드 구현(`DarkMode/`, `NppDarkMode.cpp`) — Qt 스타일/팔레트로 대체
- 플러그인 관리자(`PluginsAdmin`)의 원격 설치 기능
- 업스트림 Notepad++와의 지속적 코드 동기화 (필요 시 기능 단위 수동 반영)

---

## 2. 현재 소스 분석

### 2.1 디렉터리 구성

| 경로 | 내용 | 비고 |
|---|---|---|
| `scintilla/` | Scintilla 5.x 편집 엔진 | `win32/`, `gtk/`, `qt/`, `cocoa/` 플랫폼 레이어 **이미 포함** |
| `lexilla/` | Lexilla 문법 강조 렉서 | 플랫폼 독립, Qt `.pro` 파일 존재 |
| `boostregex/` | Boost.Regex 기반 Scintilla 검색 확장 | 플랫폼 독립 (Scintilla `RegexSearchBase` 구현) |
| `PowerEditor/src/` | Notepad++ 본체 | 대부분 Win32 API 직접 사용 |
| `PowerEditor/src/pugixml`, `uchardet`, `json` | 서드파티 (XML, 인코딩 감지, JSON) | 플랫폼 독립. pugixml은 char(UTF-8) 모드로 빌드됨 |
| `PowerEditor/installer/` | NSIS, MSI, 다국어 XML(94개), APIs, functionList | 데이터 파일은 재사용 |
| `PowerEditor/visual.net/`, `gcc/`, `src/CMakeLists.txt` | MSVC / MinGW / CMake 빌드 | 신규 CMake로 대체 |
| `PowerEditor/Test/` | FunctionList, UrlDetection, xmlValidator 테스트 | 회귀 테스트 자산으로 활용 |

### 2.2 규모 (주석 포함 `.cpp/.h` 라인 수)

| 모듈 | LOC | 주요 파일 |
|---|---|---|
| 최상위 (`src/*.cpp/h`) | 약 48,400 | `Notepad_plus.cpp`(9.5k), `Parameters.cpp`(9.5k), `NppDarkMode.cpp`(5k), `NppCommands.cpp`(4.6k), `NppBigSwitch.cpp`(4.5k), `NppIO.cpp`(3k) |
| `ScintillaComponent/` | 약 25,000 | `FindReplaceDlg.cpp`(7.1k), `ScintillaEditView.cpp`(5k), `Buffer.cpp`(2.3k), `UserDefineDialog.cpp`(2.1k) |
| `WinControls/` | 약 51,000 | `Preference`(8.3k), `Grid`(5.1k), `DockingWnd`(4.8k), `WindowsDlg`(2.6k), `TabBar`(2.4k) 등 30개 컨트롤 |
| `MISC/` | 약 11,000 | `Common`(5.4k), `PluginsManager`(2.4k), 해시(md5/sha*) |
| `DarkMode/` | 약 1,000 | Win32 UxTheme 후킹 |

포팅 대상(서드파티 제외) 약 **135,000 LOC**. 이 중 `WinControls/`, `DarkMode/`, `NppDarkMode.cpp` 등 UI 전용 약 60k LOC는 Qt로 재작성되어 폐기된다.

### 2.3 Win32 결합도 지표 (서드파티 제외, grep 기준)

| 지표 | 건수 | 의미 |
|---|---|---|
| `#include <windows.h>` 포함 파일 | 89 | 거의 전 파일이 Win32 헤더 의존 |
| `HWND` | 975 | 윈도우 핸들 기반 설계 |
| `WM_*` 메시지 | 1,473 | 메시지 루프/서브클래싱 기반 이벤트 처리 |
| `SendMessage` | 1,179 | 컨트롤 간 통신이 메시지 기반 |
| `execute(SCI_...)` | 1,843 | Scintilla 호출 — 래퍼 경유라 Qt 이식 용이 |
| `IDD_/IDC_` 리소스 ID | 3,824 | `.rc` 다이얼로그 리소스(25개 `.rc`, 메인 `.rc` 1,500줄) |
| GDI (`HDC`/`HBRUSH`/`HFONT`) | 454 | 커스텀 드로잉 (탭바, 그리드, 도킹, 다크모드) |
| `LoadLibrary/GetProcAddress` | 41 | 플러그인 및 동적 API 로딩 |
| Registry 접근 | 14 | 파일 연결, 일부 설정 |

문자열 관련 지표는 부록 A 참조.

### 2.4 아키텍처 특징
- **UI 클래스 계층**: `Window`(HWND 래퍼, `WinControls/Window.h`) ← `StaticDialog`(57개 파생, `.rc` 다이얼로그) / `DockingDlgInterface`(8개 파생, 도킹 패널).
- **명령 처리**: `NppBigSwitch.cpp`의 `Notepad_plus::process()`가 거대한 `WM_*`/`NPPM_*` 스위치. 메뉴 명령은 `NppCommands.cpp`의 `command(int id)`에서 `IDM_*` 기준 분기.
- **에디터**: `ScintillaEditView`가 Scintilla를 `execute(SCI_*, wParam, lParam)` 형태로 감쌈 — Qt의 `ScintillaEditBase::send()`와 시그니처가 동형이므로 이식 비용이 낮음.
- **문서 모델**: `Buffer` / `FileManager`(`ScintillaComponent/Buffer.*`)가 파일 상태, 인코딩, 백업, 변경 감지를 담당. 파일 I/O는 `Win32_IO_File`(`MISC/Common/FileInterface.*`) 및 `GetFileAttributesEx`, `MoveFileEx` 등 Win32 API 사용.
- **설정**: `NppParameters`(`Parameters.cpp`) 싱글톤이 XML(pugixml) 로드/저장. 경로는 `CSIDL_APPDATA`, `doLocalConf.xml`, 클라우드 경로 로직 포함.
- **플러그인 API**: `PluginInterface.h` + `Notepad_plus_msgs.h`(NPPM_* 253개 메시지) — HWND와 `SendMessage` 기반 C ABI.
- **다국어**: `localization.cpp`가 `nativeLang/*.xml`을 읽어 메뉴/다이얼로그 컨트롤 텍스트를 ID 기준으로 치환.

---

## 3. 포팅 전략

### 3.1 기본 방침
**코어 추출 + Qt UI 신규 작성**, 모든 플랫폼이 동일한 Qt 앱을 빌드한다.

- Windows도 Qt로 전환하므로 **Win32 UI 빌드를 유지할 필요가 없다.** 따라서 기존 `PowerEditor/src`를 점진 수정하는 대신, 새 트리(`app/`)에 코어·UI를 구성하면서 기존 코드를 **옮겨 오며 정리**하는 방식으로 진행한다. 기존 `PowerEditor/src`는 기능 동등성 확보 시까지 참조용으로만 두고 이후 삭제한다.
- 개인 포크이므로 업스트림 머지 용이성을 고려한 "최소 수정" 제약이 없다. 구조 개선(거대 스위치 분해, 전역 싱글톤 축소 등)을 포팅과 함께 진행할 수 있다.
- 모든 플랫폼이 Qt를 사용하므로 **코어에서 QtCore 사용을 허용**한다. 파일 감시, 표준 경로, 원자적 저장, 단일 인스턴스, 프로세스 실행 등 OS 서비스는 QtCore(`QFileSystemWatcher`, `QStandardPaths`, `QSaveFile`, `QLocalServer`, `QProcess`)로 처리하고, 별도의 win32/posix 추상화 계층은 Qt가 제공하지 않는 기능(파일 연결, 쉘 연동 등)에 한해 얇게 둔다.

### 3.2 계층 구조

```
+------------------------------------------------------------------+
|                  npp-ui (Qt 6 Widgets)                           |
|  MainWindow, TabBar, DockManager(ADS), Dialogs, Menus, StatusBar |
+------------------------------------------------------------------+
|                  npp-core (C++20 + QtCore)                       |
|  설정 XML 모델, Buffer/FileManager, 인코딩 변환, UDL,              |
|  FunctionList 파서, 단축키 모델, 세션, 검색 로직, 다국어 테이블     |
+------------------------------------------------------------------+
|  platform (얇은 계층): Windows 파일 연결/점프리스트,                |
|  macOS 파일 열기 이벤트·Dock 메뉴, Linux .desktop/MIME             |
+------------------------------------------------------------------+
| Scintilla(qt/ScintillaEditBase) | Lexilla | boostregex | pugixml | uchardet |
+------------------------------------------------------------------+
```

원칙:
- 코어는 QtWidgets/QtGui에 의존하지 않는다 (QtCore만). 헤드리스 단위 테스트 가능해야 한다.
- Scintilla 호출은 `sptr_t send(unsigned int msg, uptr_t w, sptr_t l)` 인터페이스(`IEditor`)로 추상화하여 `ScintillaEditView`의 로직을 코어에서 재사용한다.
- 기존 `IDM_*` 명령 ID 체계는 `QAction` 레지스트리의 키로 유지하여 `shortcuts.xml`·다국어 파일과 호환시킨다.

### 3.3 참고 프로젝트
- **NotepadNext** (Qt + Scintilla 기반 Notepad++ 재구현, GPL-3.0): Scintilla Qt 통합, 도킹(ADS 사용), Windows/Linux/macOS 배포 방식 참고.
- **Notepadqq**: 유사 목표의 과거 프로젝트(비활성). 설계 참고용.

---

## 4. 기술 스택 및 결정 사항

### 4.1 확정

| 항목 | 결정 | 근거 |
|---|---|---|
| UI 프레임워크 | Qt 6.5+ LTS (권장 6.8 LTS), Qt Widgets — **전 플랫폼** | 2026-11-02 결정: Windows도 Qt 전환 |
| 대상 플랫폼 | Windows 10/11 x64, Linux x86_64 (기준: Ubuntu 26.04 / Qt 6.10 이상 배포판), macOS (arm64 + x86_64) | macOS는 2026-11-09 주 추가. Ubuntu 24.04는 Qt 6.4라 패키지 Qt로는 미지원 |
| 프로젝트 성격 | 개인 독립 포크 | 2026-11-02 결정 |
| 편집 엔진 | 동봉된 `scintilla/qt/ScintillaEditBase` | Notepad++와 동일 Scintilla 버전 유지 |
| 빌드 | CMake 3.24+ 단일 루트 `CMakeLists.txt` + CMake Presets | Qt 6 공식 지원, 3플랫폼 공통 |
| 의존성 관리 | vcpkg (manifest 모드) 또는 시스템 패키지 + Qt 공식 설치본 | Boost 헤더, ADS(Qt Advanced Docking System) |
| 언어 표준 | C++20 | |
| 컴파일러 | MSVC 2022 또는 MinGW 13 (Windows), GCC 13+/Clang 17+ (Linux), Apple Clang (macOS) | 로컬 CI에서 MinGW 13.1과 MSVC 19.51(VS 2026, Qt `msvc2022_64` kit) 둘 다 검증 |
| 정규식 | 기존 `boostregex` 유지 | 검색 동작 동일성 확보 |
| 패키징 | Windows: 설치본(Qt IFW/Inno Setup/WiX 중 택1) + 포터블 zip, Linux: AppImage → Flatpak/deb/rpm, macOS: `.app` + `.dmg` | |
| CI | **로컬 전용** `ci/local-ci.sh` (Windows MinGW·MSVC + Linux Docker, ADS/QT 백엔드). GitHub Actions 사용 안 함 | 2026-11-02 결정: GitHub은 저장소로만 사용 |

### 4.2 추가 확정 사항 (2026-11-02, 판단 자료: 부록 A, B)

| 항목 | 결정 | 적용 |
|---|---|---|
| 내부 문자열 | **UTF-8 `std::string`** 으로 전환 | 1단계(코어 추출)에서 동시 진행. 규칙은 부록 A.4 |
| 도킹 | **`DockManager` 추상화 계층 + ADS 구현** | 구현은 언제든 교체 가능해야 함 (QDockWidget/KDDockWidgets 등). 설계 규칙은 부록 B.4 |
| 포크 명칭/설정 폴더 | **현 단계에서는 정하지 않음** | 로컬 개인 사용 단계. 설정 폴더는 기존 Notepad++ 경로 규칙을 그대로 사용. 외부 배포 시점에 재검토 |

---

## 5. 모듈별 포팅 매핑

### 5.1 그대로 재사용 (수정 없음 또는 빌드 설정만)
- `scintilla/` (qt 플랫폼 레이어 사용), `lexilla/`, `boostregex/`
- `pugixml/`, `uchardet/`, `json/`
- `MISC/md5`, `sha1`, `sha2`, `sha512`, `crc16`, `hmac`
- 데이터: `langs.model.xml`, `stylers.model.xml`, `installer/nativeLang/*.xml`, `installer/APIs`, `installer/functionList`, `bin/userDefineLangs`, 테마 파일

### 5.2 코어로 이전 (Win32 의존 제거)

| 대상 | 작업 내용 |
|---|---|
| `Parameters.cpp/h` | 경로 결정을 `QStandardPaths`로(Windows `%APPDATA%\Notepad++`, Linux `~/.config/notepad++`, macOS `~/Library/Application Support/Notepad++`), Registry/`HWND`/GDI(`HFONT`, `COLORREF`) 의존 분리. 데이터 모델과 XML 직렬화만 코어에 남김 |
| `ScintillaComponent/Buffer.*` | `Win32_IO_File`, `GetFileAttributesEx`, `MoveFileEx`, 타임아웃 스레드를 `QFile`/`QFileInfo`/`QSaveFile`/`std::filesystem`으로 교체 |
| `Utf8_16.*`, `EncodingMapper.*` | 코드페이지 변환(`MultiByteToWideChar`)을 ICU 또는 `QStringConverter`(Qt 6.x의 ICU 백엔드) 기반으로 교체 |
| `MISC/Common/Common.*` | 문자열 유틸·경로 함수 정리, Win32 전용 함수 제거 |
| `ScintillaComponent/ScintillaEditView.*` | `execute()` 호출부를 `IEditor`로 일반화, 렉서·스타일 적용 로직은 코어로 |
| UDL 모델, `FunctionList` 파서, `AutoCompletion`, `FunctionCallTip`, `SmartHighlighter`, `xmlMatchedTagsHighlighter` | 로직부와 UI부 분리 후 로직부 코어 편입 |
| `FindReplaceDlg.cpp` | 검색 엔진/결과 모델(Find in Files 포함)과 다이얼로그 UI 분리 |
| `lastRecentFileList`, 세션 로드/저장, `shortcut` 모델 | 데이터 모델만 코어로 |
| `localization.cpp` | XML 파싱/문자열 테이블을 코어로, 컨트롤 텍스트 적용은 UI 계층에서 |

### 5.3 Qt로 신규 작성 (UI)

| Win32 구성요소 | Qt 대응 | 우선순위 |
|---|---|---|
| `Notepad_plus_Window`, `winmain.cpp` | `QApplication`, `QMainWindow`, 단일 인스턴스(`QLocalServer`/`QLocalSocket`) | P0 |
| 메뉴(`Notepad_plus.rc`), `ContextMenu` | `QMenuBar`/`QMenu`/`QAction`, `IDM_*` → 액션 레지스트리 | P0 |
| `DocTabView`, `TabBar` | `QTabBar` 커스터마이징(닫기 버튼, 드래그, 색상 탭) | P0 |
| `SplitterContainer` (다중 뷰) | `QSplitter` | P0 |
| `StatusBar` | `QStatusBar` | P0 |
| `ToolBar`, `ImageListSet` | `QToolBar`, 아이콘 리소스(`.qrc`), `toolbarButtonsConf` 지원 | P0 |
| `OpenSaveFileDialog` | `QFileDialog` (네이티브 다이얼로그) | P0 |
| `ReadDirectoryChanges`, 파일 변경 감지 | `QFileSystemWatcher` | P0 |
| `dpiManagerV2` | Qt 고DPI 자동 처리 (코드 제거) | P0 |
| `FindReplaceDlg`, `GoToLineDlg` | `QDialog` + 검색 결과 패널 | P0 |
| `Preference` (8.3k LOC) | `QDialog` + 페이지별 위젯 (`QStackedWidget`), `.ui` 파일 | P1 |
| `WordStyleDlg`(스타일 설정), `ColourPicker` | `QColorDialog` + 커스텀 위젯 | P1 |
| `shortcut` 매퍼, `Grid` | `QTableView` + 모델 | P1 |
| `DockingWnd` | `DockManager` 인터페이스 + ADS 구현 (부록 B.4) | P1 |
| `NppDarkMode`, `DarkMode/` | `QPalette` + `QStyle`(Fusion)/스타일시트, 시스템 테마 감지(`QStyleHints::colorScheme`) | P1 |
| `FunctionList`, `DocumentMap`, `FileBrowser`, `ProjectPanel`, `VerticalFileSwitcher`, `AnsiCharPanel`, `ClipboardHistory` | 도킹 패널, `QTreeView`/`QListView` | P1–P2 |
| `UserDefineDialog` | `QDialog` | P2 |
| `columnEditor`, `FindCharsInRange`, `RunDlg`, `AboutDlg`, `WindowsDlg`, `TaskList`(Ctrl+Tab) | `QDialog` | P2 |
| `Printer` | `QPrinter` + Scintilla `FormatRange` | P2 |
| `ToolTip` | `QToolTip` | P2 |
| `TrayIcon` | `QSystemTrayIcon` | P3 |
| `PluginsAdmin` | 새 플러그인 API 확정 후 | P3 |

### 5.4 제거 또는 플랫폼별 대체

| 기존 | 처리 |
|---|---|
| `MISC/RegExt` (파일 연결) | Windows: platform 계층으로 이전(HKCU 등록), Linux: `.desktop` + MIME, macOS: `Info.plist` `CFBundleDocumentTypes` |
| `MISC/Exception` (SEH/MiniDump) | 크로스플랫폼 크래시 리포터(crashpad 또는 sentry-native) 선택 도입 |
| `verifySignedfile` (Authenticode) | 플러그인 서명 정책 재정의 시 결정 |
| `IatHook.h`, `UAHMenuBar.h`, `DarkMode/` | 제거 |
| 자동 업데이트(`GUP`) | 1차 제외. 이후 자체 업데이트 체크(Sparkle/WinSparkle 등) 검토 |

---

## 6. 플랫폼별 이슈 및 대응

### 6.1 공통

| 이슈 | 내용 | 대응 |
|---|---|---|
| 문자열 | `wchar_t` 크기 차이, MS 전용 wide CRT 함수 | 부록 A |
| 코드페이지 | `MultiByteToWideChar(CP_xxx)` 다수 | 코드페이지 ↔ 인코딩 이름 매핑 테이블 + ICU/`QStringConverter` |
| 경로 | `\` 구분자, 드라이브 문자, 대소문자 무시 비교, `MAX_PATH` | `QDir`/`std::filesystem::path`, 경로 비교 정책 함수(Windows·macOS 기본 대소문자 무시, Linux 구분) |
| 단축키 | `VK_*` 가상키 코드 기반 `shortcuts.xml` | `VK_*` ↔ `Qt::Key` 변환 테이블로 기존 파일 호환 |
| 폰트 | 기본 `Courier New`/`Consolas` | 플랫폼별 기본 폰트 매핑(Windows `Consolas`, Linux `DejaVu Sans Mono`/`Monospace`, macOS `Menlo`/`SF Mono`) |
| 클립보드 | 커스텀 포맷(`clipboardFormats.h`, 컬럼 선택 등) | `QMimeData` 커스텀 MIME |
| IME | 한/중/일 입력 | Scintilla Qt IME 지원을 3플랫폼에서 검증 — 한국어 입력 필수 테스트 항목 |
| 스레드 | `CreateThread`/`_beginthread` 9곳 | `std::thread`/`QThread`/`QtConcurrent` |

### 6.2 Windows
- 배포: `windeployqt`로 Qt DLL 동봉, MSVC 런타임 포함.
- 기존 사용자 설정 경로(`%APPDATA%\Notepad++`) 및 `doLocalConf.xml`(포터블 모드) 동작 유지. 로컬 개인 사용 단계에서는 별도 폴더를 두지 않음. 원본 Notepad++와 설정을 공유하게 되므로, 테스트 중에는 포터블 모드(`doLocalConf.xml`) 사용을 권장 — 원본 설정 파일 손상 방지.
- 관리자 권한 저장(UAC) 흐름 재설계.

### 6.3 Linux
- 설정 경로: `$XDG_CONFIG_HOME/<앱이름>`, 백업/세션은 `$XDG_STATE_HOME` 검토.
- 신규 문서 기본 EOL을 LF로 (설정 호환 유지).
- 저장 시 원본 권한·소유자 유지, 권한 부족 시 `pkexec` 기반 저장 흐름 검토.
- Wayland: 창 위치 복원 불가(best-effort), 단일 인스턴스 활성화는 `xdg-activation` 확인, 플로팅 도킹 패널 동작 검증(부록 B).
- IME: ibus/fcitx5 양쪽 검증.

### 6.4 macOS (2026-11-09 주 착수)
- 메뉴바: 전역 메뉴바로 표시. `About`/`Preferences`/`Quit`는 `QAction::MenuRole`로 앱 메뉴에 배치.
- 단축키: Qt는 `Ctrl`을 `Cmd`로 자동 매핑 — `shortcuts.xml`의 Ctrl 조합은 Cmd로 동작. 실제 Control 키가 필요한 조합(`Qt::MetaModifier`)과 시스템 예약 단축키(Cmd+H, Cmd+Q 등) 충돌 검토.
- 파일 열기: Finder 더블클릭/Dock 드롭은 `QFileOpenEvent` 처리 필요(명령줄 인자로 오지 않음).
- 앱 수명주기: 마지막 창을 닫아도 앱 유지되는 macOS 관례 반영 여부 결정.
- 배포: `macdeployqt`, Universal 2 바이너리(arm64 + x86_64), 코드 서명·공증(notarization) — 외부 배포 시 Apple Developer 계정 필요. 개인 사용만이면 ad-hoc 서명으로 충분.
- 다크모드: 시스템 외관 자동 추종.
- IME: macOS 한국어 입력기(2벌식) 조합 중 표시·커서 위치 검증.

---

## 7. 플러그인 시스템

현 구조: Win32 DLL + `HWND` 전달 + `SendMessage(NPPM_*)` 253개 메시지. Qt 전환 후에는 **Windows에서도 기존 플러그인 바이너리 호환 불가** (`HWND`/메시지 루프 전제).

계획:
1. **1차 릴리스**: 플러그인 미지원.
2. **2차**: 새 C ABI 정의 (`npp_plugin_api.h`)
   - `.dll`/`.so`/`.dylib` 동적 로딩(`QLibrary`), 함수 테이블 기반 API(`struct NppApi { ... }`)
   - 기존 `NPPM_*` 의미를 함수로 대응시키는 호환 shim → 기존 플러그인 소스 재컴파일 포팅 비용 최소화
   - Scintilla 접근은 기존과 동일하게 `SCI_*` 메시지(`send` 함수 포인터) 제공
   - 플러그인 UI는 Qt 위젯으로 작성 (플러그인도 같은 Qt 버전에 링크해야 하는 제약 명시)
3. **3차**: 스크립팅(Python/Lua) 지원 검토 — PythonScript, Compare, NppExec 등 수요 높은 기능 대체 또는 내장화.

---

## 8. 단계별 일정 (안)

> 1인 개인 프로젝트 기준으로는 기간이 더 늘어날 수 있음. 0단계 결과로 재산정.

| 단계 | 기간 | 산출물 | 완료 기준 |
|---|---|---|---|
| **0. 준비/스파이크** | 2–3주 | 3플랫폼 CMake + CI 골격, Scintilla Qt + Lexilla 샘플 앱, 도킹 스파이크(QDockWidget vs ADS), 결정 대기 항목 확정 | 샘플 앱이 Windows/Linux(1주차), macOS(2주차, 11-09 주)에서 빌드·실행. C++ 파일 하이라이팅, 한글 IME 입력 동작 |
| **1. 코어 추출** | 6–8주 | `npp-core` 라이브러리, 단위 테스트 (문자열 정책 반영) | `Parameters`, `Buffer/FileManager`, 인코딩, 세션, 다국어 단위 테스트가 3플랫폼 CI 통과 |
| **2. 최소 실행 앱 (MVP)** | 6–8주 | 메인 창, 탭, 메뉴, 툴바, 상태바, 열기/저장/새로 만들기, 인코딩·EOL 변환, 언어 선택, 2분할 뷰, 파일 변경 감지, 최근 파일, 세션 복원, 단일 인스턴스 | 일상적 텍스트 편집 가능, 기존 설정 XML 로드 |
| **3. 검색/편집 기능** | 4–6주 | 찾기/바꾸기/파일에서 찾기, 결과 패널, Go to line, 자동완성, 콜팁, 스마트 하이라이트, 매크로, 컬럼 편집, 북마크 | `PowerEditor/Test` 기반 회귀 테스트 + 수동 시나리오 통과 |
| **4. 설정/테마** | 4–6주 | 환경설정, 스타일 설정, 단축키 매퍼, 다크모드/테마, 다국어 UI | 설정 저장·재시작 후 유지, 한국어 UI 동작 |
| **5. 패널/부가 기능** | 4–6주 | Function List, Document Map, Folder as Workspace, Project Panel, Document List, UDL 다이얼로그, 인쇄, Character Panel, Clipboard History | 기능 체크리스트 90% 이상 |
| **6. 패키징/안정화** | 3–4주 | Windows 설치본/포터블, AppImage, `.dmg`, 크래시 리포터, 성능 튜닝 | 대용량 파일(>500MB) 열기, 3플랫폼 베타 배포 |
| **7. 플러그인 API** | 6주+ | 새 플러그인 ABI, 샘플 플러그인, 호환 shim | 샘플 플러그인이 3플랫폼에서 로드/동작 |

---

## 9. 테스트 전략

- **단위 테스트** (GoogleTest 또는 Qt Test): 코어 — 설정 XML 왕복(round-trip), 인코딩 감지/변환, EOL 변환, 경로 처리, 세션, UDL 파싱, 단축키 변환 테이블, 비 BMP 문자(이모지 등) 처리.
- **기존 테스트 자산 재활용**: `PowerEditor/Test/FunctionList`, `UrlDetection`, `xmlValidator`를 CTest에 편입.
- **UI 테스트**: Qt Test 스모크 테스트, 로컬 CI(`ci/local-ci.sh`)에서 `QT_QPA_PLATFORM=offscreen` 실행.
- **호환성 테스트**: Windows Notepad++ 사용자 설정 폴더를 그대로 복사해 3플랫폼에서 로드.
- **수동 검증 매트릭스**: Windows 10/11 × Linux(X11/Wayland, GNOME/KDE) × macOS(arm64/x86_64) × 한국어 IME × HiDPI.

---

## 10. 리스크

| 리스크 | 영향 | 가능성 | 대응 |
|---|---|---|---|
| UI 재작성 공수 과소평가 (`StaticDialog` 57개, `.rc` 리소스 다수) | 일정 지연 | 높음 | P0–P3 단계적 진행, Qt Designer `.ui`로 신속 작성 |
| 개인 프로젝트 리소스 한계 | 일정 지연 | 높음 | MVP 범위 엄격히 관리, 사용 빈도 낮은 기능 후순위 |
| 문자열 전환 중 회귀 (부록 A) | 문자 깨짐, 크래시 | 중 | 코어 추출과 동시 진행 + 비 BMP 문자 테스트 케이스 |
| 3플랫폼 동시 지원 부담 (CI, IME, 단축키, 배포) | 품질 편차 | 중 | 로컬 CI로 Windows/Linux 상시 검증, macOS는 Mac 장비에서 같은 스크립트 실행, 플랫폼별 체크리스트 |
| 플러그인 생태계 부재 | 사용성 저하 | 높음 | 7장 계획, 핵심 기능 내장화 검토 |
| ADS의 Wayland/macOS 플로팅 동작 이슈 | 사용성 저하 | 중 | `DockManager` 추상화로 구현 교체 가능하게 유지, 0단계 스파이크에서 확인 |
| 원본 Notepad++와 설정 폴더 공유 | 원본 설정 손상 | 중 | 개발·테스트 중 포터블 모드 사용, 설정 파일 백업 |
| macOS 서명/공증 | 배포 시 Gatekeeper 경고 | 중 | 개인 사용은 ad-hoc 서명, 외부 배포 시 Developer 계정 |
| 명칭/상표 | "Notepad++" 이름·로고 사용 | 낮음(로컬 사용) | 외부 배포 시점에 포크 명칭 지정 (GPL은 코드 사용 허용, 상표는 별개) |
| 라이선스 | GPL-3.0 + Qt(LGPL-3/GPL) + (선택) ADS(LGPL-2.1)/KDDockWidgets(GPL) | 낮음 | 전체 GPL-3.0 배포로 호환, Qt 동적 링크 |

---

## 11. 제안 디렉터리 구조

```
notepad-plus-plus/
├─ CMakeLists.txt, CMakePresets.json   # 신규 루트 (3플랫폼 공용)
├─ vcpkg.json                          # 선택
├─ scintilla/ lexilla/ boostregex/     # 기존 유지
├─ app/
│  ├─ core/                            # 플랫폼 독립 코어 (C++20 + QtCore)
│  │  ├─ config/ buffer/ encoding/ search/ session/ lang/ i18n/ editor/
│  ├─ ui/                              # Qt Widgets UI
│  │  ├─ main.cpp, MainWindow.*
│  │  ├─ editor/ (ScintillaEditBase 래퍼)
│  │  ├─ docking/ (도킹 추상화)
│  │  ├─ dialogs/ panels/ widgets/
│  │  └─ resources/ (*.qrc, icons, *.ui)
│  ├─ platform/{win,linux,mac}/        # Qt 미지원 기능만
│  ├─ thirdparty/ (pugixml, uchardet, json — PowerEditor/src에서 이동)
│  └─ tests/
├─ data/                               # langs/stylers model, nativeLang, APIs, functionList, UDL
├─ packaging/{windows,linux,macos}/
├─ PowerEditor/                        # 기존 Win32 소스 — 참조용, 기능 동등성 확보 후 삭제
└─ docs/
```

### 11.1 소스 관리

| 항목 | 내용 |
|---|---|
| 저장소 | `https://github.com/asprenas/notepad-plus-plus` (업스트림 fork, **공개**) |
| remote | `origin` = fork, `upstream` = `notepad-plus-plus/notepad-plus-plus` (참조용, 동기화 안 함) |
| 기준점 | 업스트림 태그 `v8.9.8.1` (`dd40fe4`) — fork에도 태그 push |
| 작업 브랜치 | `qt-port` (기준점에서 분기). fork의 `master`는 업스트림 미러로 두고 사용하지 않음 |
| 기능 브랜치 | 필요 시 `qt-port`에서 `feat/<주제>` 분기 후 병합 |
| 업스트림 수정 반영 | 필요한 수정만 `upstream` fetch 후 `cherry-pick` 또는 수동 반영 |
| 공개 저장소 유의 | 비밀정보(인증서, 서명 키, 토큰) 커밋 금지 |
| CI | **GitHub은 저장소로만 사용, CI는 로컬에서만 수행** (2026-11-02). fork의 GitHub Actions 비활성화(저장소 설정 `actions enabled=false`). push 전 `bash ci/local-ci.sh` 실행 |

---

## 12. 즉시 착수 작업 (0단계 체크리스트)

- [x] 결정 사항 확정: 문자열 UTF-8, `DockManager` + ADS, 명칭/설정 폴더 보류 (2026-11-02)
- [x] 소스 관리 구성: fork `qt-port` 브랜치, 기준 태그 `v8.9.8.1` (2026-11-02, 11.1절)
- [x] 업스트림 CI 워크플로 제거(`CI_build.yml`, `release-notifier.yml`, `appveyor.yml`), fork GitHub Actions 비활성화 (2026-11-02)
- [ ] 문자열 변환 규칙 문서화 및 `L"`/`std::wstring` 일괄 변환 스크립트 작성 (부록 A.4) — 1단계 착수 시
- [x] `DockManager` 인터페이스 작성: `app/ui/docking/DockManager.h` (부록 B.4)
- [x] ADS를 git submodule로 추가: `app/thirdparty/qtads` @ `v5.1.1`
- [x] 루트 CMake + Presets: Windows(MinGW/MSVC), Linux(GCC), macOS(프리셋만) 구성
- [x] `scintilla/qt/ScintillaEditBase` + `lexilla` + `boostregex` + `pugixml` CMake 타깃화 (`app/thirdparty/CMakeLists.txt`)
- [x] 샘플 Qt 앱: 파일 열기/저장, 렉서 적용, `langs.model.xml`·`stylers.model.xml` 키워드·색상 적용, 한글 UTF-8 표시
- [ ] **한글 IME 입력 수동 확인** (Windows MS IME, Linux ibus/fcitx5) — 자동 테스트 불가, 사용자 확인 필요
- [x] 로컬 CI `ci/local-ci.sh`: Windows(MinGW, MSVC) / Linux(Docker, Ubuntu 26.04) × ADS/QT 백엔드 → 2026-11-09 주 macOS 추가
- [ ] macOS 빌드 환경: Xcode CLT, Qt 6 (Universal), `macdeployqt` 동작 확인 (2026-11-09 주)
- [~] 도킹 스파이크: ADS/QT 두 백엔드로 탭 그룹·상태 저장·백엔드 전환 시 레이아웃 폴백 확인 (Windows, Linux offscreen). **남음**: 플로팅 드래그 수동 확인, Linux Wayland/X11 실기, macOS
- [ ] `Parameters.cpp`, `Buffer.cpp`, `Common.cpp`의 Win32 API 사용 목록 상세 추출 — 1단계 착수 시
- [ ] 기능 체크리스트(메뉴 기준 전체 기능 목록) 작성 → 단계별 범위 확정

---

### 12.1 0단계 진행 결과 (2026-11-02)

| 항목 | 결과 |
|---|---|
| Windows (MinGW 13.1, Qt 6.12.0) | 빌드·테스트·실행 통과 (ADS, QT 백엔드 모두) |
| Linux (Ubuntu 26.04 Docker, GCC, Qt 6.10) | 빌드·테스트·offscreen 실행 통과 (ADS, QT 백엔드 모두) |
| Windows MSVC (VS 2026 / MSVC 19.51, Qt 6.12.0 `msvc2022_64`) | 로컬 CI에서 빌드·테스트·실행 통과 (ADS, QT 백엔드). 경고 1건은 ADS 소스(`DockManager.cpp` nodiscard), 프로젝트 코드 경고 0건 |
| 자동 테스트 (`app/tests/test_editor_smoke.cpp`) | UTF-8 텍스트, Boost regex(lookahead — 기본 엔진엔 없는 기능으로 연결 확인), Lexilla(`cpp`, Npp UDL `user`), 모델 XML 로드, 확장자→언어, cpp 키워드 스타일이 Npp와 동일 |
| Linux 이식 중 발견한 vendored 소스 문제 | 2건 수정 — `docs/third-party-patches.md` |
| 도킹 추상화 검사 | `ci/check-docking-abstraction.sh` (로컬 CI 포함) |

발견 사항:
- Qt 온라인 설치본 MinGW kit에 Core5Compat 기본 미포함 → 별도 설치 필요 (`docs/build.md`).
- Ubuntu 24.04 패키지 Qt는 6.4 → Linux 기준 배포판을 Ubuntu 26.04(Qt 6.10)로 조정. 24.04 지원이 필요하면 aqtinstall 등으로 Qt를 별도 설치해야 함.
- 키워드 클래스(`instre1`, `type1` …) → Scintilla 키워드 세트 매핑이 언어별로 다름 (upstream `setXxxLexer`). 0단계는 C 계열만 처리, 1단계에서 전 언어 이식 필요.
- ADS `DockManager.h`와 프로젝트 `docking/DockManager.h` 파일명이 같음 → ADS 헤더는 `<...>`, 프로젝트 헤더는 `"..."`로 include하는 규칙으로 구분.

---

## 부록 A. 내부 문자열 UTF-8 전환 검토

### A.1 현재 상태

| 지표 | 건수 |
|---|---|
| `L"..."` 와이드 리터럴 | 4,414 |
| `std::wstring` 사용 | 2,363 |
| `wchar_t` | 1,463 |
| MS 전용 wide CRT 함수 (`_wcsicmp`, `_wtoi`, `wcscpy_s`, `swprintf_s`, `_wfopen` 등) | 190 |
| UTF-16 ↔ 멀티바이트 변환 (`WcharMbcsConvertor` 213 + `WideCharToMultiByte`/`MultiByteToWideChar` 49) | 262 |
| UTF-16 가정 코드 (`sizeof(wchar_t)`, 서로게이트 범위 상수 등) | 31 |

핵심 관찰: **앱의 양 끝은 이미 UTF-8**이다.
- Scintilla 문서 버퍼: UTF-8 (`SC_CP_UTF8`)
- pugixml: char(UTF-8) 모드로 빌드됨 (`pugiconfig.hpp`의 `PUGIXML_WCHAR_MODE` 비활성) → XML 설정은 UTF-8로 읽고 씀
- Linux/macOS 파일 경로·환경 변수·명령줄: UTF-8 바이트열

중간의 앱 로직만 Win32 API 사용을 위해 UTF-16(`std::wstring`)으로 동작하며, 그래서 변환 코드가 262곳 존재한다. Win32 API를 더 이상 쓰지 않게 되면 `std::wstring`을 유지할 이유가 사라진다.

### A.2 `std::wstring`을 유지할 때의 문제

1. **`wchar_t` 크기가 플랫폼마다 다르다.** Windows 2바이트(UTF-16), Linux/macOS 4바이트(UTF-32). 같은 코드가 플랫폼별로 다르게 동작한다.
   - 서로게이트 쌍 처리(이모지, 확장 한자 등) 코드는 Linux/macOS에서 무의미하거나 오동작.
   - `길이 × sizeof(wchar_t)`로 바이트 크기를 계산해 파일/버퍼에 쓰는 코드는 UTF-16 LE 저장 등에서 잘못된 결과를 낸다.
   - 문자 수/열 위치 계산이 플랫폼별로 달라져 버그 재현이 어려워진다.
2. **MS 전용 wide CRT 함수 190곳은 Linux/macOS에 없다.** `std::wstring`을 유지해도 이 부분은 어차피 전부 고쳐야 한다.
3. **변환 비용 유지.** Scintilla(UTF-8) ↔ 앱(wstring) ↔ Qt(QString) 3중 변환이 계속 필요하다.
4. **메모리.** Linux/macOS에서 `std::wstring`은 문자당 4바이트.
5. 표준 라이브러리의 wide 지원(`std::wregex`, `wcout`, locale 의존 wide 함수)은 플랫폼 간 동작 차이가 크다.

### A.3 선택지 비교

| 선택지 | 내용 | 장점 | 단점 |
|---|---|---|---|
| **A. `std::wstring` 유지** | 경계에서만 변환 | 초기 수정량 최소 | A.2의 문제 모두 잔존. MS CRT 190곳은 어차피 수정. 플랫폼별 동작 차이가 장기 버그 원인 |
| **B. `std::string`(UTF-8) 전환 (권장)** | 코어 전체 UTF-8, Qt 경계에서 `QString::fromUtf8` | Scintilla·pugixml·POSIX와 일치 → 변환 대부분 제거. 플랫폼 간 동작 동일. 표준 C++만으로 코어 작성 | 리터럴 4,414개, `wstring` 2,363곳 수정. 문자 단위 처리(대소문자 변환, 문자 분류)에 별도 유틸 필요(ICU/Qt 사용). Windows 파일 경로는 `std::filesystem::path(u8string)` 또는 Qt 경유 필수 |
| C. `QString`(UTF-16) 전환 | 코어도 QString 사용 | 모든 플랫폼에서 UTF-16 → 기존 UTF-16 가정 코드가 그대로 유효. Qt API(파일, 정규식, 대소문자 변환)와 직결 | 코어가 Qt 문자열에 강결합. Scintilla·pugixml과는 여전히 UTF-8 변환 필요. 수정량은 B와 비슷 |

### A.4 결정 및 적용 규칙

- **결정(2026-11-02): B (UTF-8 `std::string`)**. 데이터가 실제로 존재하는 형식(Scintilla, XML, 파일 시스템)과 내부 표현을 일치시키는 것이 3플랫폼에서 가장 단순하다.
- **시점: 1단계(코어 추출)에서 동시에 진행.** 코어 추출 과정에서 해당 파일들을 어차피 전부 수정하므로(Win32 API 제거, MS CRT 함수 교체) 이때 함께 바꾸는 것이 총비용이 가장 적다. 나중에 바꾸면 같은 코드를 두 번 수정하게 된다.
- 규칙(안):
  - 코어 공개 API의 문자열은 `std::string`(UTF-8) / `std::string_view`. 경로는 `std::filesystem::path`.
  - Qt 경계 변환 함수는 한 곳(`ui/strconv.h`: `toQ()`, `fromQ()`)에만 둔다.
  - 문자 단위 연산(대소문자 무시 비교, 단어 경계)은 전용 유틸로 일원화 (`QString`/ICU 위임).
  - Windows 빌드는 MSVC `/utf-8` 옵션 사용.
  - 리터럴 일괄 변환은 스크립트(`L"` → `"`, `std::wstring` → `std::string`)로 1차 처리 후 컴파일 오류 기반 수동 수정.
- 선택지 C가 나은 경우: 코어를 Qt에 강하게 묶어도 무방하고, 기존 UTF-16 가정 로직(31곳 + 암묵적 가정)을 재검토하는 부담을 피하고 싶을 때.

---

## 부록 B. 도킹 구현 비교: `QDockWidget` vs 별도 라이브러리

### B.1 Notepad++ 도킹 요구사항 (현 `DockingWnd` 기준)
1. 상/하/좌/우 4방향 도킹
2. 한 영역에 여러 패널을 **탭으로 묶기** (예: 하단 검색 결과 + 콘솔)
3. 패널 플로팅(분리) 및 플로팅 창 안에서도 탭 묶기
4. 캡션 바(제목 + 닫기 버튼), 패널 표시/숨김 토글
5. 배치 상태 저장/복원 (`config.xml`의 `DockingManager` 항목)
6. 다크모드/테마 일관성

### B.2 후보

| 항목 | **QDockWidget** (Qt 내장) | **Qt Advanced Docking System (ADS)** | **KDDockWidgets** (KDAB) |
|---|---|---|---|
| 라이선스 | Qt와 동일 (LGPL-3/GPL) | LGPL-2.1 | GPL-2.0/GPL-3.0 또는 상용 |
| 의존성 | 없음 | 외부 라이브러리 (CMake, 소스 포함 빌드 용이) | 외부 라이브러리, 상대적으로 큼 |
| 4방향 도킹 | O | O | O |
| 탭 그룹 | O (`tabifyDockWidget`), 탭 모양 커스터마이징 제한적 | O (영역별 탭, 탭 메뉴, 닫기 버튼) | O |
| 패널 간 임의 분할/중첩 | 제한적 (중앙 위젯 주변 영역 위주, `AllowNestedDocks`) | O (IDE 수준 자유 배치) | O |
| 플로팅 창에서 탭 묶기 | 제한적 (`GroupedDragging`, 동작 불안정한 경우 있음) | O (플로팅 컨테이너에 여러 패널) | O |
| 드롭 위치 표시 오버레이 | X (기본 하이라이트만) | O (Visual Studio 스타일) | O |
| 자동 숨김(사이드바 핀) | X | O (4.x) | O |
| 상태 저장/복원 | O (`saveState`/`restoreState`, 바이너리) | O (perspective 지원) | O (JSON 레이아웃) |
| 스타일/다크모드 | Qt 스타일시트로 처리, 단순 | 자체 스타일시트 제공, 커스터마이징 필요 | 커스터마이징 가능, 학습량 큼 |
| Linux X11 | 안정 | 안정 | 안정 |
| Linux Wayland | 플로팅 창 위치 지정 제약(Wayland 공통 제약) | 플로팅 드래그 관련 이슈 보고 다수 — 스파이크 필수 | Wayland 지원을 명시적으로 다룸 |
| macOS | 안정 (네이티브 동작에 가까움) | 동작, 플로팅 창 세부 동작 검증 필요 | 동작 |
| 성숙도/사용 사례 | Qt 기본, 매우 안정 | 활발히 유지보수, NotepadNext 등 다수 사용 | KDAB 상용 지원, 대형 앱 사용 |
| 학습/유지보수 비용 | 낮음 | 중간 | 중간~높음 |

### B.3 장단점 요약

**QDockWidget**
- 장점: 외부 의존성 없음, 3플랫폼에서 가장 안정적, 학습 비용 낮음, Qt 업그레이드 시 호환성 걱정 없음.
- 단점: Notepad++ 수준의 탭 그룹·플로팅 탭 묶기·자유 배치 구현이 어렵거나 동작이 불완전. 탭/캡션 외형 커스터마이징이 제한적. 요구사항 B.1-3을 충족하려면 우회 코드가 늘어날 수 있다.
- 적합한 경우: 패널 배치가 단순해도 괜찮고, 안정성과 낮은 유지비가 우선일 때.

**ADS**
- 장점: B.1 요구사항 전부를 기본 기능으로 충족(탭 그룹, 플로팅 탭 묶기, 드롭 오버레이, 자동 숨김). 같은 목표의 NotepadNext가 사용 중이라 참고 사례가 있음. LGPL이라 라이선스 부담 낮음.
- 단점: 외부 의존성 관리 필요, Wayland에서 플로팅 동작 이슈 가능성, 라이브러리 버그를 직접 해결해야 할 수 있음, 스타일을 앱 테마에 맞추는 작업 필요.
- 적합한 경우: Notepad++와 동등하거나 더 나은 패널 UX가 목표일 때.

**KDDockWidgets**
- 장점: 품질·Wayland 대응이 가장 체계적, 레이아웃 JSON 저장.
- 단점: 규모가 크고 API 학습 비용이 높음, GPL(본 프로젝트가 GPL-3.0이라 문제는 없음).
- 적합한 경우: ADS가 Wayland/macOS 스파이크에서 문제를 보일 때의 대안.

### B.4 결정 및 설계 규칙

**결정(2026-11-02): `DockManager` 추상화 계층 + ADS 구현.** 언제든 다른 구현(QDockWidget, KDDockWidgets 등)으로 교체할 수 있어야 한다.

설계 규칙:
1. **ADS 타입은 `ui/docking/ads/` 밖으로 노출 금지.** 다른 코드는 `DockManager` 인터페이스만 include한다. ADS 헤더를 include하는 파일은 이 디렉터리 안에만 둔다 (CI에서 grep으로 검사).
2. **패널 위젯은 순수 `QWidget`.** `FunctionList`, `DocumentMap` 등은 도킹 구현을 모른다. 도킹 관련 동작(표시 요청, 제목 변경)은 `DockManager`를 통해서만 한다.
3. **패널은 문자열 ID로 식별** (예: `"npp.functionList"`). 구현 교체 시에도 ID와 설정이 유지되도록 한다.
4. **배치 저장은 2단계.**
   - 구현 독립 정보(패널별 표시 여부, 도킹 영역, 탭 그룹, 활성 탭)는 `config.xml`에 기록 → 구현을 바꿔도 대략적 배치 복원.
   - 구현 고유 상태(ADS `saveState()` 바이너리 등)는 별도 파일에 보조로 저장 → 같은 구현일 때 정확한 복원. 로드 실패 시 구현 독립 정보로 폴백.
5. **구현 선택은 팩토리 한 곳.** `createDockManager(QMainWindow*)`에서만 구현 결정. CMake 옵션(`NPP_DOCKING_BACKEND=ADS|QT`)으로 빌드 시 선택.
6. **QDockWidget 기본 구현도 함께 유지 (권장).** 기능은 축소해도 되지만 빌드·실행 가능하게 두면 추상화 누수를 조기에 발견하고, ADS 문제 발생 시 즉시 전환 가능.

인터페이스 초안:

```cpp
// ui/docking/DockManager.h
enum class DockArea { Left, Right, Top, Bottom, Floating };

struct PanelDesc {
    std::string id;          // 고유 ID, 예: "npp.functionList"
    QString     title;
    QIcon       icon;
    DockArea    defaultArea = DockArea::Right;
    bool        closable    = true;
};

class DockManager : public QObject {
    Q_OBJECT
public:
    virtual ~DockManager() = default;

    virtual QString backendName() const = 0;          // "ADS", "QT"
    virtual void setCentralWidget(QWidget* widget) = 0; // editor area (ADS needs its own central dock widget)

    virtual void addPanel(const PanelDesc& desc, QWidget* content) = 0;
    virtual void removePanel(const std::string& id) = 0;

    virtual void showPanel(const std::string& id, bool activate = true) = 0;
    virtual void hidePanel(const std::string& id) = 0;
    virtual bool isPanelVisible(const std::string& id) const = 0;
    virtual void setPanelTitle(const std::string& id, const QString& title) = 0;

    // 구현 독립 배치 정보 (config.xml 저장용)
    virtual DockLayout layout() const = 0;
    virtual void applyLayout(const DockLayout& layout) = 0;

    // 구현 고유 상태 (보조 저장)
    virtual QByteArray saveNativeState() const = 0;
    virtual bool restoreNativeState(const QByteArray& state) = 0;

signals:
    void panelVisibilityChanged(const std::string& id, bool visible);
    void panelCloseRequested(const std::string& id);
};

std::unique_ptr<DockManager> createDockManager(QMainWindow* mainWindow);
```

# 4단계 세부 계획: 설정 / 테마 / 다국어

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장
- 기간: 4–6주
- 선행: [3단계 검색/편집](phase-3-search-edit.md) 완료
- 다음 단계: [5단계 패널/부가 기능](phase-5-panels-extras.md)


> 개발 범위: 현재 대상 플랫폼은 **Windows·Linux**. macOS는 차후 개발 예정이므로 macOS 관련 작업·테스트는 완료 기준에서 제외하고 별도 문서 [macOS 지원 세부 계획](phase-macos.md)에서 관리한다.

---

## 1. 목표

사용자가 앱 안에서 **설정을 바꾸고 저장**할 수 있게 한다. 2단계까지는 설정을 읽기만 했다.
변경 결과는 원본과 같은 XML 형식으로 저장되어 원본 Notepad++에서도 그대로 읽혀야 한다.

## 2. 범위

| 영역 | 원본 | 규모 |
|---|---|---|
| 환경설정 다이얼로그 (하위 페이지 23개) | `WinControls/Preference/` | 8.3k |
| 스타일 설정 (Style Configurator) | `WordStyleDlg` | 약 1.5k |
| 단축키 매퍼 | `WinControls/shortcut/`, `Grid/` | 7.3k |
| 다크 모드 / 테마 (`installer/themes` 22개) | `NppDarkMode.cpp`, `DarkMode/` | 6k (대부분 대체) |
| 다국어 UI (94개 언어) 전환 | `localization.cpp` | 1.6k |
| 컨텍스트 메뉴 사용자 지정 | `contextMenu.xml` | — |
| 툴바 아이콘 세트·사용자 지정 | `toolbarButtonsConf.xml`, `ToolBar/` | 1k |
| 설정 메뉴 나머지 (`IDM_SETTING_*` 15개) | | — |

## 3. 설계

### 3.1 환경설정 다이얼로그

- 구조: 왼쪽 목록 + 오른쪽 `QStackedWidget`. 페이지마다 `.ui` 파일 1개 (Qt Designer로 작성).
- 페이지 목록 (원본 순서 유지):

| 페이지 | 원본 클래스 | 비고 |
|---|---|---|
| 일반 | `GeneralSubDlg` | 언어 선택, 상태바/메뉴 표시 |
| 툴바 | `ToolbarSubDlg` | 아이콘 세트, 크기 |
| 탭 바 | `TabbarSubDlg` | 2단계 탭 동작 설정 |
| 편집 1, 2 | `EditingSubDlg`, `Editing2SubDlg` | 캐럿, 현재 줄, 다중 편집, 특수 문자 표시 |
| 다크 모드 | `DarkModeSubDlg` | Qt 팔레트로 재구현 |
| 여백·테두리·가장자리 | `MarginsBorderEdgeSubDlg` | |
| 새 문서 | `NewDocumentSubDlg` | 기본 EOL·인코딩·언어 |
| 기본 디렉터리 | `DefaultDirectorySubDlg` | |
| 최근 파일 기록 | `RecentFilesHistorySubDlg` | |
| 파일 연결 | (원본 `RegExt`) | **Windows만** 표시, 6단계 `platform/win` |
| 언어 | `LanguageSubDlg` | 언어 메뉴 숨김, 탭 설정 |
| 들여쓰기 | `IndentationSubDlg` | |
| 강조 | `HighlightingSubDlg` | 스마트 하이라이트, 태그 매칭 |
| 인쇄 | `PrintSubDlg` | 5단계 인쇄와 연동 |
| 검색 | `SearchingSubDlg` | |
| 백업 | `BackupSubDlg` | |
| 자동 완성 | `AutoCompletionSubDlg` | |
| 다중 인스턴스·날짜 | `MultiInstanceSubDlg` | |
| 구분자 | `DelimiterSubDlg` | |
| 클라우드·링크 | `CloudAndLinkSubDlg` | |
| 검색 엔진 | `SearchEngineSubDlg` | |
| 성능 | `PerformanceSubDlg` | 대용량 파일 제한 |
| 기타 | `MiscSubDlg` | 자동 업데이트 항목은 숨김 |

- 원칙: 모든 컨트롤은 코어 설정 모델 필드에 1:1 바인딩. 변경 즉시 적용(원본 동작)하고 종료 시 저장.
- 원본 컨트롤 ID(`IDC_*`)를 위젯 objectName으로 유지 → 다국어 파일의 컨트롤 텍스트를 그대로 적용.

### 3.2 스타일 설정

- 테마 선택, 언어 목록, 스타일 목록, 전경/배경색, 글꼴·크기·굵게·기울임·밑줄, 사용자 확장자, 사용자 키워드, "전역 전경/배경색 사용" 등 전역 재정의(`GlobalOverride`).
- 미리보기: 변경 즉시 열린 편집기에 반영, 취소 시 되돌림.
- 색 선택: 원본 `ColourPicker`의 팔레트 + `QColorDialog`.
- 테마 파일(`themes/*.xml`) 저장/다른 이름으로 저장.

### 3.3 단축키 매퍼

- 탭 5개: 메인 메뉴, 매크로, 실행 명령, 플러그인 명령(7단계 전까지 비어 있음), Scintilla 명령.
- `QTableView` + 모델. 필터, 충돌 표시(같은 키 중복), 수정/삭제/초기화.
- 키 입력 위젯: 원본처럼 수식키 체크박스 + 키 목록 방식과, 키를 눌러 지정하는 방식 둘 다.
- 저장: `shortcuts.xml` (원본 형식 그대로, `VK_*` 코드로 기록).

### 3.4 다크 모드와 테마

- 원본 다크 모드(Win32 UxTheme 후킹)는 이식하지 않고 **Qt 팔레트 + Fusion 스타일 + 최소 스타일시트**로 구현.
- 모드: 밝게 / 어둡게 / 시스템 따라가기(`QStyleHints::colorScheme`, Windows·macOS·GNOME·KDE).
- 원본 다크 모드 색 설정(`DarkModeConf`의 톤 선택, 사용자 지정 색)을 팔레트 역할(`QPalette::Window`, `Base`, `Text`, `Highlight` 등)로 대응.
- 편집기 색은 테마 XML(`DarkModeDefault.xml` 등)을 따른다. 다크 모드 전환 시 테마 자동 전환(원본 동작) 지원.
- ADS 도킹 영역 스타일시트를 같은 팔레트로 생성 (`DockManager` 구현 내부에서만 처리).

### 3.5 다국어 UI

- 1단계 `i18n` 테이블을 메뉴(2단계에서 적용), 다이얼로그(이 단계), 메시지 상자에 적용.
- 실행 중 언어 전환 지원 (원본은 즉시 반영).
- 오른쪽에서 왼쪽 언어(아랍어, 히브리어): `QApplication::setLayoutDirection` + 편집기 RTL 옵션.

### 3.6 컨텍스트 메뉴·툴바 사용자 지정

- `contextMenu.xml` 형식 그대로 해석 (메뉴 명령 ID, 매크로, 실행 명령, 플러그인 명령, 하위 폴더).
- 툴바: `toolbarButtonsConf.xml`로 버튼 숨김/순서, 아이콘 세트 3종(Fluent 작게/크게, 표준).

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P4-01 | 환경설정 프레임 + 바인딩 방식 정립, 페이지 6개 | 1 |
| P4-02 | 나머지 페이지 17개 | 2–3 |
| P4-03 | 스타일 설정 다이얼로그, 테마 저장 | 3–4 |
| P4-04 | 단축키 매퍼 | 4 |
| P4-05 | 다크 모드·시스템 테마 추종, ADS 스타일 | 5 |
| P4-06 | 다이얼로그 다국어 적용, 실행 중 전환, RTL | 5 |
| P4-07 | 컨텍스트 메뉴·툴바 사용자 지정 | 6 |
| P4-08 | 원본 Notepad++와 설정 교차 확인, 문서화 | 6 |

## 5. 테스트

- **설정 교차 호환**: 이 앱에서 설정 변경·저장 → 원본 Notepad++(Windows)에서 열어 같은 설정인지 확인, 반대 방향도.
- **페이지 바인딩 테스트**: 페이지별로 컨트롤 값 변경 → 모델 값 확인 → 저장 → 재로드 → 컨트롤 값 확인 (자동화).
- **다국어 스크린샷 점검**: 주요 다이얼로그를 한국어·일본어·독일어(긴 문자열)·아랍어(RTL)로 `--screenshot` 촬영해 잘림 확인.
- **테마**: 22개 테마를 순회 적용해 크래시 없음, 대표 5개는 스크린샷 비교.

## 6. 완료 기준

- [x] 환경설정 23개 페이지, 스타일 설정, 단축키 매퍼 동작 — 원본 `preference.rc`에서 생성한 24개 페이지(원본 8.9.8.1은 "편집 2"가 추가되어 24개), 컨트롤은 `config.xml` 필드에 바인딩(`ui.settings`: `allPagesExist`, `preferenceCheckboxesRoundTrip`, `styleConfiguratorSaves`, `shortcutMapperConflictsAndResult`). [feature-checklist.md](feature-checklist.md) 4단계 16/16
- [x] 설정이 원본 Notepad++와 양방향으로 호환 — `tools/settings-compat/run-with-npp.ps1`로 이 저장소의 원본 소스를 빌드한 Notepad++ 8.9.8.1(MSVC Release x64)과 비교: NPP → 포트 다시 저장 시 376줄 중 손실·추가 0, 포트 → NPP(모든 `GUIConfig` 필드를 기본값이 아닌 값으로 바꾸고 `shortcuts.xml`에 단축키·매크로·Scintilla 키 추가) 다시 저장 시 차이 0. 기본값 차이 4개는 원본이 직접 쓰는 값(업데이트 날짜, `shortcutsXmlHmac`, 강조색으로 풀린 Fluent 사용자 색). 결과: `app/tests/data/settings-compat/report.txt`. 이전 8.8.8 대상 실행에서는 8.9.x에만 있는 설정 5개만 빠짐
- [x] 밝게/어둡게/시스템 모드가 Windows·Linux에서 동작 — `ui.settings`: `lightDarkMode`(원본 다크 톤·테마 자동 전환), `systemModeFollowsColorScheme`(Windows는 실제 플랫폼 `QT_QPA_PLATFORM=windows`, Linux는 Docker 안 Xvfb + xcb + GTK 플랫폼 테마), `allThemesApply`(22개 테마 순회)
- [x] 한국어 UI에서 모든 메뉴·다이얼로그 번역 적용 — `ui.settings`: `koreanMenus`(korean.xml이 번역하는 모든 메뉴 명령과 `SubEntries` 하위 메뉴 전부), `koreanDialogs`(찾기·바꾸기, 이동, 문자 범위 찾기, 열 편집기, 증분 검색, 결과 내 찾기, 단축키 매퍼, 매크로 대화상자, 환경설정 24페이지, 스타일 설정에서 미번역 컨트롤 0), `uiLanguageSwitch`, `rtlLanguage`(아랍어 RTL). 한국어·일본어·독일어·아랍어 스크린샷(`screenshots`, `NPP_SCREENSHOT_DIR` 설정 시)으로 잘림 없음 확인
- [x] 로컬 CI 통과 — Windows MinGW/MSVC, Linux Docker × ADS/QT

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| 페이지 수가 많아 공수 큼 | 바인딩 공통 코드(위젯 ↔ 모델 필드 매핑 테이블)로 페이지당 코드 최소화 |
| 원본 다크 모드와 색감 차이 | 원본 기본 색값을 팔레트 기본값으로 사용, 스크린샷 비교 |
| 긴 번역 문자열로 레이아웃 깨짐 | 레이아웃 매니저 사용(고정 좌표 금지), 다국어 스크린샷 점검 |

## 8. 진행 현황

| ID | 상태 | 구현 위치 / 비고 |
|---|---|---|
| P4-01, P4-02 | 완료 | `ui/prefs/PreferencesDialog`, 페이지는 `tools/gen-pref-pages.py`로 `preference.rc`에서 생성, `PreferenceBindings.cpp`(위젯 ↔ `GuiConfig` 매핑 테이블). 플러그인 가져오기·플러그인 폴더 열기 |
| P4-03 | 완료 | `ui/StyleConfigurator`(원본 `WordStyleDlg`: 테마 전환·미리보기·취소 복원, 스타일별 색·글꼴, 확장자, 키워드, 전역 재정의, 사용자 stylers/테마 저장) |
| P4-04 | 완료 | `ui/ShortcutMapper`(메인 메뉴·매크로·실행·플러그인·Scintilla 탭, 충돌 표시, 수정/삭제, 원본 형식 `shortcuts.xml`) |
| P4-05 | 완료 | `ui/Theme.cpp`: Fusion + 팔레트(원본 다크 톤), 시스템 따라가기, 모드별 테마·툴바 설정(원본과 같이 모드별 저장), ADS 스타일 |
| P4-06 | 완료 | `ui/DialogTranslator`(원본 컨트롤 ID로 번역, english.xml 복귀), `qtbase_<lang>.qm`, 하위 메뉴 번역(원본 `g_menuFolderPositions` 표), RTL 레이아웃 |
| P4-07 | 완료 | `ui/ContextMenus.cpp`(`contextMenu.xml`, `tabContextMenu.xml`), `ui/Toolbar.cpp`(41개 버튼, 아이콘 세트, `toolbarButtonsConf.xml`), 탭 색·고정 탭, NPC/CC Uni EOL 표시 |
| P4-08 | 완료 | `tools/settings-compat`(원본과 양방향 교차 확인), 이 문서 6장 |

교차 확인으로 고친 점:
- 자동 들여쓰기 값: 원본 `config.xml`은 0 없음, 1 고급, 2 기본(`AutoIndentMode`)인데 포트가 1·2를 반대로 해석했다. 원본 값으로 통일(기본값 1).
- 줌 동기화가 켜져 있으면 두 번째 보기 줌을 첫 번째 보기 값으로 저장·적용한다(원본 `saveGUIParams`).
- 하위 메뉴(편집 > 클립보드에 복사 등)와 최근 파일 메뉴 항목이 번역되지 않던 문제 수정.

계획과 다르게 한 점:
- 환경설정 페이지는 Qt Designer `.ui` 대신 원본 `preference.rc`에서 생성한 코드로 만든다(컨트롤 ID·순서를 원본과 같게 유지하고 번역 파일을 그대로 적용).
- 파일 연결 페이지는 6단계(`platform/win`)에서 동작을 붙인다. 이 단계에서는 페이지만 있다.

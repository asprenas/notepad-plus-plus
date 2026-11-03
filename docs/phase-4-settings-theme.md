# 4단계 세부 계획: 설정 / 테마 / 다국어

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장
- 기간: 4–6주
- 선행: [3단계 검색/편집](phase-3-search-edit.md) 완료
- 다음 단계: [5단계 패널/부가 기능](phase-5-panels-extras.md)


> 개발 범위: 현재 대상 플랫폼은 **Windows·Linux**. macOS는 차후 개발 예정이므로 macOS 관련 작업·테스트는 완료 기준에서 제외하고 `차후 구현 항목` 절에 둔다.

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

- [ ] 환경설정 23개 페이지, 스타일 설정, 단축키 매퍼 동작
- [ ] 설정이 원본 Notepad++와 양방향으로 호환
- [ ] 밝게/어둡게/시스템 모드가 Windows·Linux에서 동작
- [ ] 한국어 UI에서 모든 메뉴·다이얼로그 번역 적용
- [ ] 로컬 CI 통과

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| 페이지 수가 많아 공수 큼 | 바인딩 공통 코드(위젯 ↔ 모델 필드 매핑 테이블)로 페이지당 코드 최소화 |
| 원본 다크 모드와 색감 차이 | 원본 기본 색값을 팔레트 기본값으로 사용, 스크린샷 비교 |
| 긴 번역 문자열로 레이아웃 깨짐 | 레이아웃 매니저 사용(고정 좌표 금지), 다국어 스크린샷 점검 |

---

## 차후 구현 항목 (macOS)

- macOS 시스템 외관(다크 모드) 추종 확인

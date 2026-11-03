# 2단계 세부 계획: 최소 실행 앱 (MVP)

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장, 5.3절
- 기간: 6–8주
- 선행: [1단계 코어 추출](phase-1-core-extraction.md) 완료
- 다음 단계: [3단계 검색/편집](phase-3-search-edit.md)


> 개발 범위: 현재 대상 플랫폼은 **Windows·Linux**. macOS는 차후 개발 예정이므로 macOS 관련 작업·테스트는 완료 기준에서 제외하고 별도 문서 [macOS 지원 세부 계획](phase-macos.md)에서 관리한다.

---

## 1. 목표

`npp-core` 위에 **일상적인 텍스트 편집이 가능한 Qt 앱**을 만든다.
기존 Notepad++ 사용자의 설정 폴더를 그대로 읽어 같은 언어 강조, 같은 단축키, 같은 세션으로 시작해야 한다.

## 2. 범위

| 포함 | 제외 (담당 단계) |
|---|---|
| 메인 창, 메뉴, 툴바, 상태바 | 찾기/바꾸기 (3) |
| 탭, 2분할 뷰(메인/서브), 문서 이동·복제 | 환경설정 다이얼로그 (4) |
| 새로 만들기/열기/저장/다른 이름으로/모두 저장/닫기 계열 | 스타일 설정, 단축키 매퍼 (4) |
| 인코딩 메뉴, EOL 변환 | 패널류 (5) |
| 언어 메뉴(수동 언어 선택) | 인쇄 (5) |
| 파일 변경 감지와 다시 읽기 | 플러그인 (7) |
| 최근 파일, 세션 복원, 백업(스냅샷) 복원 | |
| 단일 인스턴스, 명령줄 인자 | |
| 드래그 앤 드롭 | |
| 기존 `shortcuts.xml` 적용 (편집 없이 읽기만) | |

## 3. 설계

### 3.1 명령 체계 (`ActionRegistry`)

- 메뉴 명령 524개(`menuCmdID.h`)를 `IDM_*` 숫자 ID 그대로 키로 하는 `QAction` 레지스트리를 만든다.
- 메뉴 구조는 `Notepad_plus.rc`의 메뉴 정의를 옮긴 **선언형 테이블**(`ui/menu/MenuDefinition.cpp`)로 만든다. 메뉴 이름은 [1단계 `i18n`](phase-1-core-extraction.md)에서 ID로 조회한다.
- 명령 실행은 `NppCommands.cpp`의 `command(int id)` 거대 스위치(710 case)를 **기능 그룹별 핸들러 클래스**로 나눈다.

| 핸들러 | 범위 | 2단계 구현 대상 |
|---|---|---|
| `FileCommands` | `IDM_FILE_*` (27) | 전부 (인쇄 제외) |
| `EditCommands` | `IDM_EDIT_*` (106) | 실행 취소/다시, 잘라내기/복사/붙여넣기, 전체 선택, 삭제 (나머지 3단계) |
| `ViewCommands` | `IDM_VIEW_*` (94) | 줄 바꿈, 공백/EOL 표시, 확대/축소, 탭 이동, 뷰 이동/복제, 항상 위 (패널 토글은 5단계) |
| `FormatCommands` | `IDM_FORMAT_*` (14) + 인코딩 | 전부 |
| `LanguageCommands` | `IDM_LANG_*` (101) | 전부 |
| `HelpCommands` | `IDM_ABOUT` 계열 | 정보, 명령줄 인자 |

- 메뉴 활성/체크 상태 갱신은 현재 버퍼 상태 변경 신호에 연결한다 (원본 `checkMenuItem`/`enableCommand` 호출 대응).

### 3.2 단축키

- 코어 `ShortcutConfig`가 읽은 `shortcuts.xml`과 기본값(`Parameters.cpp`의 기본 단축키 표)을 `QAction::setShortcut`에 적용한다.
- `VK_*` → `Qt::Key` 변환은 1단계 `shortcut/`의 표를 사용.
- macOS: `Ctrl` 조합은 Qt가 `Cmd`로 매핑한다. 시스템 예약 조합(Cmd+H, Cmd+Q, Cmd+M)과 충돌하는 원본 단축키 목록을 작성해 대체 키를 정한다.
- Scintilla 키 매핑(`ScintillaKeyMap`)은 `SCI_ASSIGNCMDKEY`로 적용.

### 3.3 편집기 뷰

| 원본 | Qt 구현 |
|---|---|
| `ScintillaEditView` | `EditorView` (`ScintillaEditBase` 상속 또는 포함) + 코어 `IEditor` 어댑터 |
| `DocTabView` + `TabBar` | `DocTabBar`(`QTabBar` 확장): 닫기 버튼, 드래그 이동, 다른 뷰로 드래그, 변경 표시, 읽기 전용 아이콘, 탭 색상(5색), 고정 탭(`IDM_PINTAB`) |
| 메인/서브 2개 뷰 | `QSplitter` + 뷰 2개. 같은 Scintilla 문서를 공유하는 복제(`SCI_SETDOCPOINTER`) |
| 스크래치 편집기(`_pscratchTilla`) | 화면에 없는 `EditorView` 1개 (코어 FileManager 초기화용) |

탭 바 세부 동작(이중 클릭 닫기, 마지막 탭 닫을 때 동작, 탭 너비 등)은 `config.xml`의 `TabSetting`을 따른다.

### 3.4 파일 입출력

- 원본 `NppIO.cpp`(3k줄)의 흐름을 `FileCommands`로 옮긴다: 열기 시 이미 열린 파일 확인, 존재하지 않는 파일 생성 여부 질문, 폴더 열기, 와일드카드, 대용량 파일 경고, 저장 실패 시 관리자 권한 저장 제안(6단계로 미룸) 등.
- 대화상자: `QFileDialog`(네이티브). 필터 목록은 `LanguageRegistry`의 확장자로 생성 (원본 `CustomFileDialog` 동작).
- 파일 변경 감지: `QFileSystemWatcher`로 감시 → 코어 `FileManager::checkFilesystemChanges` 호출 → "다시 읽기/무시" 질문. 원본 설정(`fileAutoDetection`: 감지 안 함/감지/조용히 갱신/끝으로 이동)을 그대로 지원.
- 백업/스냅샷: 원본의 주기적 백업(`backupCurrentBuffer`)과 시작 시 복원 흐름.

### 3.5 세션과 시작 흐름

1. 단일 인스턴스 확인: `QLocalServer` 이름 = 사용자별 고유 이름. 이미 실행 중이면 명령줄 인자를 전달하고 종료 (`-multiInst`면 생략).
2. 코어로 설정 로드 → 창 배치 복원.
3. 세션(`session.xml`) 복원 → 명령줄 파일 열기 → 백업 복원.
4. 명령줄 인자: 원본 `CmdLineParams`의 주요 옵션(`-n줄`, `-c열`, `-l언어`, `-multiInst`, `-nosession`, `-ro`, `-alwaysOnTop`, `-openSession`, `-p위치`, `-x`/`-y`) 지원. 나머지는 체크리스트에 기록.

### 3.6 상태바

원본과 같은 칸 구성: 언어 이름 | 길이·줄 수 | 줄·열·선택 | EOL | 인코딩 | 삽입/겹쳐쓰기. 각 칸 더블클릭 동작(예: EOL 칸 → 변환 메뉴)도 이식.

### 3.7 macOS 특이 사항

- 전역 메뉴바, 앱 메뉴(About/Preferences/Quit), `QFileOpenEvent`(Finder에서 열기, Dock 드롭).
- 마지막 창을 닫아도 앱 유지 여부: Notepad++ 동작(창 닫기 = 종료)을 기본으로 한다.

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P2-01 | `ActionRegistry`, 메뉴 정의 테이블, 다국어 메뉴 이름 | 1 |
| P2-02 | `EditorView` + `IEditor` 어댑터, 기본 편집 명령 | 1 |
| P2-03 | `DocTabBar`, 탭 관련 `View` 명령 | 2 |
| P2-04 | 2분할 뷰, 문서 이동/복제 | 2–3 |
| P2-05 | `FileCommands` (열기/저장/닫기 계열, 대화상자) | 3–4 |
| P2-06 | 인코딩·EOL·언어 메뉴 | 4 |
| P2-07 | 파일 변경 감지, 백업/스냅샷 | 5 |
| P2-08 | 세션, 최근 파일, 시작 흐름, 단일 인스턴스, 명령줄 | 5–6 |
| P2-09 | 단축키 적용 (`shortcuts.xml`, Scintilla 키맵) | 6 |
| P2-10 | 툴바 (`toolbarButtonsConf.xml`, 아이콘 세트), 상태바 | 6–7 |
| P2-11 | Windows·Linux 점검 | 7 |
| P2-12 | 드래그 앤 드롭, 마무리, 문서화 | 8 |

## 5. 테스트

- **UI 스모크 (Qt Test, offscreen)**: 새 문서 → 입력 → 저장 → 닫기 → 다시 열기 → 내용 일치.
- **세션 왕복**: 탭 10개(두 뷰, 다양한 인코딩) 상태로 종료 → 재시작 → 같은 상태.
- **기존 설정 호환**: 실제 사용자 설정 폴더 2–3개로 시작해 언어 강조·단축키·세션이 원본과 같은지 수동 비교.
- **명령줄**: 옵션별 자동 테스트 (단일 인스턴스 전달 포함).
- **파일 변경 감지**: 외부에서 파일 수정/삭제/이름 변경 시나리오.
- 로컬 CI에 UI 스모크 추가.

## 6. 완료 기준

- [ ] 기능 체크리스트의 2단계 담당 항목 완료
- [ ] 기존 설정 폴더로 시작해 언어 강조, 단축키, 세션이 원본과 같음
- [ ] Windows·Linux에서 열기/편집/저장/세션 복원 동작
- [ ] 4시간 연속 일상 사용(개발자 본인) 중 데이터 손실·크래시 없음
- [ ] 로컬 CI 통과

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| 명령 스위치(710 case) 분해 중 동작 누락 | 기능 체크리스트로 ID 단위 추적 |
| 탭 바 세부 동작이 많음 | 1차는 핵심 동작, 세부는 4단계 설정과 함께 |
| macOS 단축키 충돌 | 충돌 목록 작성 후 대체 키 결정, 문서화 |
| Wayland에서 단일 인스턴스 창 활성화 제약 | `xdg-activation` 토큰 전달 시도, 불가 시 알림 표시 |

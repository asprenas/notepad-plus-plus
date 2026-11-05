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

- [ ] 기능 체크리스트 90% 이상 완료 (제외 항목은 사유 기록)
- [ ] Function List 테스트 100%
- [ ] 모든 패널이 두 도킹 백엔드에서 동작, 도킹 추상화 검사 통과
- [ ] 로컬 CI 통과

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| Function List 파서가 대형 파일에서 느림 | 백그라운드 + 지연 갱신, 크기 제한 |
| Document Map 동기화 버그 | 원본 동작을 단계별로 이식, 접기/줄바꿈 시나리오 테스트 |
| 인쇄 결과의 플랫폼 차이 | PDF 출력 비교로 확인, 차이는 문서화 |

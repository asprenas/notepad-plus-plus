# 3단계 세부 계획: 검색 / 편집 기능

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장
- 기간: 4–6주
- 선행: [2단계 MVP](phase-2-mvp.md) 완료
- 다음 단계: [4단계 설정/테마](phase-4-settings-theme.md)

---

## 1. 목표

Notepad++의 핵심 경쟁력인 **찾기/바꾸기**와 **편집 명령**을 원본과 같은 동작으로 이식한다.
특히 정규식 동작은 원본과 바이트 단위로 같아야 한다 (같은 boostregex 사용).

## 2. 범위

| 영역 | 원본 | 규모 |
|---|---|---|
| 찾기/바꾸기 다이얼로그 (찾기, 바꾸기, 파일에서 찾기, 프로젝트에서 찾기, 표시) | `FindReplaceDlg.*` | 7.1k |
| 검색 결과 패널 (`Finder`) | `FindReplaceDlg.cpp`의 `Finder`, `LexSearchResult` | 위에 포함 |
| 증분 검색, 결과 내 찾기, 문자 범위 찾기 | `FindIncrementDlg`, `FindInFinderDlg`, `FindCharsInRange` | 0.6k |
| 줄로 이동 | `GoToLineDlg` | 0.2k |
| 자동 완성, 함수 콜팁 | `AutoCompletion.*`, `FunctionCallTip.*` | 1.9k |
| 스마트 하이라이트, 태그 매칭 | `SmartHighlighter.*`, `xmlMatchedTagsHighlighter.*` | 1k |
| 편집 메뉴 나머지 (`IDM_EDIT_*` 106개 중 2단계 제외분) | `NppCommands.cpp` | — |
| 검색 메뉴 (`IDM_SEARCH_*` 66개) | `NppCommands.cpp` | — |
| 매크로 녹화/재생/저장/여러 번 실행 | `Notepad_plus.cpp`, `RunMacroDlg` | — |
| 열 편집기 | `columnEditor.*` | 0.4k |
| 북마크 | `IDM_SEARCH_*BOOKMARK*` | — |

## 3. 설계

### 3.1 찾기/바꾸기

- 검색 로직은 [1단계 `search/`](phase-1-core-extraction.md)를 사용하고, 이 단계는 UI와 연결만 한다.
- 다이얼로그는 원본처럼 **탭 5개를 가진 비모달 창**(`QDialog` + `QTabWidget`). 탭마다 보이는 컨트롤 집합이 다르므로 `.ui` 파일 하나에 공통 컨트롤을 두고 탭 전환 시 표시/숨김.
- 원본 기능 체크 항목: 방향(위/아래), 대소문자, 단어 단위, 검색 모드(일반/확장/정규식, `.`이 줄바꿈 일치), 순환, 선택 영역 안에서, 투명도(포커스 잃을 때/항상), 검색 기록(`FindHistory`), "모두 찾기(현재 문서/열린 모든 문서)", "모두 바꾸기(열린 모든 문서)", 표시(북마크, 스타일 1–5), 파일에서 찾기의 필터·폴더·하위 폴더·숨김 폴더.
- 파일에서 찾기는 **백그라운드 스레드**(`QtConcurrent`)로 실행하고 진행률·취소 지원. 원본의 진행 창(`Progress`)과 같은 정보.

### 3.2 검색 결과 패널

- 도킹 패널(`DockManager`, id `npp.searchResults`). 내용은 읽기 전용 Scintilla 문서 + Npp 전용 렉서 `searchResult`(`LexSearchResult.cxx`, 이미 Lexilla에 포함).
- 기능: 결과 더블클릭 → 해당 위치 이동, 검색별 접기, 결과 복사, 결과 내 찾기, 다음/이전 결과(F4/Shift+F4), 패널 비우기, 여러 결과 패널(원본 "새 검색 창").

### 3.3 편집 명령

`IDM_EDIT_*` 106개를 기능 묶음별로 `EditCommands`에 구현한다. 대부분 Scintilla 메시지 조합 또는 코어 유틸이다.

| 묶음 | 예 | 구현 위치 |
|---|---|---|
| 대소문자 변환 (대/소/제목/문장/반전/무작위) | `IDM_EDIT_UPPERCASE` 등 | 코어 `util` (UTF-8 대응) |
| 줄 작업 (복제, 삭제, 위/아래 이동, 합치기, 나누기, 빈 줄 제거, 중복 제거) | | 코어 `editor` |
| 정렬 (사전순/숫자/길이, 오름/내림, 대소문자 무시, 무작위) | `Sorters.h` | 코어 `util` |
| 주석 (줄/블록, 토글) | `commentLine` 등 언어별 정보 | 코어 `lang` |
| 공백 처리 (끝 공백 제거, 탭↔공백) | | 코어 `editor` |
| 복사 계열 (파일 경로/이름/폴더 복사, 바이너리 복사/붙여넣기) | | UI |
| 읽기 전용 토글, 선택 영역 열기(파일/폴더), 검색 엔진으로 검색 | | UI |
| 붙여넣기 특수 (HTML/RTF 복사) | | UI, 클립보드 MIME |
| 다중 선택 편집 (선택 다음 일치 추가 등 8.x 신규 명령) | | Scintilla 메시지 |

- 클립보드: 원본 커스텀 포맷(`clipboardFormats.h`, 열 선택 표시)을 `QMimeData` 커스텀 MIME으로. 다른 Notepad++ Qt 인스턴스 간에는 열 선택 유지.

### 3.4 자동 완성과 콜팁

- `installer/APIs/*.xml`(언어별 API 목록) 로드 → 코어. 단어 완성(문서 내 단어), 함수 완성, 파라미터 콜팁.
- 자동 닫기(괄호, 따옴표, HTML/XML 태그) — `MatchedPairConf`.
- 트리거 문자 수, 대소문자 무시 등은 `config.xml` 설정 그대로.

### 3.5 하이라이트

- 스마트 하이라이트: 선택 단어를 화면 범위에서 모두 표시 (indicator). 옵션: 대소문자, 단어 단위, 다른 뷰에도.
- 괄호 매칭, XML/HTML 태그 매칭(`xmlMatchedTagsHighlighter` 로직 그대로 이식).
- URL 감지/클릭 (`PowerEditor/Test/UrlDetection` 테스트 재사용).

### 3.6 매크로

- `ScintillaEditBase::macroRecord` 신호로 Scintilla 동작 기록 + Notepad++ 메뉴 명령 기록.
- 저장 형식은 `shortcuts.xml`의 `<Macros>` 그대로 (원본 매크로 재사용 가능).
- 여러 번 실행(`RunMacroDlg`): 횟수/파일 끝까지.

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P3-01 | 찾기/바꾸기 다이얼로그(찾기·바꾸기 탭), 검색 기록 | 1 |
| P3-02 | 검색 결과 패널, 모두 찾기, F4 이동 | 2 |
| P3-03 | 파일에서/프로젝트에서 찾기 (백그라운드, 취소) | 2–3 |
| P3-04 | 표시 탭, 북마크 명령, 스타일 1–5 표시 | 3 |
| P3-05 | 증분 검색, 결과 내 찾기, 문자 범위 찾기, 줄로 이동 | 3 |
| P3-06 | 편집 명령 106개 | 3–4 |
| P3-07 | 자동 완성, 콜팁, 자동 닫기 | 4–5 |
| P3-08 | 스마트 하이라이트, 태그 매칭, URL | 5 |
| P3-09 | 매크로, 열 편집기 | 5–6 |
| P3-10 | 회귀 테스트 정리, 문서화 | 6 |

## 5. 테스트

- **정규식 호환 테스트**: 원본 Notepad++ 정규식 사례(커뮤니티 FAQ의 대표 패턴 100개 이상)를 입력·기대 결과 쌍으로 만들어 코어 검색 엔진에 실행. 기대 결과는 원본 Windows Notepad++에서 1회 추출.
- **바꾸기 테스트**: 역참조(`$1`, `\1`), 대소문자 변환(`\U`, `\L`), 확장 모드 이스케이프.
- **파일에서 찾기**: 테스트 폴더 트리(인코딩 혼합, 하위 폴더, 숨김 폴더)에서 결과 수 비교.
- **편집 명령**: 명령별 입력/출력 텍스트 쌍 (UTF-8 한글·이모지 포함).
- 기존 자산: `PowerEditor/Test/UrlDetection`.

## 6. 완료 기준

- [x] 기능 체크리스트의 3단계 담당 항목(Edit, Search 메뉴) 완료 — [feature-checklist.md](../reference/feature-checklist.md) 3단계 185/185. Character Panel·Clipboard History는 패널이므로 5단계, "Change Search Engine..."(환경설정 대화상자)은 4단계로 재배정
- [x] 정규식 호환 테스트 100% 통과 — `core.regexcompat`: 137개 사례, 기대값은 원본 Windows Notepad++ 8.8.8에서 1회 추출(`tools/regex-compat/extract-from-npp.ps1`). Windows(MSVC·MinGW) 137/137 일치. Linux는 136/137 일치 + 1개 비교 제외: `\U`로 `é`를 대문자로 바꾸는 결과가 원본에서도 Windows ANSI 코드 페이지(Win32 locale traits)에 따라 달라지는 사례(`windowsCodepageDependent`). 이 1개는 기준 PC(코드 페이지 949)의 원본 결과라 Linux의 유니코드 결과와 다르며, Linux에서 원본과 같게 만들지는 결정 필요
- [x] 파일에서 찾기가 1만 개 파일 폴더에서 취소 가능하고 UI가 멈추지 않음 — `ui.find.responsiveness`(Windows, 실제 플랫폼): 1만 개 파일 검색 중 이벤트 루프 최장 정지 약 75 ms(기준 0.5 s), 취소 동작 확인. Linux는 `ui.find`(offscreen)에서 같은 검사(최장 정지 26 ms)
- [x] 원본에서 녹화한 매크로가 그대로 재생됨 — `tools/macro-compat/record-in-npp.ps1`로 원본 Notepad++에서 메뉴 명령·입력·정규식 모두 바꾸기를 녹화·저장한 매크로를 포트에서 재생해 같은 결과, 같은 동작을 포트에서 녹화하면 같은 단계 생성(`ui.find`, Windows·Linux 모두 자동 실행)
- [x] 로컬 CI 통과 — Windows MinGW/MSVC, Linux Docker × ADS/QT

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| `FindReplaceDlg.cpp`(7.1k)에 UI와 로직이 섞여 있음 | 1단계에서 로직 분리를 끝내 두고, 이 단계는 UI 연결만 |
| 대소문자 변환 등 문자 단위 처리의 UTF-8 정확성 | ICU/Qt 위임, 한글·터키어 등 특수 사례 테스트 |
| 파일에서 찾기 성능 | 파일별 병렬 처리, 큰 파일은 메모리 매핑 검토 |

## 8. 진행 현황

| ID | 상태 | 구현 위치 / 비고 |
|---|---|---|
| P3-01 | 완료 | `ui/search/FindReplaceDialog` (찾기·바꾸기·파일에서 찾기·표시 탭, 검색 기록 `config.xml`), `FindCommands.cpp` |
| P3-02 | 완료 | `ui/search/SearchResultsPanel`: 원본과 같은 결과 텍스트·`searchResult` 렉서·검색별 접기·F4/Shift+F4·결과 위치 열기 |
| P3-03 | 완료 | 파일에서 찾기/바꾸기: 25 ms 단위 작업 + 중첩 이벤트 루프(취소 가능). 열린 파일은 편집기 문서에서 바꾸고 저장(원본 동작). 프로젝트에서 찾기 탭은 5단계(프로젝트 패널) |
| P3-04 | 완료 | 표시 탭(북마크·지우기), 스타일 1–5 표시/지우기/이동/복사, 북마크 줄 잘라내기·복사·붙여넣기·삭제·반전 |
| P3-05 | 완료 | 증분 검색 막대, 검색 결과 내 찾기, 문자 범위 찾기, 줄/오프셋으로 이동 |
| P3-06 | 완료 | `core/editor/TextOps`(대소문자·정렬·줄·공백), `CommentOps`, `Sorters.h`, `ui/EditCommands.cpp` |
| P3-07 | 완료 | `core/editor/AutoCompletion`(API 파일·단어·경로 완성, 콜팁 오버로드, 짝 문자·닫는 태그), `AutoIndent` |
| P3-08 | 완료 | 스마트 하이라이트, 괄호 매칭, `core/editor/XmlTagMatcher`, `core/editor/UrlDetector`(원본 `PowerEditor/Test/UrlDetection` 150개 사례 일치) |
| P3-09 | 완료 | 매크로(녹화·재생·저장·여러 번 실행, `shortcuts.xml` 형식), `core/editor/ColumnOps` 열 편집기 |
| P3-10 | 완료 | 테스트: `core.textops`, `core.regexcompat`, `core.urldetect`, `core.autocompletion`, `ui.find`, `ui.find.responsiveness` |

계획과 다르게 한 점:
- 파일에서 찾기는 `QtConcurrent` 대신 메인 스레드에서 시간 분할로 실행한다. 검색 엔진이 Scintilla 문서(위젯)를 쓰므로 다른 스레드에서 쓸 수 없고, `processEvents()` 반복은 Windows에서 posted 이벤트가 밀려 UI가 멈추는 것을 확인했다.
- 숨긴 줄 표시는 원본의 이미지 대신 Scintilla 기본 화살표 마커를 쓴다(여백 클릭으로 다시 표시).
- 대화상자 문자열 번역(`<Dialog>` 블록)은 4단계에서 다른 대화상자와 함께 적용한다.
- `tests`: Windows에서 headless 플랫폼(offscreen, minimal)은 Qt 애니메이션 타이머 이벤트 안에서 중첩 루프가 수 초 멈춰 응답성 검사만 실제 플랫폼(`QT_QPA_PLATFORM=windows`)으로 실행한다(클립보드를 쓰는 다른 검사는 headless 유지).

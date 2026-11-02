# 1단계 세부 계획: 코어 추출 (`npp-core`)

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 8장, 부록 A(문자열)
- 기간: 6–8주
- 선행: [0단계](phase-0-preparation.md) 완료
- 다음 단계: [2단계 MVP](phase-2-mvp.md)

---

## 1. 목표

기존 Notepad++ 소스에서 UI와 무관한 로직을 꺼내 **플랫폼 독립 라이브러리 `npp-core`** 로 만든다.
2단계 이후의 Qt UI는 모두 이 코어 위에 올라간다.

- 코어는 표준 C++20 + **QtCore**만 사용한다 (QtGui/QtWidgets 금지).
- 내부 문자열은 **UTF-8 `std::string`** 으로 전환한다 (부록 A.4 규칙).
- 기존 설정 파일(`config.xml`, `langs.xml`, `stylers.xml`, `shortcuts.xml`, `session.xml`, `contextMenu.xml`, UDL, 테마)을 **같은 형식으로 읽고 쓴다**.
- 모든 공개 API는 헤드리스 단위 테스트로 검증한다.

## 2. 범위

### 2.1 코어로 옮기는 대상

| 모듈 (`app/core/…`) | 원본 | 원본 규모 | 내용 |
|---|---|---|---|
| `util/` | `MISC/Common/Common.*`, `Sorters.h`, `SortLocale.*` | 약 5.4k | 문자열·경로 유틸, 정렬 |
| `encoding/` | `Utf8_16.*`, `EncodingMapper.*`, `uchardet/` | 약 2k + 서드파티 | BOM 처리, 코드페이지 변환, 인코딩 감지 |
| `config/` | `Parameters.*` | 약 11k | 설정 모델(`NppGUI`, `ScintillaViewParams`, `NewDocDefaultSettings`, `PrintSettings` 등)과 XML 직렬화 |
| `lang/` | `Parameters.*`의 `Lang`, `LexerStyler`, `StyleArray`, `UserLangContainer` + `ScintillaEditView.cpp`의 `setXxxLexer` 85개 | 약 4k | 언어 정의, 스타일, 렉서 설정을 **데이터 테이블**로 |
| `buffer/` | `ScintillaComponent/Buffer.*`, `MISC/Common/FileInterface.*` | 약 3k | 문서 상태, 파일 로드/저장, 백업, 변경 감지 |
| `session/` | `Parameters.*`의 `Session`, `sessionFileInfo` | 약 0.5k | 세션 로드/저장 |
| `shortcut/` | `WinControls/shortcut/` 의 모델 부분 | 약 1k | 단축키 모델, `VK_*` ↔ 키 코드 변환 |
| `search/` | `FindReplaceDlg.cpp`의 검색 로직 부분 | 약 2k | 검색 옵션 모델, 찾기/바꾸기 엔진, 파일에서 찾기 |
| `i18n/` | `localization.*` | 약 1.6k | `nativeLang/*.xml` 파싱, 문자열 테이블 |
| `editor/` | `ScintillaEditView.*`의 비 UI 로직 | 약 2k | `IEditor` 인터페이스 기반 편집 명령 |

### 2.2 이번 단계에서 하지 않는 것

- UI(다이얼로그, 메뉴, 탭)는 만들지 않는다. 0단계 샘플 앱만 코어로 갈아끼워 검증한다.
- 플러그인 관련 코드(`MISC/PluginsManager`)는 7단계.
- Windows 전용 기능(레지스트리 파일 연결, 쉘 확장)은 6단계 `platform/win`.

## 3. 사전 조사 (1주차)

| ID | 작업 | 산출물 |
|---|---|---|
| P1-01 | `Parameters.cpp`, `Buffer.cpp`, `Common.cpp`, `NppIO.cpp`의 Win32 API 호출을 함수 단위로 목록화 (0단계 P0-18 이관) | `docs/core-win32-api-inventory.md` |
| P1-02 | 클래스별 의존 그래프: 어떤 클래스가 `HWND`, `ScintillaEditView`, `Notepad_plus`에 의존하는지 | 위 문서의 의존 표 |
| P1-03 | 문자열 변환 스크립트 작성 및 시험 적용 | `tools/convert-wstring.py` |
| P1-04 | 설정 파일 왕복 테스트용 샘플 수집: 실제 사용자 설정 폴더 2–3개(익명화) | `app/tests/data/configs/` |

### 3.1 문자열 변환 스크립트 (P1-03)

1단계에서 옮기는 파일에 한해 다음을 자동 처리하고, 나머지는 컴파일 오류를 보며 수동 수정한다.

| 변환 | 예 |
|---|---|
| 와이드 리터럴 | `L"abc"` → `"abc"` |
| 문자열 타입 | `std::wstring` → `std::string`, `std::wstring_view` → `std::string_view`, `wchar_t*` → `char*` |
| MS CRT 함수 | `_wcsicmp` → `npp::util::iequals`, `_wtoi` → `std::stoi`(래퍼), `swprintf_s` → `std::format` |
| Win32 변환 | `WcharMbcsConvertor` 호출 제거 (이미 UTF-8이므로) |
| 경로 | `wchar_t` 경로 조합 → `std::filesystem::path` |

스크립트는 변환 대상 목록을 출력만 하는 `--dry-run`을 지원한다. 변환 결과는 파일 단위로 커밋해 리뷰 가능하게 한다.

## 4. 설계

### 4.1 디렉터리와 타깃

```
app/core/
├─ CMakeLists.txt          # add_library(npp_core STATIC ...), Qt6::Core만 링크
├─ util/  encoding/  config/  lang/  buffer/  session/
├─ shortcut/  search/  i18n/  editor/
└─ platform/               # Qt가 제공하지 않는 OS 기능만 (1단계에서는 거의 비어 있음)
app/tests/core/            # 모듈별 단위 테스트 (Qt Test 또는 GoogleTest)
```

CMake에서 `npp_core`가 `Qt6::Gui`/`Qt6::Widgets`에 링크되면 실패하도록 검사 스크립트를 `ci/`에 추가한다 (`check-core-deps.sh`).

### 4.2 `IEditor` 인터페이스

```cpp
// app/core/editor/IEditor.h
namespace npp::core {
class IEditor {
public:
    virtual ~IEditor() = default;
    virtual intptr_t send(unsigned int msg, uintptr_t wParam = 0, intptr_t lParam = 0) const = 0;
};
}
```

- UI 계층의 `ScintillaEditBase`는 어댑터로 감싸 이 인터페이스를 구현한다.
- 테스트에서는 같은 어댑터를 화면 없이(`QT_QPA_PLATFORM=offscreen`) 사용한다.
- 기존 `ScintillaEditView::execute(SCI_xxx, …)` 호출 1,843곳은 `editor.send(SCI_xxx, …)`로 1:1 대응된다.

### 4.3 설정 모델 (`config/`)

- `NppParameters` 싱글톤(약 9.5k줄)을 기능별 클래스로 나눈다.

| 새 클래스 | 원본 | 파일 |
|---|---|---|
| `AppPaths` | `_nppPath`, `_userPath`, `doLocalConf.xml`, 클라우드 경로 | 경로 결정 (`QStandardPaths`) |
| `GuiConfig` | `NppGUI` | `config.xml` `<GUIConfigs>` |
| `EditorConfig` | `ScintillaViewParams` | `config.xml` |
| `LanguageRegistry` | `Lang`, `LangType` | `langs.xml` |
| `StyleRegistry` | `LexerStylerArray`, `StyleArray`, `GlobalOverride` | `stylers.xml`, 테마 |
| `UserLanguageRegistry` | `UserLangContainer` | `userDefineLang.xml`, `userDefineLangs/*.xml` |
| `ShortcutConfig` | `CommandShortcut`, `MacroShortcut`, `UserCommand`, `PluginCmdShortcut`, `ScintillaKeyMap` | `shortcuts.xml` |
| `FindHistory` | `FindHistory` | `config.xml` `<FindHistory>` |
| `SessionStore` | `Session` | `session.xml` |
| `ContextMenuConfig` | `contextMenu.xml` 처리 | `contextMenu.xml` |

- 설정 경로 규칙:

| 플랫폼 | 사용자 설정 폴더 | 포터블 모드 |
|---|---|---|
| Windows | `%APPDATA%\Notepad++` (원본과 동일) | 실행 파일 옆 `doLocalConf.xml` 존재 시 실행 파일 폴더 |
| Linux | `$XDG_CONFIG_HOME/notepad++` | 동일 규칙 |
| macOS | `~/Library/Application Support/Notepad++` | 동일 규칙 |

- **왕복 원칙**: 읽은 뒤 바로 저장하면 원본 XML과 의미상 같아야 한다 (모르는 요소·속성도 보존). pugixml 노드를 보관해 모르는 부분을 그대로 다시 쓴다.

### 4.4 렉서 설정 테이블 (`lang/`)

`ScintillaEditView`의 `setCppLexer`, `setHTMLLexer` 등 85개 함수를 다음 형태의 데이터로 바꾼다.

```cpp
struct LexerSpec {
    LangType                         lang;
    const char*                      lexilla;      // "cpp", "hypertext", ...
    std::array<int8_t, 9>            keywordSet;   // instre1..type7 -> Scintilla keyword set (-1 = 미사용)
    std::vector<std::pair<const char*, const char*>> properties; // fold.compact 등
    std::vector<SubStyleSpec>        subStyles;    // substyle1..8
};
```

- 특수 처리(HTML 내장 JS, PHP, XML 태그 매칭 등)는 테이블의 `postSetup` 콜백으로 남긴다.
- 검증: 언어별 샘플 파일을 열어 스타일 번호 배열을 원본 Notepad++(Windows)와 비교하는 **골든 테스트**. 원본 결과는 Windows에서 Notepad++ v8.9.8.1 + 덤프 플러그인 또는 수동 스크립트로 1회 추출해 `app/tests/data/golden/`에 저장.

### 4.5 버퍼와 파일 (`buffer/`)

- `Buffer`와 `FileManager`의 공개 API(`loadFile`, `saveBuffer`, `reloadBuffer`, `backupCurrentBuffer`, `moveFile`, `deleteFile`, `checkFilesystemChanges` 등)를 유지하되 문자열과 경로를 UTF-8/`std::filesystem::path`로 바꾼다.
- `Win32_IO_File` → `QFile`/`QSaveFile` 기반 `FileIO`. 원본의 "쓰기 전 임시 파일 → 교체" 동작과 쓰기 실패 시 복구 동작을 유지한다.
- `getFileAttributesExWithTimeout`(네트워크 드라이브 지연 대응) → 별도 스레드에서 `QFileInfo` 조회 + 타임아웃.
- 파일 변경 감지: 코어는 "확인 함수"만 제공하고, 감시(`QFileSystemWatcher`)는 2단계 UI가 연결한다.
- 문서 저장소: Scintilla 문서 포인터(`SCI_CREATEDOCUMENT`)를 `Buffer`가 보유하는 원본 구조를 유지한다.
- 대용량 파일 정책(`LargeFileRestriction`) 이식.

### 4.6 인코딩 (`encoding/`)

| 기능 | 원본 | 이식 |
|---|---|---|
| BOM 감지/쓰기 (UTF-8, UTF-16 LE/BE) | `Utf8_16_Read/Write` | 그대로, `wchar_t` 가정 제거 |
| 인코딩 자동 감지 | `uchardet` + `FileManager::detectCodepage` | 그대로 |
| 코드페이지 ↔ 유니코드 | `MultiByteToWideChar` | ICU 또는 Qt `QStringConverter`(ICU 백엔드). Windows Qt 빌드의 ICU 유무를 1주차에 확인하고, 없으면 ICU를 직접 의존성으로 추가 |
| 인코딩 메뉴 목록 | `EncodingMapper` | 코드페이지 번호 ↔ 인코딩 이름 테이블 |

검증: `app/tests/data/encodings/`에 인코딩별 샘플(UTF-8 ±BOM, UTF-16 LE/BE, CP949, Shift_JIS, GB2312, Windows-1252 등)을 두고 읽기→쓰기 바이트 일치 테스트.

### 4.7 검색 (`search/`)

- 찾기/바꾸기 옵션(`FindOption`), 정규식/확장 모드 변환(`\n`, `\t` 등), 바꾸기 문자열 처리를 코어로.
- "파일에서 찾기"의 파일 열거·필터·제외 규칙과 결과 모델(파일, 줄, 일치 범위)을 코어로. 실제 검색은 화면 없는 Scintilla 문서(`SCI_CREATEDOCUMENT` + boostregex)로 수행해 원본과 같은 정규식 동작을 보장한다.
- UI(다이얼로그, 결과 패널)는 3단계.

### 4.8 다국어 (`i18n/`)

- `nativeLang/*.xml` 94개를 읽어 `(분류, ID) → 문자열` 테이블 제공.
- 메뉴 ID, 다이얼로그 컨트롤 ID 체계는 그대로 유지한다 (UI가 같은 ID로 조회).

## 5. 작업 순서와 일정

| 주차 | 작업 |
|---|---|
| 1 | P1-01~04 사전 조사, `npp_core` 타깃과 테스트 골격, `util/` |
| 2 | `encoding/` + 인코딩 테스트, `i18n/` |
| 3–4 | `config/` (`AppPaths`, `GuiConfig`, `EditorConfig`, `FindHistory`, `ShortcutConfig`, `SessionStore`) + 왕복 테스트 |
| 4–5 | `lang/` (`LanguageRegistry`, `StyleRegistry`, `UserLanguageRegistry`, 렉서 테이블 85개) + 골든 테스트 |
| 5–6 | `buffer/` (로드/저장/백업/변경 확인) + 파일 테스트 |
| 7 | `search/`, `editor/` |
| 8 | 0단계 샘플 앱을 코어 기반으로 교체, 3플랫폼 로컬 CI, 문서화 |

## 6. 테스트

| 종류 | 내용 | 위치 |
|---|---|---|
| 단위 | 모듈별 공개 API | `app/tests/core/` |
| 왕복 | 실제 사용자 설정 폴더 읽기→쓰기→비교 | `app/tests/data/configs/` |
| 인코딩 | 인코딩별 바이트 일치 | `app/tests/data/encodings/` |
| 골든 | 언어별 스타일 배열을 원본과 비교 | `app/tests/data/golden/` |
| 기존 자산 | `PowerEditor/Test/UrlDetection`, `xmlValidator` CTest 편입 | |

모든 테스트는 `ci/local-ci.sh`로 Windows(MinGW, MSVC), Linux, macOS에서 실행한다.

## 7. 완료 기준

- [ ] `npp_core`가 QtCore 외 Qt 모듈에 의존하지 않음 (검사 스크립트 통과)
- [ ] 코어 소스에 `<windows.h>`, `wchar_t`, `std::wstring`, `L"` 없음 (`platform/win` 제외, 검사 스크립트로 확인)
- [ ] 설정 파일 7종 왕복 테스트 통과
- [ ] 인코딩 샘플 전부 바이트 일치
- [ ] 렉서 골든 테스트: 주요 20개 언어 통과, 나머지 목록화
- [ ] 0단계 샘플 앱이 코어 API로 동작 (기존 설정 폴더 로드)
- [ ] 3플랫폼 로컬 CI 통과

## 8. 리스크

| 리스크 | 대응 |
|---|---|
| `NppParameters`가 UI 객체(`HWND`, `HFONT`, 이미지 목록)를 함께 보관 | 모델과 UI 리소스를 분리, UI 리소스는 2단계에서 UI 계층이 보유 |
| 문자열 전환 중 의미 변화 (문자 수 vs 바이트 수) | 열 위치·길이 계산 코드는 UTF-8 유틸로 일원화하고 비 BMP 문자 테스트 추가 |
| Windows Qt에 ICU 없음 | ICU 직접 의존성 또는 Windows에서만 Win32 API 사용(`platform/win`) |
| 골든 데이터 추출 비용 | 주요 20개 언어부터, 나머지는 3–5단계 중 점진적으로 |

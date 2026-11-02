# 7단계 세부 계획: 플러그인 API

- 상위 문서: [개발 계획서](qt-cross-platform-port-plan.md) 7장, 8장
- 기간: 6주 이상
- 선행: [6단계 패키징/안정화](phase-6-packaging-stabilization.md) 완료 (베타 배포 후)

---

## 1. 목표

3개 플랫폼에서 동작하는 **새 플러그인 인터페이스**를 만들고, 기존 Notepad++ 플러그인 소스를 적은 수정으로 다시 빌드할 수 있는 길을 연다.

기존 Win32 DLL 플러그인은 `HWND`와 `SendMessage(NPPM_*)`에 의존하므로 **바이너리 호환은 불가능**하다 (Windows 포함).

## 2. 현 구조 분석

| 요소 | 원본 | 내용 |
|---|---|---|
| 플러그인 진입점 | `PluginInterface.h` | `setInfo(NppData)`, `getName()`, `getFuncsArray()`, `beNotified(SCNotification*)`, `messageProc(UINT, WPARAM, LPARAM)`, `isUnicode()` |
| 앱 → 플러그인 데이터 | `NppData` | `_nppHandle`, `_scintillaMainHandle`, `_scintillaSecondHandle` (모두 `HWND`) |
| 플러그인 → 앱 요청 | `Notepad_plus_msgs.h` | `NPPM_*` 메시지 253개 (`SendMessage(nppHandle, NPPM_xxx, w, l)`) |
| 플러그인 → Scintilla | `SendMessage(scintillaHandle, SCI_xxx, w, l)` 또는 직접 함수 포인터(`SCI_GETDIRECTFUNCTION`) |
| 알림 | `NPPN_*` 알림 + Scintilla `SCN_*` | `beNotified`로 전달 |
| 메뉴 | `FuncItem` 배열 | 플러그인 메뉴 항목, 단축키, 체크 상태 |
| 도킹 | `NPPM_DMMREGASDCKDLG` 등 + `tTbData` | 플러그인 다이얼로그를 도킹 패널로 |
| 관리자 | `PluginsManager.*`, `PluginsAdmin` | 로드, 목록, 설치 |

## 3. 설계

### 3.1 새 C ABI (`npp_plugin_api.h`)

C++ ABI는 컴파일러마다 달라 플러그인 경계에서는 **C 함수와 POD 구조체만** 사용한다.

```c
#define NPP_PLUGIN_API_VERSION 1

typedef struct NppApi {
    uint32_t    version;
    /* 앱 정보 */
    const char* (*appVersion)(void);
    /* 문서·뷰 */
    intptr_t    (*currentView)(void);
    intptr_t    (*sendToEditor)(intptr_t view, unsigned msg, uintptr_t w, intptr_t l); /* SCI_* */
    int         (*getCurrentFilePath)(char* utf8Buf, size_t bufSize);
    int         (*openFile)(const char* utf8Path);
    /* 메뉴 명령 실행 */
    void        (*runMenuCommand)(int idmCommand);
    /* 도킹 */
    int         (*registerPanel)(const char* panelId, const char* title, void* qWidget);
    void        (*showPanel)(const char* panelId, int show);
    /* 설정 경로 */
    int         (*pluginConfigDir)(char* utf8Buf, size_t bufSize);
    /* ... NPPM_* 대응 함수 계속 */
} NppApi;

typedef struct NppPluginInfo {
    uint32_t    apiVersion;
    const char* name;
    const char* version;
} NppPluginInfo;

/* 플러그인이 내보내는 함수 */
NPP_EXPORT const NppPluginInfo* npp_plugin_info(void);
NPP_EXPORT int  npp_plugin_init(const NppApi* api);
NPP_EXPORT int  npp_plugin_menu_items(const NppMenuItem** items);
NPP_EXPORT void npp_plugin_notify(const NppNotification* n);
NPP_EXPORT void npp_plugin_shutdown(void);
```

원칙:
- 문자열은 UTF-8 (`char*`). 프로젝트 문자열 정책과 일치.
- 구조체 첫 필드에 버전/크기를 두어 이후 확장 시 하위 호환.
- `sendToEditor`는 Scintilla 메시지를 그대로 전달 → 기존 플러그인의 Scintilla 사용 코드는 거의 그대로 재사용.
- 플러그인 UI가 Qt 위젯이면 `registerPanel`에 `QWidget*`를 넘긴다. 이 경우 **플러그인과 앱이 같은 Qt 버전에 링크**되어야 한다 (제약으로 문서화). UI 없는 플러그인은 Qt 의존 불필요.

### 3.2 `NPPM_*` 호환 계층

기존 플러그인 소스 포팅을 돕는 헤더 전용 계층.

```c
/* npp_compat.h: 기존 코드의 SendMessage(nppHandle, NPPM_xxx, w, l) 를 대체 */
intptr_t npp_compat_send(unsigned nppmMessage, uintptr_t w, intptr_t l);
```

- 253개 메시지를 사용 빈도로 분류: 1차(문서 경로, 현재 언어, 메뉴 명령, 버퍼 ID, 인코딩, 설정 경로 등 상위 50개) → 2차 → 미지원(목록 문서화).
- `wchar_t*` 인자를 쓰는 메시지는 UTF-8 변환 버전으로 대응 (원본도 플러그인은 UTF-16).
- 사용 빈도 조사: 인기 플러그인(Compare, NppExec, XML Tools, JSON Viewer, PythonScript, DSpellCheck 등) 소스에서 `NPPM_` 사용 집계.

### 3.3 로더

- `QLibrary`로 로드. 플러그인 폴더:

| 플랫폼 | 경로 |
|---|---|
| Windows | `<설치 폴더>\plugins\<이름>\<이름>.dll`, 사용자 `%APPDATA%\Notepad++\plugins\` |
| Linux | `<설치 폴더>/plugins/<이름>/lib<이름>.so`, `$XDG_DATA_HOME/notepad++/plugins/` |
| macOS | `<번들>/Contents/PlugIns/<이름>/lib<이름>.dylib`, `~/Library/Application Support/Notepad++/plugins/` |

- `apiVersion` 불일치 시 로드 거부 + 알림. 로드 실패·초기화 예외는 플러그인 단위로 격리하고 해당 플러그인만 비활성화.
- 플러그인 메뉴: "플러그인" 메뉴 아래 플러그인별 하위 메뉴. 단축키는 단축키 매퍼 "플러그인 명령" 탭(4단계)과 연결.
- 알림: 원본 `NPPN_*`(파일 열기/저장/닫기, 버퍼 활성화, 준비 완료, 종료 등)와 `SCN_*`를 같은 의미로 전달.

### 3.4 보안

- 플러그인은 앱과 같은 권한으로 실행된다 — 신뢰할 수 있는 플러그인만 설치하도록 안내.
- 원본 `verifySignedfile`(Authenticode)은 Windows 전용이므로, 외부 배포 시 서명 정책(예: 해시 목록, GPG 서명)을 별도로 결정.

### 3.5 스크립팅 (선택, 3차)

- 수요가 높은 기능을 스크립트로 대체: Python(PythonScript 대응) 또는 Lua 내장 검토.
- 이 경우 스크립트 API는 `NppApi`를 감싸 노출.

## 4. 작업 목록

| ID | 작업 | 주차 |
|---|---|---|
| P7-01 | 인기 플러그인 `NPPM_*` 사용 빈도 조사, 1차 대응 목록 확정 | 1 |
| P7-02 | `npp_plugin_api.h` v1 설계·리뷰 | 1–2 |
| P7-03 | 로더, 플러그인 메뉴, 알림 전달, 오류 격리 | 2–3 |
| P7-04 | 도킹 패널 등록, 단축키 연결 | 3 |
| P7-05 | `npp_compat.h` 1차 50개 메시지 | 4 |
| P7-06 | 샘플 플러그인 2개 (UI 없음: 텍스트 변환 / UI 있음: Qt 패널) | 4–5 |
| P7-07 | 기존 공개 플러그인 1개를 이식해 검증 | 5–6 |
| P7-08 | 플러그인 개발 문서 (`docs/plugin-development.md`) | 6 |
| P7-09 | (선택) 플러그인 관리자: 로컬 폴더 목록·활성/비활성 | 이후 |

## 5. 테스트

- 샘플 플러그인 2개가 3플랫폼에서 로드·메뉴 실행·알림 수신·패널 표시.
- 잘못된 플러그인(버전 불일치, 초기화 실패, 예외)으로 앱이 죽지 않는지.
- 이식한 공개 플러그인의 주요 기능 수동 확인.

## 6. 완료 기준

- [ ] `npp_plugin_api.h` v1 확정·문서화
- [ ] 샘플 플러그인 2개가 3플랫폼에서 동작
- [ ] 공개 플러그인 1개 이식 성공
- [ ] 플러그인 개발 문서 작성

## 7. 리스크

| 리스크 | 대응 |
|---|---|
| 플러그인과 앱의 Qt 버전 일치 요구 | UI 없는 플러그인은 Qt 비의존, UI 플러그인은 앱 Qt 버전 명시 |
| `NPPM_*` 의미 중 Win32 특화(창 핸들 반환 등) | 대응 불가 목록으로 문서화, 대체 함수 제공 |
| 플러그인 생태계가 생기지 않음 | 핵심 기능은 내장 기능화, 스크립팅으로 진입 장벽 낮춤 |

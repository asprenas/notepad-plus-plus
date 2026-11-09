# 플러그인 개발 문서 (Notepad++ Qt 플러그인 API v1)

- 상위 문서: [7단계 세부 계획](../plan/phase-7-plugin-api.md)
- 헤더: `app/plugin-api/include/` — `npp_plugin_api.h`(C ABI), `npp_compat.h`·`npp_compat_win32.h`(이식 도움), `npp_msgs.h`(NPPM 번호)
- 예제: `app/plugins/samples/textcase`(C, UI 없음), `app/plugins/samples/docinfo`(C++/Qt 패널), `app/plugins/ports/mimetools`(기존 플러그인 이식)
- 메시지 호환 목록: [plugin-api-nppm.md](plugin-api-nppm.md)

## 1. 개요

Notepad++ Qt 플러그인은 **C 함수 몇 개를 내보내는 공유 라이브러리**(Windows `.dll`, Linux `.so`)다.
앱은 시작할 때 플러그인 폴더의 라이브러리를 읽고, 함수 포인터 표(`NppApi`)를 넘겨준다. 플러그인은 이 표로
편집기에 Scintilla 메시지(`SCI_*`)를 보내고, 문서를 열고, 메뉴·패널·툴바 버튼을 등록한다.

- 원본 Notepad++의 Win32 DLL 플러그인은 **바이너리 호환이 안 된다** (Windows 포함). `HWND`와 `SendMessage`
  대신 `NppApi`를 쓰므로 소스를 고쳐 다시 빌드해야 한다. 고칠 양을 줄이는 계층이 `npp_compat.h`다 (8장).
- 경계는 C ABI(C 함수, POD 구조체)라서 컴파일러가 달라도 된다 (예: MSVC로 빌드한 플러그인을 MinGW 빌드 앱이 사용).
  단 **Qt 위젯을 넘기는 플러그인**(패널)은 앱과 같은 Qt 버전(메이저.마이너), 같은 ABI의 컴파일러로 빌드해야 한다.
- 문자열은 모두 UTF-8.

## 2. 빠른 시작 (C)

```c
#define NPP_PLUGIN_IMPLEMENTATION
#include <npp_plugin_api.h>
#include <Scintilla.h>

static const NppApi* g_api;

static void hello(void)
{
    g_api->sendToEditor(NPP_VIEW_CURRENT, SCI_REPLACESEL, 0, (intptr_t)"Hello from a plugin");
}

static NppMenuItem g_items[] = {
    {"Say hello", hello, 0, {1, 1, 0, 'H'}, 0},   /* Ctrl+Alt+H */
};

NPP_EXPORT const NppPluginInfo* npp_plugin_info(void)
{
    static const NppPluginInfo info = {NPP_PLUGIN_API_VERSION, "Hello", "1.0"};
    return &info;
}

NPP_EXPORT int npp_plugin_init(const NppApi* api, NppPluginHandle self)
{
    (void)self;
    g_api = api;
    return 0; /* 0 = 성공. 그 밖의 값이면 앱이 플러그인을 비활성화한다 */
}

NPP_EXPORT int npp_plugin_menu_items(NppMenuItem** items)
{
    *items = g_items;
    return 1;
}
```

빌드 (앱 소스 트리 밖, CMake):

```cmake
cmake_minimum_required(VERSION 3.24)
project(hello C)
set(NPPQT_SDK "<저장소>/app/plugin-api/include;<저장소>/scintilla/include")
add_library(hello MODULE hello.c)
target_include_directories(hello PRIVATE ${NPPQT_SDK})
set_target_properties(hello PROPERTIES C_VISIBILITY_PRESET hidden)
if(WIN32)
    set_target_properties(hello PROPERTIES PREFIX "")        # hello.dll
endif()
if(MINGW)
    target_link_options(hello PRIVATE -static)                # MinGW 런타임 DLL 의존 없음
endif()
```

앱 소스 트리 안에서는 `npp_add_plugin(<이름> SOURCES ... [QT])`(`app/plugin-api/CMakeLists.txt`)를 쓴다.
결과는 `<빌드>/plugins/<이름>/<라이브러리>`이고, 이 폴더를 아래 플러그인 폴더로 복사하면 된다.

## 3. 설치 위치와 로드

플러그인 하나 = 폴더 하나. 폴더 이름과 라이브러리 이름이 같아야 한다 (원본 규칙).

| 플랫폼 | 앱 폴더 | 사용자 폴더 |
|---|---|---|
| Windows | `<설치 폴더>\plugins\<이름>\<이름>.dll` | `%APPDATA%\Notepad++Qt\plugins\<이름>\<이름>.dll` (포터블: `<앱>\plugins`) |
| Linux | `<prefix>/lib/notepadpp-qt/plugins/<이름>/lib<이름>.so` 또는 `<앱 폴더>/plugins/...` | `~/.config/notepad++qt/plugins/<이름>/lib<이름>.so` (`<이름>.so`도 됨) |

- 사용자 폴더는 **설정 폴더 아래 `plugins`** 다 (`-settingsDir`, 포터블 모드를 따른다). 설정 > 가져오기 > "플러그인 가져오기"와
  플러그인 메뉴 > "플러그인 폴더 열기"가 이 폴더를 쓴다.
- 앱 폴더를 먼저 읽고, 같은 이름(대소문자, `lib`, 확장자 무시)의 플러그인은 처음 것만 로드한다.
- 플러그인을 넣거나 뺀 뒤에는 앱을 다시 시작한다.
- 명령줄 `-noPlugin`(원본과 같음) 또는 환경 변수 `NPP_QT_NO_PLUGINS`가 있으면 플러그인을 읽지 않는다.
- 플러그인 설정 파일은 `NPPM_GETPLUGINSCONFIGDIR`(`<설정 폴더>/plugins/config`)에 둔다.

## 4. 진입점과 호출 순서

| 함수 | 필수 | 설명 |
|---|---|---|
| `const NppPluginInfo* npp_plugin_info(void)` | 예 | API 버전, 이름(메뉴에 표시), 버전 문자열 |
| `int npp_plugin_init(const NppApi*, NppPluginHandle)` | 예 | 0 = 성공. `NppApi*`와 `self`는 끝날 때까지 유효 |
| `int npp_plugin_menu_items(NppMenuItem**)` | | 메뉴 항목 배열과 개수. 배열은 플러그인이 계속 가지고 있어야 한다 |
| `void npp_plugin_notify(const NppNotification*)` | | `NPPN_*`, `SCN_*` 알림 |
| `intptr_t npp_plugin_message(unsigned, uintptr_t, intptr_t)` | | 다른 플러그인의 `NPPM_MSGTOPLUGIN` |
| `void npp_plugin_shutdown(void)` | | 종료 직전 (`NPPN_SHUTDOWN` 다음) |

순서: `npp_plugin_info` → (API 버전 확인) → `npp_plugin_init` → `npp_plugin_menu_items`(앱이 `cmdId`를 채움) →
메뉴·도킹 배치 복원 → `NPPN_TBMODIFICATION` → `NPPN_READY` → … → `NPPN_BEFORESHUTDOWN` → `NPPN_SHUTDOWN` → `npp_plugin_shutdown`.

- `npp_plugin_init`은 창이 만들어지는 중에 불린다: 문서·편집기 내용은 아직 없다. 문서 작업은 `NPPN_READY` 이후에.
- 패널은 `npp_plugin_init`에서 등록하면 지난 세션의 도킹 배치(위치·표시 여부)가 복원된다.
- 라이브러리는 프로그램이 끝날 때까지 내려지지 않는다.
- 플러그인은 앱의 메인(UI) 스레드에서만 호출된다. 다른 스레드에서 `NppApi`를 부르지 않는다.

## 5. NppApi

`NppApi.size`는 앱이 아는 구조체 크기다. 나중에 추가되는 함수는 끝에 붙으므로 새 함수를 쓰기 전에
`NPP_API_HAS(api, 멤버)`로 확인한다. `NPP_PLUGIN_API_VERSION`이 다르면 앱이 플러그인을 거부한다.

| 함수 | 설명 |
|---|---|
| `appVersion()` | 앱 버전 문자열 |
| `mainWindow()` | 메인 창 `QWidget*` (Qt 대화상자의 부모) |
| `pluginConfigDir(buf, size)` | 플러그인 설정 폴더 |
| `setStatusText(text)` | 상태 표시줄 임시 메시지 |
| `isDarkMode()` | 다크 모드 여부 (`NPPN_DARKMODECHANGED`로 변경 알림) |
| `messageBox(text, title, type)` | 모달 메시지 상자 (`NPP_MB_*`, 반환 `NPP_ID*`) — Qt 없는 플러그인용 |
| `currentView()` | 활성 뷰 0/1 |
| `sendToEditor(view, SCI_xxx, w, l)` | 편집기에 Scintilla 메시지. `NPP_VIEW_CURRENT` = 활성 뷰 |
| `currentBufferId()`, `bufferPath(id, ...)`, `currentFilePath(...)` | 문서 ID와 경로 (제목 없는 문서는 탭 이름) |
| `openFile(path)`, `activateBuffer(id)` | 파일 열기, 문서 활성화 |
| `runMenuCommand(id)` | 메인 메뉴 명령(`IDM_*`) 또는 플러그인 `cmdId` 실행 |
| `setMenuItemCheck(cmdId, checked)` | 메뉴 항목 체크 표시 |
| `registerPanel(self, id, title, QWidget*)` | 도킹 패널 등록 (처음엔 숨김), 위젯 소유권은 앱으로 |
| `showPanel(self, id, show)`, `isPanelVisible(self, id)` | 패널 표시/숨김 |
| `nppMessage(NPPM_xxx, w, l)` | 원본 `NPPM_*` 메시지 ([목록](plugin-api-nppm.md)) |
| `pluginFilePath(self, buf, size)` | 플러그인 라이브러리 경로 (같은 폴더의 아이콘·데이터 파일용) |
| `addToolbarIcon(cmdId, icon, darkIcon)` | 툴바 버튼 (PNG/ICO/BMP 파일). `NPPN_TBMODIFICATION`에서 호출 |

버퍼를 채우는 함수(`buf, bufSize`)는 항상 NUL로 끝내고 전체 길이(NUL 제외)를 돌려준다. `bufSize` 0으로 길이만 물을 수 있다.

## 6. 메뉴와 단축키

- 플러그인 메뉴 아래에 플러그인 이름의 하위 메뉴가 생기고 항목이 순서대로 들어간다 ("플러그인 폴더 열기" 위).
  `name`이 `NULL`/빈 문자열이거나 `callback`이 `NULL`이면 구분선.
- 앱이 항목마다 명령 ID(22000–22999, 원본 `ID_PLUGINS_CMD`)를 `cmdId`에 써 준다. 체크 표시, `runMenuCommand`, `NPPM_GETSHORTCUTBYCMDID`에 쓴다.
- 기본 단축키는 `shortcut`(Win32 가상 키 코드, 모든 플랫폼 공통 — 문자 키는 대문자 `'A'`..`'Z'`, 숫자 `'0'`..`'9'`, F1 = `0x70`).
  사용자가 단축키 매퍼의 "플러그인 명령" 탭에서 바꾸면 `shortcuts.xml`의 `<PluginCommands><PluginCommand moduleName="<라이브러리 파일 이름>" internalID="<항목 순서>" .../>`에
  원본과 같은 형식으로 저장되고, 이후 시작 때 기본값보다 우선한다. 바뀌면 `NPPN_SHORTCUTREMAPPED`.
- 플러그인이 비활성화되면 그 메뉴 항목은 흐리게 표시된다.

## 7. 알림

`NppNotification`: `code`(NPPN_*/SCN_*), `view`(SCN이면 0/1, NPPN이면 -1), `id`(원본 `idFrom`: 대부분 버퍼 ID), `scn`(SCN이면 Scintilla의 `SCNotification*`).

| 알림 | 보내는 때 | `id` |
|---|---|---|
| `NPPN_READY` | 시작 완료 (세션·명령줄 파일 열린 뒤) | |
| `NPPN_TBMODIFICATION` | `NPPN_READY` 직전, 툴바 버튼을 등록할 때 | |
| `NPPN_FILEBEFORELOAD` / `NPPN_FILELOADFAILED` | 파일 읽기 전 / 실패 | |
| `NPPN_FILEBEFOREOPEN`, `NPPN_FILEOPENED` | 파일을 연 뒤 (세션 복원 포함은 `FILEOPENED`) | 버퍼 |
| `NPPN_FILEBEFORESAVE`, `NPPN_FILESAVED` | 저장 전 (문서를 고칠 수 있음) / 저장 성공 후 | 버퍼 |
| `NPPN_FILEBEFORECLOSE`, `NPPN_FILECLOSED` | 문서를 닫기 전 / 후 | 버퍼 |
| `NPPN_BUFFERACTIVATED` | 문서 전환 | 버퍼 |
| `NPPN_LANGCHANGED` | 문서 언어 변경 | 버퍼 |
| `NPPN_WORDSTYLESUPDATED` | 스타일 설정 적용 | |
| `NPPN_DARKMODECHANGED` | 다크/라이트 전환 | |
| `NPPN_NATIVELANGCHANGED` | UI 언어 변경 | |
| `NPPN_SHORTCUTREMAPPED` | 단축키 매퍼 저장 | |
| `NPPN_BEFORESHUTDOWN`, `NPPN_CANCELSHUTDOWN`, `NPPN_SHUTDOWN` | 종료 시작 / 취소 / 종료 (문서는 아직 열려 있음) | |

`SCN_*`은 두 편집기의 Scintilla 알림을 그대로 전달한다 (`SCN_MODIFIED`는 앱의 `SCI_SETMODEVENTMASK`를 따르며,
`NPPM_ADDSCNMODIFIEDFLAGS`로 넓힐 수 있다). 자주 오는 알림(`SCN_UPDATEUI`, `SCN_PAINTED`)에서는 무거운 일을 하지 않는다.

## 8. 도킹 패널, 툴바, Qt

- 패널은 `QWidget`을 `registerPanel`에 넘겨 등록한다. 앱이 위젯을 소유하고 도킹 관리자(ADS 또는 QDockWidget)에 넣는다.
  사용자가 닫기 단추로 숨긴 것을 알려면 위젯의 `QEvent::Show/Hide`를 본다 (`docinfo.cpp`의 `VisibilityWatcher`).
- 패널 ID는 플러그인 안에서만 유일하면 된다 (앱 내부 ID는 `plugin.<모듈>.<패널 ID>`).
- **Qt 버전 제약**: Qt 위젯을 쓰는 플러그인은 앱이 쓰는 Qt와 같은 메이저.마이너 버전(현재 6.12)에, Windows에서는 같은 컴파일러
  계열(MinGW 빌드 앱 ↔ MinGW, MSVC 빌드 앱 ↔ MSVC)로 빌드한다. UI 없는 플러그인은 Qt가 필요 없다.
- 툴바 버튼: `NPPN_TBMODIFICATION`에서 `addToolbarIcon(cmdId, "<폴더>/icon.png", "<폴더>/icon_dark.png")`. 아이콘은 툴바 크기(16/32)로 줄여 쓴다.

## 9. 오류 격리

- 로드 실패(라이브러리 아님, 의존 DLL 없음), API 버전 불일치, 원본 Win32 플러그인(`getName`/`setInfo`만 내보냄),
  `npp_plugin_init` 실패(0이 아닌 값)는 그 플러그인만 건너뛰고, 시작 후 모달이 아닌 경고 상자에 모아 보여준다 (stderr에도 기록).
- 플러그인 함수(메뉴 명령, 알림, 메시지)가 C++ 예외를 던지면 앱이 잡아서 **그 플러그인을 비활성화**하고 보고한다.
  다른 플러그인과 편집은 계속된다. 단, 예외가 경계를 넘으려면 앱과 같은 C++ 런타임이어야 한다 — 예외는 플러그인 안에서 처리하는 것이 원칙.
- 잘못된 메모리 접근 같은 충돌은 잡지 못한다 (앱이 크래시 보고서를 남김: 6단계).

## 10. 기존 Notepad++ 플러그인 이식

`npp_compat.h`(+ 필요하면 `npp_compat_win32.h`)가 원본 플러그인 코드의 모양을 유지해 준다.

| 원본 | 이식 후 |
|---|---|
| `#include "PluginInterface.h"` (+ `<windows.h>`) | `#include <npp_compat_win32.h>` (또는 `npp_compat.h`) |
| `setInfo(NppData)` | `npp_plugin_init`: `nppData = npp_compat_init(api, self);` |
| `getName()`, `isUnicode()` | `npp_plugin_info` |
| `getFuncsArray(int*)` | `npp_plugin_menu_items`: `return npp_compat_menu_items(funcItem, nbFunc, items);` |
| `beNotified(SCNotification*)` | `npp_plugin_notify`: `npp_compat_notify(n, beNotified);` (함수는 그대로 둔다) |
| `messageProc(UINT, WPARAM, LPARAM)` | `npp_plugin_message` |
| `::SendMessage(nppData._nppHandle, NPPM_x, w, l)` | 그대로 (`npp_compat_win32.h`) 또는 `npp_compat_send(NPPM_x, w, l)` |
| `::SendMessage(nppData._scintillaMainHandle, SCI_x, w, l)` | 그대로 또는 `npp_send(h, SCI_x, w, l)` |
| `::MessageBox(...)`, `TCHAR`, `TEXT()`, `lstrcpy` | 그대로 (`npp_compat_win32.h`, `TCHAR`=`char` UTF-8) |
| `DllMain`의 메뉴 초기화 | 일반 함수로 옮겨 `npp_plugin_menu_items`에서 호출 |
| `wchar_t` 경로·문자열 | UTF-8 `char` (버퍼 크기는 바이트) |
| Win32 대화상자·리소스(`.rc`) | Qt 위젯 또는 `messageBox` |
| 도킹 대화상자(`tTbData`, `NPPM_DMM*`) | `registerPanel` / `showPanel` |
| 툴바 아이콘(`NPPM_ADDTOOLBARICON_*`) | `addToolbarIcon` |

주의: `npp_compat_win32.h`를 쓰는 소스 파일에서는 `<windows.h>`를 포함하지 않는다 (`SendMessage`/`MessageBox` 매크로 충돌).
예: MIME Tools 이식 — 변경 내역 [app/plugins/ports/mimetools/PORTING.md](../../app/plugins/ports/mimetools/PORTING.md).

## 11. 보안

플러그인은 앱과 같은 권한으로 실행되는 네이티브 코드다. 신뢰할 수 있는 출처의 플러그인만 설치한다.
원본의 서명 검사(`verifySignedfile`, Authenticode)는 Windows 전용이라 하지 않는다. 플러그인 목록 배포(플러그인 관리자)를 하게 되면
해시 목록 또는 GPG 서명으로 검증한다 (P7-09, 미정).

## 12. 버전 정책

- `NPP_PLUGIN_API_VERSION` 1. 기존 멤버의 의미가 바뀔 때만 올린다 (그때는 플러그인을 다시 빌드해야 함).
- `NppApi`·`NppNotification`에 멤버를 더할 때는 끝에 붙인다: 새 앱은 옛 플러그인을 그대로 읽는다. 새 멤버를 쓰는 플러그인은 `NPP_API_HAS`로 확인한다.

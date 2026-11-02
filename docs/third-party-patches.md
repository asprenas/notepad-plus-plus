# Vendored 소스 수정 기록

`scintilla/`, `lexilla/`, `boostregex/`, `PowerEditor/src/pugixml` 등 외부 소스를 Qt 포트를 위해 수정한 내역.
업스트림 버전 갱신 시 이 목록을 기준으로 재적용한다. 코드에는 `[npp-qt]` 주석으로 표시한다.

| 파일 | 수정 내용 | 이유 | 날짜 |
|---|---|---|---|
| `scintilla/include/Sci_Position.h` | `#include <stdint.h>` 추가 | Notepad++가 `Sci_PositionCR`을 `long` → `intptr_t`로 바꿨으나 헤더 미포함. Windows에서는 다른 헤더 경유로 우연히 통과, Linux GCC에서 컴파일 오류 | 2026-11-02 |
| `PowerEditor/src/uchardet/nsCharSetProber.cpp` | `#include <cstdint>` 추가 | Notepad++ 수정 코드가 `uintptr_t` 사용, Linux GCC에서 헤더 누락 오류 | 2026-11-02 |
| `lexilla/lexers/LexUser.cxx` | `#include <windows.h>`를 `_WIN32`로 감싸고, 비 Windows용 `_itoa` 대체 함수 추가, `<cstring>` 명시 포함 | UDL 렉서가 Windows 헤더/MSVC CRT(`_itoa`)에 의존. `strlen`/`strcmp`는 `windows.h` 경유로 들어오던 것 | 2026-11-02 |

## 서브모듈

| 경로 | 저장소 | 고정 버전 |
|---|---|---|
| `app/thirdparty/qtads` | https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System (LGPL-2.1) | `v5.1.1` |

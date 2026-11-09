# NPPM_* 메시지 호환 목록 (7단계 P7-01, P7-05)

- 상위 문서: [7단계 세부 계획](../plan/phase-7-plugin-api.md), [플러그인 개발 문서](plugin-development.md)
- 구현: `app/ui/plugins/PluginMessages.cpp` (`NppApi.nppMessage`, 플러그인 쪽은 `npp_compat_send` / `npp_send`)
- 메시지 번호: `app/plugin-api/include/npp_msgs.h` (원본 `Notepad_plus_msgs.h` v8.9.8.1에서 `tools/gen-npp-msgs.py`로 생성)

## 1. 사용 빈도 조사 (P7-01)

인기 플러그인 10개의 소스에서 `NPPM_*` 사용을 집계했다 (`tools/plugin-survey/nppm_usage.py`, 2026-11-05).
각 플러그인에 포함된 `Notepad_plus_msgs.h` 사본과 정의 줄은 제외했다.

| 플러그인 | 저장소 | 커밋 |
|---|---|---|
| Compare | github.com/pnedev/compare-plugin | `6611373` |
| NppExec | github.com/d0vgan/nppexec | `7a5d279` |
| XML Tools | github.com/morbac/xmltools | `771a82c` |
| JSON Viewer | github.com/kapilratnani/JSON-Viewer | `fcd2ae5` |
| PythonScript | github.com/bruderstein/PythonScript | `cabbcc9` |
| DSpellCheck | github.com/Predelnik/DSpellCheck | `a152beb` |
| MIME Tools | github.com/npp-plugins/mimetools | `7a54297` |
| Converter | github.com/npp-plugins/converter | `dbba091` |
| CsvQuery (C#) | github.com/jokedst/CsvQuery | `7e960ea` |
| BetterMultiSelection | github.com/dail8859/BetterMultiSelection | `4c63c6d` |

재실행: 각 저장소를 한 폴더에 받은 뒤 `python tools/plugin-survey/nppm_usage.py <폴더> --markdown`.

결과: 서로 다른 메시지 123개가 쓰인다. **상위 50개 중 44개를 그대로 지원하고, 6개(도킹 `DMM*`, 툴바 아이콘,
메뉴 핸들)는 Win32 객체가 필요해 새 API 함수로 대체**한다. 전체로는 76개 지원, 12개 대체, 35개는 2차.

## 2. 의미 차이 (원본과 다른 점)

- 문자열은 모두 UTF-8 `char*`. 원본의 `wchar_t*` 인자 자리에 UTF-8 버퍼를 넘기고, 크기 인자는 **바이트** 수다.
- 경로를 돌려주는 `RUNCOMMAND_USER` 메시지(`NPPM_GETFULLCURRENTPATH` 등)는 원본처럼 버퍼가 작으면 `FALSE`.
  `NPPM_GETPLUGINSCONFIGDIR`, `NPPM_GETNPPSETTINGSDIRPATH`, `NPPM_GETPLUGINHOMEPATH`, `NPPM_GETFULLPATHFROMBUFFERID`,
  `NPPM_GETLANGUAGENAME/DESC`는 길이를 돌려주고 버퍼 `NULL`이면 길이만 알려 준다 (원본 최신 동작).
- `NPPM_GETOPENFILENAMES`(원본 `_DEPRECATED`, 같은 번호): 각 버퍼는 260바이트(`MAX_PATH`)까지 채운다.
- `NPPM_GETNPPVERSION`: 기준 원본 버전 8.9.8.1 (`HIWORD` 8, `LOWORD` 981). 플러그인의 기능 판별용.
- `NPPM_GETCURRENTNATIVELANGENCODING`, `NPPM_DECODESCI/ENCODESCI`: 편집기는 항상 UTF-8 (65001 / UTF-8 모드).
- `NPPM_MODELESSDIALOG`, `NPPM_DARKMODESUBCLASSANDTHEME`: 받아들이되 할 일이 없다 (Qt가 처리).
- `NPPM_GETPLUGINSCONFIGDIR`: `<설정 폴더>/plugins/config` (없으면 만든다).
- `NPPM_GETPLUGINHOMEPATH`: `<앱 폴더>/plugins`.
- `NPPM_MSGTOPLUGIN`: 대상 이름은 라이브러리 파일 이름(`mimeTools.dll`, `libmimeTools.so`)이며 대소문자,
  `lib` 접두사, 확장자를 무시해 비교한다. 대상은 `npp_plugin_message(NPPM_MSGTOPLUGIN, 0, info)`로 받는다.
- `NPPM_ALLOCATECMDID / MARKER / INDICATOR`: 원본과 같은 범위 (23000–24999, 1–15, 9–20).
- `NPPM_SETBUFFERENCODING`: 저장할 때 쓸 유니코드 모드만 바꾼다 (원본처럼 다시 읽지 않음).
- 지원하지 않는 메시지는 0을 돌려준다.

## 3. Win32 전용 메시지의 대체

| 원본 | 대체 |
|---|---|
| `NPPM_DMMREGASDCKDLG` + `tTbData` | `NppApi.registerPanel(self, id, title, QWidget*)` |
| `NPPM_DMMSHOW` / `NPPM_DMMHIDE` | `NppApi.showPanel(self, id, 1/0)`, `isPanelVisible` |
| `NPPM_ADDTOOLBARICON_FORDARKMODE` (HBITMAP/HICON) | `NppApi.addToolbarIcon(cmdId, 아이콘 파일, 다크 모드 아이콘 파일)` |
| `NPPM_GETMENUHANDLE` (HMENU) | 메뉴는 `npp_plugin_menu_items`, 체크는 `setMenuItemCheck` |
| `NPPM_CREATESCINTILLAHANDLE` | Qt 플러그인이 `ScintillaEditBase`를 직접 만든다 |
| 플러그인 대화상자 (Win32 리소스) | Qt 위젯 또는 `NppApi.messageBox` |
| `HWND` 비교 (`hwndFrom == scintillaMainHandle`) | `npp_compat.h`의 핸들 값은 그대로 비교 가능 (`NppData`) |

## 4. 전체 목록

조사에서 나온 순서(사용한 플러그인 수, 사용 횟수). "지원" = `PluginMessages.cpp`가 처리, `ui.plugins` 테스트가 주요 메시지를 확인.

| 순위 | 메시지 | 플러그인 수 | 사용 횟수 | Qt 포트 |
|---|---|---|---|---|
| 1 | `NPPM_GETCURRENTSCINTILLA` | 9 | 29 | 지원 |
| 2 | `NPPM_GETPLUGINSCONFIGDIR` | 9 | 20 | 지원 |
| 3 | `NPPM_SETMENUITEMCHECK` | 7 | 22 | 지원 |
| 4 | `NPPM_ADDTOOLBARICON_FORDARKMODE` | 6 | 37 | 대체: `addToolbarIcon` (이미지 파일) |
| 5 | `NPPM_DOOPEN` | 6 | 16 | 지원 |
| 6 | `NPPM_ACTIVATEDOC` | 6 | 9 | 지원 |
| 7 | `NPPM_DMMREGASDCKDLG` | 6 | 7 | 대체: `registerPanel` (QWidget) |
| 8 | `NPPM_MENUCOMMAND` | 5 | 32 | 지원 |
| 9 | `NPPM_GETCURRENTBUFFERID` | 5 | 27 | 지원 |
| 10 | `NPPM_GETMENUHANDLE` | 5 | 20 | 대체: `npp_plugin_menu_items` + `setMenuItemCheck` |
| 11 | `NPPM_GETFULLPATHFROMBUFFERID` | 5 | 15 | 지원 |
| 12 | `NPPM_MODELESSDIALOG` | 5 | 15 | 지원 |
| 13 | `NPPM_GETNBOPENFILES` | 5 | 13 | 지원 |
| 14 | `NPPM_SWITCHTOFILE` | 5 | 12 | 지원 |
| 15 | `NPPM_DMMSHOW` | 5 | 8 | 대체: `showPanel` |
| 16 | `NPPM_DMMHIDE` | 5 | 6 | 대체: `showPanel(..., 0)` |
| 17 | `NPPM_MSGTOPLUGIN` | 4 | 50 | 지원 |
| 18 | `NPPM_GETFULLCURRENTPATH` | 4 | 15 | 지원 |
| 19 | `NPPM_GETNPPVERSION` | 4 | 9 | 지원 |
| 20 | `NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR` | 4 | 7 | 지원 |
| 21 | `NPPM_GETFILENAME` | 4 | 7 | 지원 |
| 22 | `NPPM_GETPOSFROMBUFFERID` | 4 | 7 | 지원 |
| 23 | `NPPM_GETBUFFERENCODING` | 4 | 5 | 지원 |
| 24 | `NPPM_GETBUFFERIDFROMPOS` | 4 | 5 | 지원 |
| 25 | `NPPM_DMMUPDATEDISPINFO` | 4 | 4 | 대체: `registerPanel` 제목 (변경은 2차) |
| 26 | `NPPM_SETCURRENTLANGTYPE` | 4 | 4 | 지원 |
| 27 | `NPPM_HIDETABBAR` | 3 | 15 | 지원 |
| 28 | `NPPM_GETCURRENTVIEW` | 3 | 8 | 지원 |
| 29 | `NPPM_GETNPPDIRECTORY` | 3 | 8 | 지원 |
| 30 | `NPPM_GETCURRENTDIRECTORY` | 3 | 6 | 지원 |
| 31 | `NPPM_GETCURRENTDOCINDEX` | 3 | 6 | 지원 |
| 32 | `NPPM_RELOADFILE` | 3 | 6 | 지원 |
| 33 | `NPPM_ALLOCATECMDID` | 3 | 5 | 지원 |
| 34 | `NPPM_DARKMODESUBCLASSANDTHEME` | 3 | 5 | 지원 |
| 35 | `NPPM_GETEDITORDEFAULTFOREGROUNDCOLOR` | 3 | 5 | 지원 |
| 36 | `NPPM_GETPLUGINHOMEPATH` | 3 | 5 | 지원 |
| 37 | `NPPM_GETSHORTCUTBYCMDID` | 3 | 5 | 지원 |
| 38 | `NPPM_GETCURRENTLANGTYPE` | 3 | 4 | 지원 |
| 39 | `NPPM_SAVECURRENTFILEAS` | 3 | 4 | 지원 |
| 40 | `NPPM_SETBUFFERLANGTYPE` | 3 | 4 | 지원 |
| 41 | `NPPM_SETSTATUSBAR` | 3 | 4 | 지원 |
| 42 | `NPPM_GETBUFFERLANGTYPE` | 3 | 3 | 지원 |
| 43 | `NPPM_GETCURRENTLINE` | 3 | 3 | 지원 |
| 44 | `NPPM_ISTABBARHIDDEN` | 3 | 3 | 지원 |
| 45 | `NPPM_MAKECURRENTBUFFERDIRTY` | 3 | 3 | 지원 |
| 46 | `NPPM_SAVEALLFILES` | 3 | 3 | 지원 |
| 47 | `NPPM_SAVECURRENTFILE` | 3 | 3 | 지원 |
| 48 | `NPPM_SETBUFFERENCODING` | 3 | 3 | 지원 |
| 49 | `NPPM_GETLANGUAGEDESC` | 2 | 4 | 지원 |
| 50 | `NPPM_GETLANGUAGENAME` | 2 | 4 | 지원 |
| 51 | `NPPM_GETNATIVELANGFILENAME` | 2 | 4 | 지원 |
| 52 | `NPPM_ISDARKMODEENABLED` | 2 | 4 | 지원 |
| 53 | `NPPM_GETNPPSETTINGSDIRPATH` | 2 | 3 | 지원 |
| 54 | `NPPM_GETSETTINGSONCLOUDPATH` | 2 | 3 | 지원 |
| 55 | `NPPM_SETLINENUMBERWIDTHMODE` | 2 | 3 | 지원 |
| 56 | `NPPM_ADDSCNMODIFIEDFLAGS` | 2 | 2 | 지원 |
| 57 | `NPPM_ADDTOOLBARICON_DEPRECATED` | 2 | 2 | 대체: `addToolbarIcon` |
| 58 | `NPPM_ALLOCATEINDICATOR` | 2 | 2 | 지원 |
| 59 | `NPPM_ALLOCATEMARKER` | 2 | 2 | 지원 |
| 60 | `NPPM_DECODESCI` | 2 | 2 | 지원 |
| 61 | `NPPM_ENCODESCI` | 2 | 2 | 지원 |
| 62 | `NPPM_GETAPPDATAPLUGINSALLOWED` | 2 | 2 | 지원 |
| 63 | `NPPM_GETBOOKMARKID` | 2 | 2 | 지원 |
| 64 | `NPPM_GETBUFFERFORMAT` | 2 | 2 | 지원 |
| 65 | `NPPM_GETCURRENTCOLUMN` | 2 | 2 | 지원 |
| 66 | `NPPM_GETCURRENTNATIVELANGENCODING` | 2 | 2 | 지원 |
| 67 | `NPPM_GETLINENUMBERWIDTHMODE` | 2 | 2 | 지원 |
| 68 | `NPPM_GETOPENFILENAMES` | 2 | 2 | 지원 |
| 69 | `NPPM_GETWINDOWSVERSION` | 2 | 2 | 지원 |
| 70 | `NPPM_HIDEMENU` | 2 | 2 | 지원 |
| 71 | `NPPM_HIDESTATUSBAR` | 2 | 2 | 지원 |
| 72 | `NPPM_HIDETOOLBAR` | 2 | 2 | 지원 |
| 73 | `NPPM_ISMENUHIDDEN` | 2 | 2 | 지원 |
| 74 | `NPPM_ISSTATUSBARHIDDEN` | 2 | 2 | 지원 |
| 75 | `NPPM_ISTOOLBARHIDDEN` | 2 | 2 | 지원 |
| 76 | `NPPM_LAUNCHFINDINFILESDLG` | 2 | 2 | 미지원 (2차) |
| 77 | `NPPM_LOADSESSION` | 2 | 2 | 미지원 (2차) |
| 78 | `NPPM_RELOADBUFFERID` | 2 | 2 | 지원 |
| 79 | `NPPM_SAVEFILE` | 2 | 2 | 미지원 (2차) |
| 80 | `NPPM_SETBUFFERFORMAT` | 2 | 2 | 지원 |
| 81 | `NPPM_SETEDITORBORDEREDGE` | 2 | 2 | 미지원 (2차) |
| 82 | `NPPM_SETSMOOTHFONT` | 2 | 2 | 미지원 (2차) |
| 83 | `NPPM_TRIGGERTABBARCONTEXTMENU` | 2 | 2 | 미지원 (2차) |
| 84 | `NPPM_ADDREBAR` | 1 | 5 | 미지원 (2차) |
| 85 | `NPPM_GETFILENAMEATCURSOR` | 1 | 3 | 지원 |
| 86 | `NPPM_ADDTOOLBARICON` | 1 | 2 | 미지원 (2차) |
| 87 | `NPPM_ALLOCATESUPPORTED_DEPRECATED` | 1 | 2 | 미지원 (2차) |
| 88 | `NPPM_CREATESCINTILLAHANDLE` | 1 | 2 | 대체: Qt 플러그인이 `ScintillaEditBase`를 직접 생성 |
| 89 | `NPPM_DESTROYSCINTILLAHANDLE_DEPRECATED` | 1 | 2 | 미지원 (2차) |
| 90 | `NPPM_GETCURRENTWORD` | 1 | 2 | 지원 |
| 91 | `NPPM_GETEXTPART` | 1 | 2 | 지원 |
| 92 | `NPPM_GETNAMEPART` | 1 | 2 | 지원 |
| 93 | `NPPM_ISDOCSWITCHERSHOWN` | 1 | 2 | 미지원 (2차) |
| 94 | `NPPM_ALLOCATESUPPORTED` | 1 | 1 | 미지원 (2차) |
| 95 | `NPPM_DISABLEAUTOUPDATE` | 1 | 1 | 미지원 (2차) |
| 96 | `NPPM_DMMGETPLUGINHWNDBYNAME` | 1 | 1 | 대체: 플러그인 간 통신은 `NPPM_MSGTOPLUGIN` |
| 97 | `NPPM_DOCLISTDISABLEEXTCOLUMN` | 1 | 1 | 미지원 (2차) |
| 98 | `NPPM_DOCLISTDISABLEPATHCOLUMN` | 1 | 1 | 미지원 (2차) |
| 99 | `NPPM_DOCSWITCHERDISABLECOLUMN` | 1 | 1 | 미지원 (2차) |
| 100 | `NPPM_GETCURRENTCMDLINE` | 1 | 1 | 미지원 (2차) |
| 101 | `NPPM_GETCURRENTLINESTR` | 1 | 1 | 지원 |
| 102 | `NPPM_GETDARKMODECOLORS` | 1 | 1 | 대체: `isDarkMode` + `QPalette` |
| 103 | `NPPM_GETENABLETHEMETEXTUREFUNC` | 1 | 1 | 대체: 해당 없음 (Win32 테마) |
| 104 | `NPPM_GETENABLETHEMETEXTUREFUNC_DEPRECATED` | 1 | 1 | 대체: 해당 없음 (Win32 테마) |
| 105 | `NPPM_GETEXTERNALLEXERAUTOINDENTMODE` | 1 | 1 | 미지원 (2차) |
| 106 | `NPPM_GETNBSESSIONFILES` | 1 | 1 | 미지원 (2차) |
| 107 | `NPPM_GETNBUSERLANG` | 1 | 1 | 미지원 (2차) |
| 108 | `NPPM_GETNPPFULLFILEPATH` | 1 | 1 | 지원 |
| 109 | `NPPM_GETOPENFILENAMESPRIMARY` | 1 | 1 | 미지원 (2차) |
| 110 | `NPPM_GETOPENFILENAMESPRIMARY_DEPRECATED` | 1 | 1 | 미지원 (2차) |
| 111 | `NPPM_GETOPENFILENAMESSECOND` | 1 | 1 | 미지원 (2차) |
| 112 | `NPPM_GETOPENFILENAMESSECOND_DEPRECATED` | 1 | 1 | 미지원 (2차) |
| 113 | `NPPM_GETSESSIONFILES` | 1 | 1 | 미지원 (2차) |
| 114 | `NPPM_GETTABCOLORID` | 1 | 1 | 미지원 (2차) |
| 115 | `NPPM_GETTOOLBARICONSETCHOICE` | 1 | 1 | 미지원 (2차) |
| 116 | `NPPM_ISAUTOINDENTON` | 1 | 1 | 미지원 (2차) |
| 117 | `NPPM_ISDOCLISTSHOWN` | 1 | 1 | 미지원 (2차) |
| 118 | `NPPM_REMOVESHORTCUTBYCMDID` | 1 | 1 | 미지원 (2차) |
| 119 | `NPPM_SAVECURRENTSESSION` | 1 | 1 | 미지원 (2차) |
| 120 | `NPPM_SAVESESSION` | 1 | 1 | 미지원 (2차) |
| 121 | `NPPM_SETEXTERNALLEXERAUTOINDENTMODE` | 1 | 1 | 미지원 (2차) |
| 122 | `NPPM_SETUNTITLEDNAME` | 1 | 1 | 미지원 (2차) |
| 123 | `NPPM_SHOWDOCLIST` | 1 | 1 | 미지원 (2차) |


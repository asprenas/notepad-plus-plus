# 성능 측정 (6단계)

- 관련: [6단계 세부 계획](../plan/phase-6-packaging-stabilization.md) 3.5절
- 측정일: 2026-11-05, 버전 0.1.0 (커밋은 [CHANGELOG](../CHANGELOG.md) 참고)
- 비교 대상: 원본 Notepad++ 8.9.8.1 (`PowerEditor\bin64\notepad++.exe`, 이 저장소에서 MSBuild로 빌드한 x64 Release)
- 기기: Windows 11 Pro 23H2, 개발 PC (폰트 564개 설치). 같은 기기, 같은 입력.
- Qt 포트: 배포본과 같은 MSVC Release 빌드 (`build\package\stage-windows`), 일부 항목은 MinGW Release 빌드(표에 표기)

## 1. 결과 요약

| 항목 | 목표 | 원본 | Qt 포트 | 판정 |
|---|---|---|---|---|
| 시작 (세션 탭 20개) | 원본의 1.5배 이내 | 340–561 ms | 592–1006 ms → 개선 후 **592 ms** | 1.1–1.7배. 차이 원인 2절 |
| 시작 (빈 문서) | — | 387 ms | **449 ms** | 1.16배 |
| 메모리 (빈 문서, private) | 원본의 2배 이내 | 62.8 MB | **30.2 MB** (working set 66.3 MB) | 충족 |
| 500MB 파일 열기 | 원본과 같은 방식, 스크롤·검색 가능 | 800 ms, 606 MB | **1112 ms, 573 MB** | 충족 (같은 제한 적용, 아래) |
| 500MB 끝으로 스크롤 / 처음으로 | 지연 없음 | — | 15 ms / 11 ms (MinGW) | 충족 |
| 500MB 끝의 단어 검색 | 가능 | — | 841 ms (MinGW) | 충족 |
| 탭 200개 전환 | 체감 지연 없음 | — | 평균 21 ms, 최대 28 ms (MinGW, 화면 갱신 포함) | 충족 |
| 탭 200개 열기 | — | — | 4.5 초 (파일당 22 ms, MinGW) | — |
| 파일에서 찾기 1만 파일 | 원본과 같은 수준 | 777–902 ms (중앙값 792) | **1.20–1.22 초** (MinGW) | 1.5배. 차이 원인 2절 |

시간은 7회(파일에서 찾기 원본 5회, 포트 3회) 중앙값. 원본 시작 시간은 측정 회차마다 300–650 ms 로
흔들렸다(같은 스크립트 세 차례 실행 중앙값 340 / 535 / 561 ms). 표의 "개선 후" 수치는 마지막 회차.

### 6단계에서 고친 것 (측정으로 찾음)

| 문제 | 원인 | 수정 | 효과 |
|---|---|---|---|
| 500MB 파일을 열면 끝없이 CPU 사용 | 대용량 파일 제한 미적용: 구문 강조, 스마트 강조, 괄호 강조가 전체 문서에 동작 | 원본 Buffer.cpp 와 같게: 스타일 없는 Scintilla 문서(`SC_DOCUMENTOPTION_STYLES_NONE`), 일반 텍스트, 줄 바꿈 끔, 괄호/스마트 강조 끔(설정 허용 시 제외), 스냅숏 백업 안 함 | 120 초 이상 → 1.1 초, 메모리 1077 → 573 MB |
| MSVC 빌드에서 큰 파일 읽기 느림 | `istreambuf_iterator` 바이트 단위 복사 | 크기만큼 한 번에 읽기 | 500MB: 4.0 → 1.5 초 |
| 탭 200개에서 탭 전환 362 ms | 문서 목록 패널이 행마다 `style()->standardIcon()` 생성 (숨겨져 있어도) | 아이콘 3개를 한 번만 만들어 재사용 | 362 → 21 ms, 200개 열기 75 → 4.5 초 |
| 세션 복원 500 ms (탭 20개) | 파일마다 제목/상태 표시줄/문서 목록 갱신 | 복원이 끝난 뒤 한 번만 갱신 | 500 → 130 ms |
| 시작 시 테마 적용 180 ms | 라이트 모드에서도 스타일을 새로 만들어 모든 위젯을 다시 꾸밈 | 스타일·팔레트가 바뀔 때만 적용 | 180 → 16 ms |
| 시작 시 문자 패널 120 ms 이상 | 숨겨진 ASCII 패널의 256행을 시작 때 채움 | 처음 보일 때(또는 읽힐 때) 채움 | 시작~첫 화면 650 → 250 ms (MinGW) |
| 파일에서 찾기 목록 작성 450 ms | 항목마다 숨김 속성을 다시 조회(`QFileInfo::isHidden`) | `QDirIterator` 가 디렉터리 읽기에서 얻은 속성 사용 | 450 → 16 ms |
| 파일에서 찾기 파일 읽기 | 검색용 로드에도 언어 감지·시각·쓰기 권한 확인 | 검색 전용 로드(`LoadOptions::forSearch`) | 전체 2.0 → 1.2 초 |

## 2. 목표와 차이가 남은 항목의 원인

### 시작 시간 (세션 탭 20개): 원본의 약 1.1–1.7배

`NPP_QT_STARTUP_LOG=1` 로그 (MSVC Release, 탭 20개, 개선 후) 기준으로 남은 비용:

- **프로세스 시작~`main()`**: Qt DLL(Core, Gui, Widgets, Network, PrintSupport, Core5Compat)과
  플러그인(`qwindows`, `qmodernwindowsstyle`, 이미지 형식) 로드. 외부 측정의 "ready" 와 내부 로그의
  첫 화면 시각 차이가 약 250–300 ms 이며 대부분 이 단계다. 원본은 Win32 + Scintilla 정적 링크라 이 비용이 없다.
- **DirectWrite 글꼴 데이터베이스**: 첫 글꼴 사용 때 Qt 가 시스템 글꼴(이 PC 564개)을 열거한다.
  `QT_QPA_PLATFORM=windows:fontengine=gdi` 로 바꾸면 약 130 ms 줄지만, 이모지·합자·고해상도 렌더링을
  위해 DirectWrite 를 유지한다.
- 탭 20개 문서 로드 자체(약 130 ms)는 원본과 같은 수준이다.

추가 개선 후보: 패널 생성을 처음 보일 때로 미루기(원본처럼), 사용하지 않는 Qt 모듈(Network: 업데이트
확인용) 지연 로드, 정적 Qt 빌드. 7단계 이후 필요하면 진행한다.

### 파일에서 찾기 1만 파일: 원본의 약 1.5배

- 포트는 검색 중에도 화면이 멈추지 않도록 25 ms 단위로 이벤트 루프를 돌린다(91회, 가장 긴 멈춤 39 ms).
  원본은 검색 중 진행 창만 갱신한다.
- 파일마다 Scintilla 문서를 만들고(원본과 같음) UTF-8 변환·인코딩 감지를 한다. 남은 차이는 파일당
  약 40 µs 이며 대부분 문서 생성·해제다.
- 실사용 규모(수백~수천 파일)에서는 1초 안에 끝나므로 베타에서는 허용한다.

## 3. 측정 방법 (재현)

```powershell
# 배포본 빌드 (MSVC Release, build\package\stage-windows)
powershell -ExecutionPolicy Bypass -File ci\package-windows.ps1
# 시작 (탭 20개 / 빈 문서), 500MB 파일, 메모리: 원본과 포트를 번갈아 실행
powershell -ExecutionPolicy Bypass -File tools\perf\measure-windows.ps1 -Runs 7
# 원본 파일에서 찾기 1만 파일 (대화상자를 창 메시지로 조작)
powershell -ExecutionPolicy Bypass -File tools\perf\fif-npp.ps1 -Runs 5
```

```bash
# 포트: 탭 200개, 500MB 스크롤/검색 (test_ui_perf), 파일에서 찾기 1만 파일 (ui.find.responsiveness 와 같은 시험)
QT_QPA_PLATFORM=windows NPP_PERF_LARGE_FILE=build/perf/large-500mb.txt build/<dir>/bin/test_ui_perf
QT_QPA_PLATFORM=windows build/<dir>/bin/test_ui_find findInFilesLargeFolderIsResponsiveAndCancellable
# 시작 단계별 시간
NPP_QT_STARTUP_LOG=1 notepadpp-qt -multiInst -settingsDir=<dir>
```

- "ready": 주 창이 생기고 프로세스가 500 ms 동안 CPU 를 쓰지 않은 시점(마지막 CPU 사용 시각).
  두 프로그램 모두 `-multiInst -settingsDir=<임시 폴더>` 로 실행해 설치본 설정을 건드리지 않는다.
- 세션 탭 20개: `PowerEditor\src` 의 가장 큰 `.cpp/.h` 20개.
- 500MB 파일: 110 바이트 줄의 ASCII 텍스트(대용량 파일 기준 200MB 이상이므로 두 프로그램 모두 제한 적용).
- 결과 파일: `build\perf\results-windows.csv`.

## 4. Linux

원본이 없으므로 비교하지 않는다. AppImage 시작·파일 열기는 [6단계 문서](../plan/phase-6-packaging-stabilization.md)
8절의 배포판 컨테이너 시험에서 확인한다. 같은 성능 수정(대용량 파일 제한, 문서 목록, 파일에서 찾기)은
플랫폼 공통 코드다.

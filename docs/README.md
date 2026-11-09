# Notepad++ Qt 문서

Qt 포트 문서 목차다. 기존 Win32 빌드 안내는 저장소 루트의 `BUILD.md`를 본다.

## 폴더 구조

```text
docs/
├── README.md       이 목차
├── CHANGELOG.md    버전별 변경 기록
├── build/          빌드, 빌드 환경, 릴리스
├── plan/           포팅 계획서와 단계별 세부 계획
├── plugins/        플러그인 개발과 API 호환 목록
└── reference/      점검표, 측정 결과, 기록 자료
```

## 처음 볼 문서

| 하려는 일 | 문서 |
|---|---|
| 소스를 빌드한다 | [빌드 가이드](build/build.md) |
| OS별 컴파일러와 릴리스 파일을 확인한다 | [OS별 빌드 환경과 릴리스 파일](build/build-environments.md) |
| 릴리스를 만든다 | [릴리스 절차](build/release.md) |
| 프로젝트 전체 계획을 본다 | [포팅 개발 계획서](plan/qt-cross-platform-port-plan.md) |
| 플러그인을 만든다 | [플러그인 개발 문서](plugins/plugin-development.md) |
| 버전별 변경 사항을 본다 | [변경 기록](CHANGELOG.md) |

## 빌드와 릴리스 (`build/`)

| 문서 | 내용 |
|---|---|
| [build.md](build/build.md) | Windows, Linux 빌드 명령, 로컬 CI, CLion 설정, 패키징 명령 |
| [build-environments.md](build/build-environments.md) | OS별 컴파일러, Qt, 빌드 환경, 릴리스 파일 |
| [release.md](build/release.md) | 버전 관리와 릴리스 절차 |

## 계획 (`plan/`)

| 문서 | 내용 |
|---|---|
| [qt-cross-platform-port-plan.md](plan/qt-cross-platform-port-plan.md) | 포팅 전체 계획서 |
| [phase-0-preparation.md](plan/phase-0-preparation.md) | 0단계: 준비, 스파이크 |
| [phase-1-core-extraction.md](plan/phase-1-core-extraction.md) | 1단계: 코어 추출 (`npp-core`) |
| [phase-2-mvp.md](plan/phase-2-mvp.md) | 2단계: 최소 실행 앱 |
| [phase-3-search-edit.md](plan/phase-3-search-edit.md) | 3단계: 검색, 편집 기능 |
| [phase-4-settings-theme.md](plan/phase-4-settings-theme.md) | 4단계: 설정, 테마, 다국어 |
| [phase-5-panels-extras.md](plan/phase-5-panels-extras.md) | 5단계: 패널, 부가 기능 |
| [phase-6-packaging-stabilization.md](plan/phase-6-packaging-stabilization.md) | 6단계: 패키징, 안정화 |
| [phase-7-plugin-api.md](plan/phase-7-plugin-api.md) | 7단계: 플러그인 API |
| [phase-macos.md](plan/phase-macos.md) | macOS 지원 계획 |

## 플러그인 (`plugins/`)

| 문서 | 내용 |
|---|---|
| [plugin-development.md](plugins/plugin-development.md) | 플러그인 API v1 개발 문서 |
| [plugin-api-nppm.md](plugins/plugin-api-nppm.md) | `NPPM_*` 메시지 호환 목록 |

## 참고 자료 (`reference/`)

| 문서 | 내용 |
|---|---|
| [feature-checklist.md](reference/feature-checklist.md) | 메뉴 기준 기능 체크리스트 (`tools/gen-feature-checklist.py`로 생성) |
| [performance.md](reference/performance.md) | 성능 측정 결과 |
| [core-win32-api-inventory.md](reference/core-win32-api-inventory.md) | 코어 추출용 Win32 API 목록 (`tools/win32-api-inventory.py`로 생성) |
| [third-party-patches.md](reference/third-party-patches.md) | vendored 소스 수정 기록 |

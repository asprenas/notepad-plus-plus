# 릴리스 절차 (Qt 포트)

- 관련: [6단계 세부 계획](../plan/phase-6-packaging-stabilization.md) 3.6절, [변경 기록](../CHANGELOG.md), [빌드](build.md)
- 릴리스는 GitHub Actions 또는 개발 PC(Windows)에서 만든다. 절차는 GitHub Actions(3.1절) 또는 로컬(3.2절) 중 하나를 고른다.

## 1. 버전

- `0.x.y`: 베타 전은 `0.`. 기능 추가 `x`, 수정만 `y`.
- 버전은 한 곳: 루트 `CMakeLists.txt` 의 `project(... VERSION x.y.z)`. 실행 파일 버전 정보, 설치본,
  AppImage, 파일 이름이 모두 여기서 온다.
- 원본 기준 버전(예: Notepad++ 8.9.8.1)은 CHANGELOG 의 각 버전 항목에 적는다.

## 2. 준비물 (Windows 호스트)

| 도구 | 용도 |
|---|---|
| Qt 6.12 MinGW + MSVC 키트, Visual Studio C++ | 로컬 CI, Windows 배포본 (MSVC) |
| Docker Desktop | Linux CI, AppImage 빌드(`ci/docker/appimage.Dockerfile`), 배포판 시험 |
| GitHub CLI `gh` (로그인) | 릴리스 업로드 |

## 3. 절차

### 3.1 GitHub Actions

1. `docs/CHANGELOG.md` 에 `## [x.y.z] - 날짜` 항목 작성, `CMakeLists.txt` 버전 수정, 커밋 후 `master` 에 push.
2. GitHub Actions 의 `Release` workflow 를 `master` 에서 수동 실행하고 버전을 입력한다.

   ```bash
   gh workflow run release.yml --ref master -f version=x.y.z
   ```

3. 버전 검사 → Windows MSVC, Linux 빌드·시험 → 패키지·패키지 시험 → draft 릴리스 순서로 진행된다. MinGW 는 실행하지 않는다.
4. draft 의 파일과 노트를 확인하고 게시한다. 태그 `vx.y.z` 는 게시할 때 생성된다.

### 3.2 로컬

1. `docs/CHANGELOG.md` 에 `## [x.y.z] - 날짜` 항목 작성, `CMakeLists.txt` 버전 수정, 커밋.
2. 시험 실행(태그·업로드 없음):

   ```bash
   bash ci/release.sh
   ```

   깨끗한 작업 트리 → 로컬 CI 전체(`ci/local-ci.sh`) → 패키지(`ci/package-windows.ps1`,
   `ci/package-linux.sh`) → 패키지 시험(`ci/test-package-windows.ps1`, `ci/test-package-linux.sh`)
   → `dist/SHA256SUMS.txt`, `dist/release-notes.md`.
3. 결과 확인 후 게시:

   ```bash
   bash ci/release.sh --publish
   ```

   위 과정을 다시 수행한 뒤 `git tag -a vx.y.z`, 브랜치와 태그 push, `gh release create`
   (0.x 는 pre-release) 로 `dist/` 의 파일을 올린다.

## 4. 결과물 (`dist/`)

| 파일 | 내용 |
|---|---|
| `notepadpp-qt-<v>-windows-x64-portable.zip` | 포터블 (`doLocalConf.xml`: 설정을 프로그램 폴더에 저장) |
| `notepadpp-qt-<v>-windows-x64-symbols.zip` | `notepadpp-qt.pdb` (크래시 덤프 분석용, 사용자 배포 불필요) |
| `notepadpp-qt-<v>-linux-x86_64.AppImage` | Linux (glibc 2.35+, X11·Wayland) |
| `notepadpp-qt-<v>-linux-x86_64.tar.gz` | Linux 포터블 AppDir (`AppDir/AppRun`) |
| `SHA256SUMS.txt` | 체크섬 |

Windows 설치본과 Linux deb, rpm, Flatpak 생성은 차후 버전을 위해 비활성화되어 있다.

## 5. 크래시 덤프 분석

사용자가 보낸 `<설정 폴더>/crash/npp-*.dmp` 는 같은 버전의 `symbols.zip` 의 PDB 와 함께 Visual Studio
또는 WinDbg 로 연다. Linux 는 `npp-*.log` 에 백트레이스가 있다(AppImage 는 심볼이 없어 함수 이름 일부만 보임).

## 6. 서명

현재 미서명(개인 사용 단계). 외부 배포 전에 Windows 코드 서명 인증서를 준비하고
`ci/package-windows.ps1` 에서 `signtool` 로 exe·설치본을 서명한다.

# 버전 업데이트와 자동 빌드

현재 버전은 `VERSION`에 기록합니다. `v0.1.0`처럼 `v`로 시작하는 태그를 push하면
GitHub Actions가 Windows 패치 EXE를 빌드하고 해당 버전의 Release에 ZIP을 첨부합니다.
저장소가 private이므로 릴리즈도 저장소 접근 권한이 있는 사용자만 볼 수 있습니다.

수정한 코드를 먼저 커밋한 뒤 프로젝트 폴더에서 실행합니다.

```powershell
# 0.1.0 → 0.1.1
.\scripts\release.ps1
# 0.1.1 → 0.2.0
.\scripts\release.ps1 -Bump minor
# 0.2.0 → 1.0.0
.\scripts\release.ps1 -Bump major
```

스크립트가 VERSION 수정, 버전 커밋, 태그 생성, main·태그 동시 push를 처리합니다.
작업 폴더에 커밋하지 않은 변경이 있으면 중단합니다.
빌드만 확인하려면 GitHub Actions에서 `한글 패치 릴리즈 빌드`를 수동 실행합니다.
수동 실행은 artifact만 만들고 Release를 게시하지 않습니다.

번역 JSON 수정 시에는 README의 기준 해시 갱신 절차를 먼저 수행하고
갱신된 manifest.json까지 커밋한 뒤 버전을 올리세요.

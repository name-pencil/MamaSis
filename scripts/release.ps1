# 변경 사항을 먼저 커밋한 뒤 실행하면 버전 커밋과 태그를 함께 업로드한다.
param(
	[ValidateSet('patch', 'minor', 'major')]
	[string] $Bump = 'patch'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $root
function Invoke-Git {
	& git @args
	if ($LASTEXITCODE -ne 0) { throw "Git 명령 실패: $args" }
}
if ((Invoke-Git branch --show-current) -ne 'main') { throw 'main 브랜치에서 실행해 주세요.' }
if (Invoke-Git status --porcelain) { throw '먼저 변경 사항을 커밋해 주세요.' }
$versionPath = Join-Path $root 'VERSION'
$current = (Get-Content -LiteralPath $versionPath -Raw).Trim()
if ($current -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION 형식은 숫자.숫자.숫자입니다.' }
$parts = $current.Split('.') | ForEach-Object { [int] $_ }
switch ($Bump) {
	'major' { $parts[0]++; $parts[1] = 0; $parts[2] = 0 }
	'minor' { $parts[1]++; $parts[2] = 0 }
	'patch' { $parts[2]++ }
}
$next = $parts -join '.'
$tag = "v$next"
Invoke-Git fetch origin --tags
if (Invoke-Git tag --list $tag) { throw "이미 존재하는 태그입니다: $tag" }
[IO.File]::WriteAllText($versionPath, "$next`n", [Text.UTF8Encoding]::new($false))
Invoke-Git add -- VERSION
Invoke-Git commit -m "Release $tag"
Invoke-Git tag -a $tag -m "Release $tag"
# 원격 브랜치와 태그 중 하나만 올라가는 상황을 방지한다.
Invoke-Git push --atomic origin main $tag
Write-Host "$tag 업로드 완료. GitHub Actions에서 빌드 후 Release를 생성합니다."

# 로컬 Visual Studio와 GitHub Windows runner에서 같은 빌드 명령을 사용한다.
param(
	[Parameter(Mandatory)]
	[ValidateSet('font_hook', 'launcher')]
	[string] $Target
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
	throw 'Visual Studio Build Tools의 C++ 데스크톱 개발 구성요소를 설치해 주세요.'
}
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x86/x64 빌드 도구를 찾지 못했습니다.' }
$dev = Join-Path $vs 'Common7\Tools\VsDevCmd.bat'
$runtime = Join-Path $root 'runtime'
$build = Join-Path $root 'build\native'
New-Item -ItemType Directory -Path $runtime, $build -Force | Out-Null
$source = Join-Path $PSScriptRoot "$Target.cpp"
$object = Join-Path $build "$Target.obj"

# /MT는 C++ 실행 라이브러리를 포함한다. 사용자에게 별도 재배포 패키지를 요구하지 않는다.
# 게임이 32비트이므로 빌드 PC가 64비트여도 -arch=x86을 사용한다.
if ($Target -eq 'font_hook') {
	$output = Join-Path $runtime 'mama_sis_font.dll'
	$library = Join-Path $build 'mama_sis_font.lib'
	$options = '/LD'
	$link = '/IMPLIB:"{0}" gdi32.lib user32.lib' -f $library
} else {
	$output = Join-Path $runtime 'MamaSisKoreanLauncher.exe'
	$options = ''
	$link = '/SUBSYSTEM:WINDOWS user32.lib'
}
$command = '"{0}" -arch=x86 -host_arch=x64 && cl /nologo /utf-8 /std:c++17 /O2 /MT /EHsc {1} /Fo"{2}" "{3}" /link /OUT:"{4}" {5}' -f $dev, $options, $object, $source, $output, $link
cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "컴파일에 실패했습니다. 종료 코드: $LASTEXITCODE" }
Get-Item -LiteralPath $output

# 게임 실행 전에 한글 DLL을 로드하는 32비트 런처를 컴파일한다.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build_native.ps1') -Target launcher

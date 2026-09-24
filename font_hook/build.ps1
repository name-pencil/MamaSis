# 한글 출력 DLL을 32비트로 컴파일한다.
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'build_native.ps1') -Target font_hook

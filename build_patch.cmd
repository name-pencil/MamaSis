@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
rem 개발용 Python 실행 진입점입니다. 릴리즈 사용자는 MamaSisPatch.exe를 실행합니다.
where py >nul 2>nul
if not errorlevel 1 (
	py -3 "%~dp0build_patch.py" %*
) else (
	python "%~dp0build_patch.py" %*
)
set "PATCH_EXIT=%ERRORLEVEL%"
echo.
if not "%PATCH_EXIT%"=="0" echo 패치에 실패했습니다. 위 오류를 확인해 주세요.
pause
exit /b %PATCH_EXIT%

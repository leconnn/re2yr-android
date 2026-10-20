@echo off
rem Configures, builds and tests the Windows desktop version.
rem Usage: scripts\build-desktop.bat [Debug|RelWithDebInfo|Release]
rem
rem Starts from a short PATH (a very long PATH breaks vcvars64.bat), finds the
rem newest Visual Studio C++ tools with vswhere, and uses CMake and Ninja from
rem PATH or from the Android SDK.

setlocal
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"
set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build\desktop-%CONFIG%"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
	for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
rem Fallback when vswhere can't see the installation: search the standard folders.
if not defined VSDIR (
	for /d %%v in ("%ProgramFiles%\Microsoft Visual Studio\*" "%ProgramFiles(x86)%\Microsoft Visual Studio\*") do (
		for /d %%e in ("%%~v\*") do (
			if exist "%%~e\VC\Auxiliary\Build\vcvars64.bat" set "VSDIR=%%~e"
		)
	)
)
if not defined VSDIR (
	echo No Visual Studio installation with C++ tools found.
	exit /b 1
)

set "CMAKE_DIR="
where cmake >nul 2>nul || (
	for /d %%d in ("%LOCALAPPDATA%\Android\Sdk\cmake\*") do set "CMAKE_DIR=%%d\bin"
)

set "PATH=%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\WindowsPowerShell\v1.0;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if defined CMAKE_DIR set "PATH=%CMAKE_DIR%;%PATH%"

cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG% || exit /b 1
cmake --build "%BUILD%" || exit /b 1
ctest --test-dir "%BUILD%" --output-on-failure || exit /b 1
echo.
echo Built: %BUILD%\engine\re2yr.exe

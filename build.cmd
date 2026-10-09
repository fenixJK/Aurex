@echo off
setlocal
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" (
  echo Install Visual Studio 2022 with Desktop development with C++ first.
  exit /b 1
)
for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs=%%i"
if not defined vs (
  echo Visual Studio 2022 C++ tools were not found.
  exit /b 1
)
call "%vs%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
set "PATH=%vs%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%vs%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
cmake %* -P "%~dp0packaging\build.cmake"
exit /b %errorlevel%

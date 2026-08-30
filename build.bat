@echo off
REM The-IgNition build script
REM   build.bat        - compile the library only (syntax check)
REM   build.bat test   - build and run test.c++
REM
REM NOTE: this file is ASCII on purpose. cmd.exe reads .bat in the OEM
REM codepage (949 here), so UTF-8 Korean text in a .bat breaks parsing.
REM
REM /TP    : MSVC does not treat the .c++ extension as C++ source
REM /utf-8 : sources are UTF-8 without BOM; otherwise Korean comments mangle

setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [!] vcvars64.bat not found:
    echo     %VCVARS%
    echo     Edit VCVARS in this file if Visual Studio is installed elsewhere.
    exit /b 1
)
call "%VCVARS%" >nul 2>&1

cd /d "%~dp0"
if not exist build mkdir build

set SRC=location.c++ delivery.c++ dispatch.c++ datetime.c++ photo.c++ intake.c++ day.c++ calendar.c++ storage.c++ recommend.c++ food.c++ foodcsv.c++ domains.c++ user.c++ format.c++
set FLAGS=/TP /utf-8 /EHsc /W3 /std:c++14 /nologo

if /i "%~1"=="test" (
    if not exist test.c++ (
        echo [!] test.c++ not found.
        exit /b 1
    )
    cl %FLAGS% /Fo:build\ /Fe:build\test.exe test.c++ %SRC%
    if errorlevel 1 exit /b 1
    echo.
    build\test.exe
    exit /b %errorlevel%
)

cl /c %FLAGS% /Fo:build\ %SRC%
if errorlevel 1 exit /b 1
echo.
echo [OK] compiled

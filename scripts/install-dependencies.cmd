@echo off
setlocal EnableExtensions
set "VCPKG_DISABLE_METRICS=1"

pushd "%~dp0.." || exit /b 1
set "ROOT=%CD%"
set "VCPKG_ROOT=%ROOT%\.vcpkg"

where git >nul 2>nul
if errorlevel 1 (
    echo [dependencies] Git was not found. Install Git and make sure it is available in PATH.
    goto fail
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    if not exist "%VCPKG_ROOT%\bootstrap-vcpkg.bat" (
        echo [dependencies] Cloning the official vcpkg repository...
        git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
        if errorlevel 1 goto fail
    )

    echo [dependencies] Bootstrapping vcpkg...
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if errorlevel 1 goto fail
)

echo [dependencies] Installing FreeType and LuaJIT for x86-windows-static-md...
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x86-windows-static-md
if errorlevel 1 goto fail

if not exist "%ROOT%\vcpkg_installed\x86-windows-static-md\include\ft2build.h" (
    echo [dependencies] FreeType headers are missing after installation.
    goto fail
)
if not exist "%ROOT%\vcpkg_installed\x86-windows-static-md\include\luajit\lua.hpp" (
    echo [dependencies] LuaJIT headers are missing after installation.
    goto fail
)
if not exist "%ROOT%\vcpkg_installed\x86-windows-static-md\lib\freetype.lib" (
    echo [dependencies] The Release FreeType library is missing after installation.
    goto fail
)
if not exist "%ROOT%\vcpkg_installed\x86-windows-static-md\lib\lua51.lib" (
    echo [dependencies] The Release LuaJIT library is missing after installation.
    goto fail
)

echo [dependencies] All dependencies are ready.
popd
endlocal
exit /b 0

:fail
set "RESULT=%ERRORLEVEL%"
echo [dependencies] Dependency installation failed. See the messages above for details.
popd
if "%RESULT%"=="0" set "RESULT=1"
endlocal & exit /b %RESULT%

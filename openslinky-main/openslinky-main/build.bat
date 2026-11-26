@echo off
setlocal

set ROOT=%~dp0
set BUILD=%ROOT%build
set GEN=MinGW Makefiles

where cmake >nul 2>nul || (echo cmake not found in PATH & exit /b 1)
where javac >nul 2>nul || (echo javac not found in PATH & exit /b 1)
where python >nul 2>nul || (echo python not found in PATH & exit /b 1)

cmake -S "%ROOT%." -B "%BUILD%" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%BUILD%" --target SlinkyClient --parallel || exit /b 1

if not exist "%ROOT%dist" mkdir "%ROOT%dist"
copy /y "%BUILD%\SlinkyClient\SlinkyClient.exe" "%ROOT%dist\SlinkyClient.exe" >nul
copy /y "%BUILD%\SlinkyClient\libslinky.dll" "%ROOT%dist\libslinky.dll" >nul

echo.
echo Built:
echo   %ROOT%dist\SlinkyClient.exe
echo   %ROOT%dist\libslinky.dll
endlocal

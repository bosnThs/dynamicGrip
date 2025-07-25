@echo off

RMDIR dist /S /Q

xmake project -k vsxmake
if %ERRORLEVEL% NEQ 0 exit 1
xmake f -m releasedbg
if %ERRORLEVEL% NEQ 0 exit 1
xmake
if %ERRORLEVEL% NEQ 0 exit 1

xcopy "build\windows\x64\releasedbg\*.dll" "dist\SKSE\Plugins\" /I /Y
xcopy "build\windows\x64\releasedbg\*.pdb" "dist\SKSE\Plugins\" /I /Y
xcopy "package" "dist" /I /Y /E

pause
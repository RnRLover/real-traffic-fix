@echo off
setlocal
if not exist project_files\RealTrafficFix-ThreePlatform.sln call scripts\generate_vs2022.bat || exit /b 1
msbuild project_files\RealTrafficFix-ThreePlatform.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
endlocal

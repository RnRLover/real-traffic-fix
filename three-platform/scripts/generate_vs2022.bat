@echo off
setlocal
if "%PLUGIN_SDK_DIR%"=="" (
  echo Set PLUGIN_SDK_DIR to the plugin-sdk root folder.
  exit /b 1
)
premake5 vs2022
endlocal

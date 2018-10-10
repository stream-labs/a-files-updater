if not exist "%DEPS_LOCAL_PATH%\%~1\" (
	appveyor AddMessage "%DEPS_LOCAL_PATH%\%~1\ didn't exist, regenerating..."
	mkdir "%DEPS_LOCAL_PATH%\%~1\"
	appveyor AddMessage "Downloading file from %DEPS_DIST_URI%%~1.7z to %DEPS_LOCAL_PATH%\%~1.7z"
	appveyor DownloadFile "%DEPS_DIST_URI%%~1.7z" -FileName "%DEPS_LOCAL_PATH%\%~1.7z"
	7z x "%DEPS_LOCAL_PATH%\%~1.7z" -o"%DEPS_LOCAL_PATH%\%~1" -y
)
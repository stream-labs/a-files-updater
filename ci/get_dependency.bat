curl -kLO "%DEPS_DIST_URI%%~1.7z" -f --retry 5
7z x "%~1.7z" -o"%DEPS_LOCAL_PATH%\%~1" -y
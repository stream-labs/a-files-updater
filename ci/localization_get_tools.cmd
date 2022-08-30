REM download gettext tools from https://mlocati.github.io/articles/gettext-iconv-windows.html

set GETTEXT_URL=https://github.com/mlocati/gettext-iconv-windows/releases/download/v0.21-v1.16/gettext0.21-iconv1.16-static-64.zip
set GETTEXT_DIR=gettext_dist
cd build

if exist %GETTEXT_DIR%\ (
    echo "get text tools already installed"
) else (
    curl -kLO %GETTEXT_URL% -f --retry 5 -C -
    7z x gettext0.21-iconv1.16-static-64.zip -aoa -o%GETTEXT_DIR%
)

cd ..
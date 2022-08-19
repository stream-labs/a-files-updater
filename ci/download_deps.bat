mkdir "%DEPS_LOCAL_PATH%"
set ZLIB_DIST_NAME=zlib-1.2.11-static-mt
set OPENSSL_DIST_NAME=openssl-1.1.1c-x64
set BOOST_DIST_NAME=boost-vc143-1_79_0-bin
set DEPS_DIST_URI=https://s3-us-west-2.amazonaws.com/streamlabs-obs-updater-deps

curl -kLO "%DEPS_DIST_URI%/%ZLIB_DIST_NAME%.7z" -f --retry 5
curl -kLO "%DEPS_DIST_URI%/%OPENSSL_DIST_NAME%.7z" -f --retry 5
curl -kLO "%DEPS_DIST_URI%/%BOOST_DIST_NAME%.7z" -f --retry 5
7z x "%ZLIB_DIST_NAME%.7z" -o"%DEPS_LOCAL_PATH%\zlib" -y 
7z x "%OPENSSL_DIST_NAME%.7z" -o"%DEPS_LOCAL_PATH%\openssl" -y
7z x "%BOOST_DIST_NAME%.7z" -o"%DEPS_LOCAL_PATH%\boost" -y
cmake ^
	-H. ^
	-B"%BUILD_DIR%" ^
	-G"Visual Studio 15 2017" ^
	-A x64 ^
	-DFLTK_ROOT="%FLTK_ROOT%" ^
	-DBOOST_ROOT="%BOOST_ROOT%" ^
	-DOPENSSL_ROOT_DIR="%OPENSSL_ROOT%" ^
	-DZLIB_ROOT="%ZLIB_ROOT%" ^
	-DWIN_MT="%WIN_MT%" ^
	-DUSE_STREAMLABS_RESOURCE=ON

cmake ^
	--build "%BUILD_DIR%" ^
	--config %BUILD_TYPE% ^
	-- /logger:"C:\Program Files\AppVeyor\BuildAgent\Appveyor.MSBuildLogger.dll"
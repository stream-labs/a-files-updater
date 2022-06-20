# How to build
This project depends on a few libraries.

* OpenSSL 1.1.x
* Boost 1.79.0 (system, filesystem, thread, regex, and headers for asio, beast, iostreams, core, util)
* zlib 1.2.x

When configuring, the following cmake variables are of interest:

* ZLIB_ROOT - Specifies the root of the ZLIB library installation (the cpack structure or `cmake --build <build> --target install`).
* BOOST_ROOT - Specifies the root of the Boost libraries installation (the result of `b2 install`).
* Boost_NO_BOOST_CMAKE - To disable search for boost-cmake files. 
* OPENSSL_ROOT_DIR - Specifies the root of the OpenSSL installation (the result of `nmake install`, see details below).

In order to build, set the above variables (see CMake find_package documentation for more flexible hints) and then run cmake however you want.
A C++17 comformant compiler is required. Outside of that, as long as the dependencies are met and compatible, you can use whatever compiler you want.

## Openssl build from source
* clone https://github.com/openssl
* install perl. 
  download from http://strawberryperl.com/
* in console check perl and setup VC env
  perl --version 
  "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvars64.bat"
* configure to build 64 static lib 
  perl Configure VC-WIN64A no-asm no-shared --prefix=C:\work\libs\openssl-lib-vvv-x64\
* build and install 
  nmake 
  nmake test && nmake install 
* make an archive from install path 
# How to build
This project depends on a few libraries.

* OpenSSL 1.1.x
* Boost 1.67.0 (system, filesystem, thread, regex, and headers for asio, beast, iostreams, core, util)
* zlib 1.2.x

When configuring, the following cmake variables are of interest:

* ZLIB_ROOT - Specifies the root of the ZLIB library installation (the cpack structure or `cmake --build <build> --target install`).
* BOOST_ROOT - Specifies the root of the Boost libraries installation (the result of `b2 install`).
* OPENSSL_ROOT_DIR - Specifies the root of the OpenSSL installation (the result of `nmake install`).

In order to build, set the above variables (see CMake find_package documentation for more flexible hints) and then run cmake however you want.
As far as I know, a C++11 comformant compiler is required. Outside of that, as long as the dependencies are met and compatible, you can use whatever compiler you want.
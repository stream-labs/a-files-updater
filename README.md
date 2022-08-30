# How to build
As easy as: 
```
set PATH=%PATH%;C:\Program Files\7-Zip\

set DEPS_LOCAL_PATH=build/deps

ci\download_deps.bat
ci\localization_prepare_binaries.cmd

cmake -H"." -B"build" -G"Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=RelWithDebInfo -A x64 -DCMAKE_DEPS_DIR=%CD%/build/deps -DCMAKE_INSTALL_PREFIX="%CD%/build/distribute/a-file-updater"

cmake --build build --target install --config RelWithDebInfo
```
##
This project depends on a few libraries.

* OpenSSL 1.1.x
* Boost 1.79.0 (system, filesystem, thread, regex, and headers for asio, beast, iostreams, core, util)
* zlib 1.2.x

Prepackaged binaries of this libraries will be downloaded by ci\download_deps.bat and put inside build/deps

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

## Localization

Boost.locale lib with a gettext format used for a localization. 
mo files included in exe by windows resources. 
### Commands 

`ci\localization_prepare_binaries.cmd` - prepares mo files with current translation 

`ci\localization_set_translations.cmd` - update po files with current strings from source code 

### Add new language 

* Add new lang code into `ci\localization_get_tools.cmd` and run `ci\localization_set_translations.cmd`
* Translate lines inside `locale\NEW_LANG\LC_MESSAGES\messages.po`
* Add new mo file to `resources\slobs-updater.rc`
* Add it to `locales_resources` map inside `get_messages_callback()`
* Prepare binaries `ci\localization_prepare_binaries.cmd`
* Make a new build 
* Do not forget to commit `locale\NEW_LANG\LC_MESSAGES\messages.po`
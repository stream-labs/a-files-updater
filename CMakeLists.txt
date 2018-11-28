cmake_minimum_required(VERSION 3.12)

project(slobs-updater)

set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	"${slobs-updater_SOURCE_DIR}/cmake"
)

set(OPENSSL_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

option(USE_STREAMLABS_RESOURCE "Embed and use the streamlabs resource file in the resulting executable" OFF)

find_package(ZLIB REQUIRED)
find_package(OpenSSL 1.1.0 EXACT REQUIRED)

find_package(Boost
	1.67.0 EXACT
	REQUIRED
	COMPONENTS
		filesystem
		iostreams
		system
		date_time
)

add_executable(slobs-updater
	WIN32
	src/main.cc
	src/update-parameters.hpp
	src/cli-parser.cc src/cli-parser.hpp
	src/uri-parser.cc src/uri-parser.hpp
	src/update-client.cc src/update-client.hpp

	# argtable
	src/argtable/argtable3.c src/argtable/argtable3.h

	# fmt
	src/fmt/format.cc
	src/fmt/posix.cc
	src/fmt/fmt/format.h
	src/fmt/fmt/core.h
	src/fmt/fmt/format-inl.h

	# boost iostreams
	src/iostreams/gzip.cpp
	src/iostreams/zlib.cpp
	src/checksum-filters.hpp

	# logger
	src/logger/log.c src/logger/log.h
)

if (USE_STREAMLABS_RESOURCE)
	target_sources(slobs-updater PUBLIC resources/slobs-updater.rc)
endif ()

target_compile_options(slobs-updater
	PRIVATE
		$<IF:$<CONFIG:Debug>,-MTd,-MT>
		-W3
		-bigobj
)

target_include_directories(slobs-updater
	PRIVATE src/argtable
	PRIVATE src/fmt
)

target_link_libraries(slobs-updater
	Boost::boost
	Boost::filesystem
	Boost::iostreams
	Boost::system
	Boost::date_time
	OpenSSL::SSL #Note that this pulls in Crypto
	ZLIB::ZLIB
	# -MANIFESTUAC:level=requireAdministrator
)

# OpenSSL needs us to link against libraries it depends
# on in order to be runtime agnostic
target_link_libraries(slobs-updater Crypt32)

target_compile_definitions(slobs-updater
	PRIVATE
		-D_WIN32_WINNT=0x600
		-DWINVER=0x600
		-DNOMINMAX
		-DWIN32_LEAN_AND_MEAN
		-DUNICODE
		-D_UNICODE
		-DBOOST_IOSTREAMS_NO_LIB
)

find_program(WIN_MT "mt.exe")

if (NOT WIN_MT)
	message(FATAL_ERROR "Failed to find mt.exe to embed manifest")
endif ()

add_custom_command(
	TARGET slobs-updater
	POST_BUILD
	COMMAND
		"${WIN_MT}"
		-manifest "${slobs-updater_SOURCE_DIR}/resources/slobs-updater.exe.manifest"
		-outputresource:"$<TARGET_FILE:slobs-updater>"
)
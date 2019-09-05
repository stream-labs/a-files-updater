cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0074 NEW)

project(slobs-updater)

set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	"${slobs-updater_SOURCE_DIR}/cmake"
)

#set(OPENSSL_USE_STATIC_LIBS TRUE)
#set(OPENSSL_MSVC_STATIC_RT TRUE)
#find_package(OpenSSL REQUIRED )
#message(STATUS "${OPENSSL_LIBRARIES}")

set(OPENSSL_LIBRARIES ${OPENSSL_ROOT_DIR}/lib/libssl.lib ${OPENSSL_ROOT_DIR}/lib/libcrypto.lib )

option(USE_STREAMLABS_RESOURCE "Embed and use the streamlabs resource file in the resulting executable" OFF)

find_package(ZLIB REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

find_package(Boost
	1.70.0 EXACT
	REQUIRED
	COMPONENTS
		filesystem
		iostreams
		system
		date_time
)
	
include(cppcheck)

cppcheck(EXCLUDE "")

file(GLOB_RECURSE SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cc ${PROJECT_SOURCE_DIR}/src/*/*.c ${PROJECT_SOURCE_DIR}/src/*/*.cpp)

add_executable(slobs-updater
	WIN32
	${SRC_FILES}
)

if (USE_STREAMLABS_RESOURCE)
	target_sources(slobs-updater PUBLIC resources/slobs-updater.rc)
endif ()

target_compile_options(slobs-updater
	PRIVATE
		$<IF:$<CONFIG:Debug>,-MTd,-MT>
		-W3 -Zi
		-bigobj
)

target_link_options(slobs-updater
	PRIVATE
		 /DEBUG 
)

set_property(TARGET slobs-updater PROPERTY CXX_STANDARD 17)
set_property(TARGET slobs-updater PROPERTY CXX_STANDARD_REQUIRED ON)

target_include_directories(slobs-updater
	PRIVATE src/argtable
	PRIVATE src/fmt
)

target_include_directories(slobs-updater
	SYSTEM PUBLIC ${OPENSSL_ROOT_DIR}/include
)

target_link_libraries(slobs-updater
	comctl32
	Boost::boost
	Boost::filesystem
	Boost::iostreams
	Boost::system
	Boost::date_time
	${OPENSSL_LIBRARIES}
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

		#It might be worth removing this eventually
		-D_CRT_SECURE_NO_WARNINGS

		#This can be removed with Boost 1.68.0 I believe
		-D_SILENCE_FPOS_SEEKPOS_DEPRECATION_WARNING
		-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING 
)

find_program(WIN_MT "mt.exe")


if (NOT WIN_MT)
	message(FATAL_ERROR "Failed to find mt.exe to embed manifest")
endif ()
cppcheck_add_project(slobs-updater)

add_custom_command(
	TARGET slobs-updater
	POST_BUILD
	COMMAND
		"${WIN_MT}"
		-manifest "${slobs-updater_SOURCE_DIR}/resources/slobs-updater.exe.manifest"
		-outputresource:"$<TARGET_FILE:slobs-updater>"
)
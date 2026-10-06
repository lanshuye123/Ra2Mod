#pragma once

#define _STR(x) _STR_(x)
#define _STR_(x) #x

#pragma region Version numbering

// Indicates project maturity and completeness
#define VERSION_MAJOR 0

// Indicates major changes and significant additions, like new logics
#define VERSION_MINOR 1

// Indicates minor changes, like vanilla bugfixes, unhardcodings or hacks
#define VERSION_REVISION 0

// Indicates bugfixes only
#define VERSION_PATCH 0

#define VERSION_LONG_STR _STR(VERSION_MAJOR) "." _STR(VERSION_MINOR) "." _STR(VERSION_REVISION) "." _STR(VERSION_PATCH)

#pragma endregion

#pragma region Product identity

#define PRODUCT_NAME "HAres"
#define PRODUCT_FILE_NAME PRODUCT_NAME ".dll"
#define PRODUCT_INI_NAME PRODUCT_NAME ".ini"
#define PRODUCT_SUMMARY "Ares-compatible YR engine extension"

#define FILE_VERSION VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_PATCH
#define FILE_VERSION_STR VERSION_LONG_STR
#define PRODUCT_VERSION "v" FILE_VERSION_STR

#if defined(DEBUG) || defined(_DEBUG)
	#define BUILD_TYPE_NAME "debug build"
	#define TESTING_BUILD
#else
	#define BUILD_TYPE_NAME "release build"
#endif

#define FILE_DESCRIPTION PRODUCT_NAME " " PRODUCT_VERSION " (" BUILD_TYPE_NAME ")"

#pragma endregion

#pragma once

/**
 * Initialize flags for Windows compilation, to avoid conflicts with GNU libraries
 * (mainly `select` from WinSocket2 and `select` from sys/select in GNU).
 * Included in all files during compilation, if using VSCode you can add 
 * -include "libs/wininit.h" to your compiler settings in c_cpp_properties.json
 * to make the IDE recognition properly work, if needed.
*/

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)
	#define _fseeki64 fseek
	#define _ftelli64 ftell

    #define __USE_W32_SOCKETS
    #include <winsock2.h>
    #include <windows.h>
	#include <ws2tcpip.h>
    #define USING_WIN
#endif
# Windows/386 with the i686 MinGW-w64 cross toolchain.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_PREFIX_PATH /usr/i686-w64-mingw32/usr)

set(CMAKE_C_FLAGS_INIT "-I/usr/i686-w64-mingw32/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-I/usr/i686-w64-mingw32/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L/usr/i686-w64-mingw32/usr/lib")

set(ENV{PKG_CONFIG_PATH} "/usr/i686-w64-mingw32/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/i686-w64-mingw32/usr/lib/pkgconfig")

set(OPENSSL_ROOT_DIR /usr/i686-w64-mingw32/usr)
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(CURL_USE_STATIC_LIBS TRUE)
set(CURL_LIBRARY /usr/i686-w64-mingw32/usr/lib/libcurl.a CACHE FILEPATH "" FORCE)
set(CURL_INCLUDE_DIR /usr/i686-w64-mingw32/usr/include CACHE PATH "" FORCE)

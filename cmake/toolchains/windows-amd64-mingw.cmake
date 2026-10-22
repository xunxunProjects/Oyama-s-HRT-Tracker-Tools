# Windows/amd64 with MinGW-w64 and static OpenSSL/libcurl from the cross root.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 /usr/lib/mingw64-toolchain)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_PREFIX_PATH /usr/x86_64-w64-mingw32/usr)

set(CMAKE_C_FLAGS_INIT "-I/usr/x86_64-w64-mingw32/usr/include")
set(CMAKE_CXX_FLAGS_INIT "-I/usr/x86_64-w64-mingw32/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-L/usr/x86_64-w64-mingw32/usr/lib")

set(ENV{PKG_CONFIG_PATH} "/usr/x86_64-w64-mingw32/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/x86_64-w64-mingw32/usr/lib/pkgconfig")

set(OPENSSL_ROOT_DIR /usr/x86_64-w64-mingw32/usr)
set(OPENSSL_USE_STATIC_LIBS TRUE)
set(CURL_USE_STATIC_LIBS TRUE)
set(CURL_LIBRARY /usr/x86_64-w64-mingw32/usr/lib/libcurl.a CACHE FILEPATH "" FORCE)
set(CURL_INCLUDE_DIR /usr/x86_64-w64-mingw32/usr/include CACHE PATH "" FORCE)

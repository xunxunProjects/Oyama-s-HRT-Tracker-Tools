# Linux/386 with the host multilib toolchain and 32-bit OpenSSL/libcurl.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)

set(CMAKE_C_FLAGS_INIT "-m32")
set(CMAKE_CXX_FLAGS_INIT "-m32")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-m32")

set(CMAKE_FIND_ROOT_PATH /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Gentoo's multilib layout puts 32-bit libraries in /usr/lib when /usr/lib64
# holds the native ones; other distributions use /usr/lib32.
if(EXISTS /usr/lib32/libssl.so)
    set(HRT_32BIT_LIBDIR /usr/lib32)
else()
    set(HRT_32BIT_LIBDIR /usr/lib)
endif()

# Keep pkg-config away from the 64-bit .pc files; CMake's FindOpenSSL may
# prefer pkg-config over OPENSSL_*_LIBRARY.
set(ENV{PKG_CONFIG_PATH} "${HRT_32BIT_LIBDIR}/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${HRT_32BIT_LIBDIR}/pkgconfig:/usr/share/pkgconfig")

set(OPENSSL_ROOT_DIR /usr)
set(OPENSSL_INCLUDE_DIR /usr/include)
set(OPENSSL_SSL_LIBRARY ${HRT_32BIT_LIBDIR}/libssl.so)
set(OPENSSL_CRYPTO_LIBRARY ${HRT_32BIT_LIBDIR}/libcrypto.so)
set(CURL_INCLUDE_DIR /usr/include)
set(CURL_LIBRARY ${HRT_32BIT_LIBDIR}/libcurl.so)

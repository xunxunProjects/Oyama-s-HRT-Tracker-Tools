cmake_minimum_required(VERSION 3.16)
set(HRT_TOOL_VERSION "0.1.1" CACHE STRING "hrt-tool release version")
set(HRT_RUST_TARGET "" CACHE STRING "Rust target triple (empty = host)")
set(HRT_GOOS "" CACHE STRING "Go GOOS (empty = host)")
set(HRT_GOARCH "" CACHE STRING "Go GOARCH (empty = host)")
set(HRT_GO_CC "" CACHE STRING "C compiler for cgo (empty = host default)")
set(HRT_GO_CXX "" CACHE STRING "C++ compiler for cgo (empty = host default)")
set(HRT_GO_CGO_CFLAGS "" CACHE STRING "cgo CFLAGS (empty = none)")
set(HRT_GO_CGO_LDFLAGS "" CACHE STRING "cgo LDFLAGS (empty = none)")
project(hrt_tool VERSION ${HRT_TOOL_VERSION} LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

option(HRT_TOOL_ENABLE_F2F "Build the Go F2F bridge" ON)
option(HRT_TOOL_ENABLE_WARNINGS "Enable compiler warnings" ON)
option(HRT_TOOL_ENABLE_WERROR "Treat compiler warnings as errors" OFF)
option(HRT_TOOL_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_program(CARGO_EXECUTABLE cargo REQUIRED)

# On Windows, link the static libcurl path directly. FindCURL prefers the
# import library (libcurl.dll.a), which would make the release depend on a
# DLL that is not shipped.
if(WIN32 AND CURL_LIBRARY)
    set(HRT_CURL_TARGET "${CURL_LIBRARY}")
    set(HRT_CURL_INCLUDE "${CURL_INCLUDE_DIR}")
    set(HRT_CURL_STATIC TRUE)
else()
    set(HRT_CURL_TARGET CURL::libcurl)
    set(HRT_CURL_INCLUDE "")
endif()

# nlohmann/json is header-only. Cross builds must not add the host
# /usr/include to the search path: doing so lets the target's libstdc++
# __has_include pick up host glibc headers. Stage only the nlohmann tree.
if(CMAKE_CROSSCOMPILING)
    get_target_property(HRT_NLOHMANN_INCLUDE nlohmann_json::nlohmann_json
        INTERFACE_INCLUDE_DIRECTORIES)
    list(GET HRT_NLOHMANN_INCLUDE 0 HRT_NLOHMANN_INCLUDE_DIR)
    set(HRT_HOST_INCLUDE ${CMAKE_BINARY_DIR}/host-include)
    file(MAKE_DIRECTORY ${HRT_HOST_INCLUDE}/nlohmann)
    file(COPY ${HRT_NLOHMANN_INCLUDE_DIR}/nlohmann/
         DESTINATION ${HRT_HOST_INCLUDE}/nlohmann)
    set(HRT_NLOHMANN_TARGET "")
else()
    set(HRT_HOST_INCLUDE "")
    set(HRT_NLOHMANN_TARGET nlohmann_json::nlohmann_json)
endif()

function(hrt_tool_warnings target)
    if(NOT HRT_TOOL_ENABLE_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4)
        if(HRT_TOOL_ENABLE_WERROR)
            target_compile_options("${target}" PRIVATE /WX)
        endif()
    else()
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
        if(HRT_TOOL_ENABLE_WERROR)
            target_compile_options("${target}" PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(HRT_TOOL_ENABLE_SANITIZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "HRT_TOOL_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(hrtcore STATIC
    native/core/version.c
    native/core/crypto.c
    native/core/base64.c
    native/core/pk.c
)
target_include_directories(hrtcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(hrtcore PRIVATE
    HRT_CORE_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(hrtcore PUBLIC OpenSSL::Crypto)

set(HRTPRINT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rust/hrt-print)
if(HRT_RUST_TARGET)
    set(HRTPRINT_LIB ${HRTPRINT_DIR}/target/${HRT_RUST_TARGET}/release/libhrtprint.a)
    set(HRTPRINT_TARGET_ARG --target ${HRT_RUST_TARGET})
else()
    set(HRTPRINT_LIB ${HRTPRINT_DIR}/target/release/libhrtprint.a)
    set(HRTPRINT_TARGET_ARG "")
endif()
add_custom_command(
    OUTPUT ${HRTPRINT_LIB}
    COMMAND ${CMAKE_COMMAND} -E env HRTPRINT_VERSION=${PROJECT_VERSION}
            ${CARGO_EXECUTABLE} build --release --offline ${HRTPRINT_TARGET_ARG}
            --manifest-path ${HRTPRINT_DIR}/Cargo.toml
    DEPENDS ${HRTPRINT_DIR}/Cargo.toml ${HRTPRINT_DIR}/Cargo.lock
            ${HRTPRINT_DIR}/build.rs ${HRTPRINT_DIR}/src/lib.rs
    WORKING_DIRECTORY ${HRTPRINT_DIR}
    COMMENT "Building Rust hrtprint"
    VERBATIM
)
add_custom_target(hrtprint_build ALL DEPENDS ${HRTPRINT_LIB})

set(HRT_F2F_SOURCES src/f2f_bridge_stub.cpp)
set(HRTF2F_INCLUDE_DIR "")
set(HRTF2F_LINK_LIB "")
set(HRTF2F_DEPENDENCY "")
set(HRT_F2F_ENABLED_VALUE 0)
if(HRT_TOOL_ENABLE_F2F)
    set(HRT_F2F_ENABLED_VALUE 1)
    find_program(GO_EXECUTABLE go REQUIRED)
    set(HRTF2F_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../hrt-f2f)
    set(HRTF2F_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/hrtf2f)
    set(HRTF2F_LIB ${HRTF2F_BUILD_DIR}/libhrtf2f.a)
    set(HRTF2F_HEADER ${HRTF2F_BUILD_DIR}/libhrtf2f.h)
    file(GLOB HRTF2F_GO_SOURCES CONFIGURE_DEPENDS ${HRTF2F_DIR}/*.go)
    set(HRTF2F_ENV CGO_ENABLED=1)
    foreach(pair "GOOS;HRT_GOOS" "GOARCH;HRT_GOARCH" "CC;HRT_GO_CC"
                 "CXX;HRT_GO_CXX" "CGO_CFLAGS;HRT_GO_CGO_CFLAGS"
                 "CGO_LDFLAGS;HRT_GO_CGO_LDFLAGS")
        list(GET pair 0 env_name)
        list(GET pair 1 var_name)
        if(${var_name})
            list(APPEND HRTF2F_ENV "${env_name}=${${var_name}}")
        endif()
    endforeach()
    add_custom_command(
        OUTPUT ${HRTF2F_LIB} ${HRTF2F_HEADER}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${HRTF2F_BUILD_DIR}
        COMMAND ${CMAKE_COMMAND} -E env ${HRTF2F_ENV}
                ${GO_EXECUTABLE} build -buildmode=c-archive -trimpath
                -ldflags "-X main.buildVersion=${PROJECT_VERSION}"
                -o ${HRTF2F_LIB} .
        DEPENDS ${HRTF2F_GO_SOURCES} ${HRTF2F_DIR}/go.mod ${HRTF2F_DIR}/go.sum
        WORKING_DIRECTORY ${HRTF2F_DIR}
        COMMENT "Building the Go F2F library"
        VERBATIM
    )
    add_custom_target(hrtf2f_build DEPENDS ${HRTF2F_LIB} ${HRTF2F_HEADER})
    set_source_files_properties(src/f2f_bridge.cpp PROPERTIES
        OBJECT_DEPENDS ${HRTF2F_HEADER})
    set(HRT_F2F_SOURCES src/f2f_bridge.cpp)
    set(HRTF2F_INCLUDE_DIR ${HRTF2F_BUILD_DIR})
    set(HRTF2F_LINK_LIB ${HRTF2F_LIB})
    set(HRTF2F_DEPENDENCY hrtf2f_build)
endif()

set(HRT_PLATFORM_SOURCES "")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    list(APPEND HRT_PLATFORM_SOURCES src/mingw32_unwind_stub.c)
endif()

add_library(hrtcpp STATIC
    src/add_interactive.cpp
    src/api_live.cpp
    src/backup_envelope.cpp
    src/cli.cpp
    src/command_flags.cpp
    src/commands.cpp
    src/completion.cpp
    src/console.cpp
    src/doctor.cpp
    src/encrypted_file.cpp
    src/entropy.cpp
    src/exit_codes.cpp
    ${HRT_F2F_SOURCES}
    src/file_io.cpp
    src/format.cpp
    src/http.cpp
    src/hrt_client.cpp
    src/hrt_pairing.cpp
    src/hrt_protocol.cpp
    src/i18n.cpp
    src/api_hrt.cpp
    src/link.cpp
    src/local_commands.cpp
    src/log.cpp
    src/model.cpp
    src/net_error.cpp
    src/paths.cpp
    src/pk.cpp
    src/prompt.cpp
    src/proxy_commands.cpp
    src/session.cpp
    src/snapshot.cpp
    src/status.cpp
    src/trust.cpp
    src/vault.cpp
    src/version.cpp
)
target_include_directories(hrtcpp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${HRTF2F_INCLUDE_DIR}
    ${HRT_CURL_INCLUDE}
)
if(HRT_HOST_INCLUDE)
    target_include_directories(hrtcpp PRIVATE ${HRT_HOST_INCLUDE})
endif()
target_compile_definitions(hrtcpp PUBLIC
    HRT_TOOL_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    HRT_TOOL_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    HRT_TOOL_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    HRT_TOOL_F2F_ENABLED=${HRT_F2F_ENABLED_VALUE}
)
if(HRT_CURL_STATIC)
    target_compile_definitions(hrtcpp PUBLIC CURL_STATICLIB)
endif()
target_link_libraries(hrtcpp PUBLIC
    hrtcore
    ${HRT_NLOHMANN_TARGET}
    ${HRTPRINT_LIB}
    ${HRTF2F_LINK_LIB}
    ${HRT_CURL_TARGET}
    OpenSSL::SSL
    OpenSSL::Crypto
)
add_dependencies(hrtcpp hrtprint_build)
if(HRTF2F_DEPENDENCY)
    add_dependencies(hrtcpp ${HRTF2F_DEPENDENCY})
endif()
hrt_tool_warnings(hrtcore)
hrt_tool_warnings(hrtcpp)

add_executable(hrt-tool src/main.cpp)
target_sources(hrt-tool PRIVATE ${HRT_PLATFORM_SOURCES})
target_link_libraries(hrt-tool PRIVATE
    hrtcpp
    ${HRT_CURL_TARGET}
    Threads::Threads
)
if(WIN32)
    # Repeat the static archives and system libraries at the end: GNU ld
    # resolves them in order, and the Go/Rust archives are pulled in through
    # hrtcpp's interface.
    target_link_libraries(hrt-tool PRIVATE
        hrtcore ${HRTPRINT_LIB} ${HRTF2F_LINK_LIB}
        ws2_32 crypt32 bcrypt advapi32 iphlpapi ntdll userenv winmm z)
else()
    target_link_libraries(hrt-tool PRIVATE ${CMAKE_DL_LIBS} m)
endif()
add_dependencies(hrt-tool hrtprint_build)
hrt_tool_warnings(hrt-tool)

install(TARGETS hrt-tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES docs/PRIVACY_ENVIRONMENTS.md DESTINATION ${CMAKE_INSTALL_DOCDIR})

add_custom_target(update
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target hrt-tool
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR}
    COMMENT "Rebuilding and reinstalling hrt-tool"
    VERBATIM
)

include(CTest)
if(BUILD_TESTING)
    add_executable(version_test tests/version_test.cpp)
    target_link_libraries(version_test PRIVATE hrtcpp)
    add_dependencies(version_test hrtprint_build)
    add_test(NAME version COMMAND version_test)

    add_executable(exit_code_test tests/exit_code_test.cpp)
    target_link_libraries(exit_code_test PRIVATE hrtcpp)
    add_test(NAME exit_code COMMAND exit_code_test)

    add_executable(proxy_test tests/proxy_test.cpp)
    target_link_libraries(proxy_test PRIVATE hrtcpp)
    add_test(NAME proxy COMMAND proxy_test)

    add_executable(parse_args_test tests/parse_args_test.cpp)
    target_link_libraries(parse_args_test PRIVATE hrtcpp)
    add_test(NAME parse_args COMMAND parse_args_test)

    add_executable(vault_links_test tests/vault_links_test.cpp)
    target_link_libraries(vault_links_test PRIVATE hrtcpp)
    add_test(NAME vault_links COMMAND vault_links_test)

    add_executable(link_test tests/link_test.cpp)
    target_link_libraries(link_test PRIVATE hrtcpp)
    add_test(NAME link COMMAND link_test)

    add_executable(link_commands_test tests/link_commands_test.cpp)
    target_link_libraries(link_commands_test PRIVATE hrtcpp)
    add_test(NAME link_commands COMMAND link_commands_test)

    add_executable(session_test tests/session_test.cpp)
    target_link_libraries(session_test PRIVATE hrtcpp)
    add_test(NAME session COMMAND session_test)

    add_executable(i18n_test tests/i18n_test.cpp)
    target_link_libraries(i18n_test PRIVATE hrtcpp)
    add_test(NAME i18n COMMAND i18n_test)

    add_executable(net_error_test tests/net_error_test.cpp)
    target_link_libraries(net_error_test PRIVATE hrtcpp)
    add_test(NAME net_error COMMAND net_error_test)

    add_executable(console_test tests/console_test.cpp)
    target_link_libraries(console_test PRIVATE hrtcpp)
    add_test(NAME console COMMAND console_test)

    add_executable(status_test tests/status_test.cpp)
    target_link_libraries(status_test PRIVATE hrtcpp)
    add_test(NAME status COMMAND status_test)

    add_executable(add_interactive_test tests/add_interactive_test.cpp)
    target_link_libraries(add_interactive_test PRIVATE hrtcpp)
    add_test(NAME add_interactive COMMAND add_interactive_test)

    add_executable(doctor_test tests/doctor_test.cpp)
    target_link_libraries(doctor_test PRIVATE hrtcpp)
    add_test(NAME doctor COMMAND doctor_test)

    add_executable(completion_test tests/completion_test.cpp)
    target_link_libraries(completion_test PRIVATE hrtcpp)
    add_test(NAME completion COMMAND completion_test)

    add_executable(proxy_commands_test tests/proxy_commands_test.cpp)
    target_link_libraries(proxy_commands_test PRIVATE hrtcpp)
    add_test(NAME proxy_commands COMMAND proxy_commands_test)

    add_executable(version_json_test tests/version_json_test.cpp)
    target_link_libraries(version_json_test PRIVATE hrtcpp)
    add_test(NAME version_json COMMAND version_json_test)

    if(HRT_TOOL_ENABLE_F2F)
        add_executable(f2f_bridge_test tests/f2f_bridge_test.cpp)
        target_link_libraries(f2f_bridge_test PRIVATE hrtcpp)
        add_test(NAME f2f_bridge COMMAND f2f_bridge_test)
    else()
        add_executable(f2f_bridge_test tests/f2f_bridge_stub_test.cpp)
        target_link_libraries(f2f_bridge_test PRIVATE hrtcpp)
        add_test(NAME f2f_bridge COMMAND f2f_bridge_test)
    endif()

    add_executable(console_integration_test tests/console_integration_test.cpp)
    target_link_libraries(console_integration_test PRIVATE hrtcpp)
    add_test(NAME console_integration COMMAND console_integration_test)

    add_executable(crypto_test tests/crypto_test.cpp)
    target_link_libraries(crypto_test PRIVATE hrtcore)
    add_test(NAME crypto COMMAND crypto_test)

    add_executable(entropy_test tests/entropy_test.cpp)
    target_link_libraries(entropy_test PRIVATE hrtcpp)
    add_test(NAME entropy COMMAND entropy_test)

    add_executable(vault_test tests/vault_test.cpp)
    target_link_libraries(vault_test PRIVATE hrtcpp)
    add_test(NAME vault COMMAND vault_test)

    add_executable(log_test tests/log_test.cpp)
    target_link_libraries(log_test PRIVATE hrtcpp)
    add_test(NAME log COMMAND log_test)

    add_executable(cli_test tests/cli_test.cpp)
    target_link_libraries(cli_test PRIVATE hrtcpp)
    add_test(NAME cli COMMAND cli_test)

    add_executable(local_commands_test tests/local_commands_test.cpp)
    target_link_libraries(local_commands_test PRIVATE hrtcpp Threads::Threads)
    target_compile_definitions(local_commands_test PRIVATE
        HRT_TOOL_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
    add_test(NAME local_commands COMMAND local_commands_test)

    add_executable(http_test tests/http_test.cpp)
    target_link_libraries(http_test PRIVATE hrtcpp)
    add_test(NAME http COMMAND http_test)

    add_executable(hrt_protocol_test tests/hrt_protocol_test.cpp)
    target_link_libraries(hrt_protocol_test PRIVATE hrtcpp)
    add_test(NAME hrt_protocol COMMAND hrt_protocol_test)

    add_executable(hrt_client_test tests/hrt_client_test.cpp)
    target_link_libraries(hrt_client_test PRIVATE hrtcpp)
    add_test(NAME hrt_client COMMAND hrt_client_test)

    add_executable(hrt_client_network_test tests/hrt_client_network_test.cpp)
    target_link_libraries(hrt_client_network_test PRIVATE hrtcpp Threads::Threads)
    add_test(NAME hrt_client_network COMMAND hrt_client_network_test)

    add_executable(hrt_pairing_test tests/hrt_pairing_test.cpp)
    target_link_libraries(hrt_pairing_test PRIVATE hrtcpp Threads::Threads)
    add_test(NAME hrt_pairing COMMAND hrt_pairing_test)

    add_executable(hrt_api_test tests/hrt_api_test.cpp)
    target_link_libraries(hrt_api_test PRIVATE hrtcpp Threads::Threads)
    add_test(NAME hrt_api COMMAND hrt_api_test)

    add_executable(hrt_login_test tests/hrt_login_test.cpp)
    target_link_libraries(hrt_login_test PRIVATE hrtcpp Threads::Threads)
    add_test(NAME hrt_login COMMAND hrt_login_test)

    add_executable(trust_test tests/trust_test.cpp)
    target_link_libraries(trust_test PRIVATE hrtcpp)
    add_test(NAME trust COMMAND trust_test)

    add_executable(api_test tests/api_test.cpp)
    target_link_libraries(api_test PRIVATE hrtcpp)
    add_test(NAME api COMMAND api_test)

    add_executable(pk_test tests/pk_test.cpp)
    target_link_libraries(pk_test PRIVATE hrtcpp)
    target_compile_definitions(pk_test PRIVATE
        HRT_TOOL_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
    add_test(NAME pk COMMAND pk_test)

    add_executable(model_test tests/model_test.cpp)
    target_link_libraries(model_test PRIVATE hrtcpp)
    add_test(NAME model COMMAND model_test)

    add_executable(snapshot_test tests/snapshot_test.cpp)
    target_link_libraries(snapshot_test PRIVATE hrtcpp)
    target_compile_definitions(snapshot_test PRIVATE
        HRT_TOOL_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")
    add_test(NAME snapshot COMMAND snapshot_test)

    add_executable(encrypted_file_test tests/encrypted_file_test.cpp)
    target_link_libraries(encrypted_file_test PRIVATE hrtcpp)
    add_test(NAME encrypted_file COMMAND encrypted_file_test)

    add_executable(backup_envelope_test tests/backup_envelope_test.cpp)
    target_link_libraries(backup_envelope_test PRIVATE hrtcpp)
    add_test(NAME backup_envelope COMMAND backup_envelope_test)

    add_test(NAME rust_print
        COMMAND ${CARGO_EXECUTABLE} test --offline
                --manifest-path ${HRTPRINT_DIR}/Cargo.toml)
    set_tests_properties(rust_print PROPERTIES WORKING_DIRECTORY ${HRTPRINT_DIR})
endif()

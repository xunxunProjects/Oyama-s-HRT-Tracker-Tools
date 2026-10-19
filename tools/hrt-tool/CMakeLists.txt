cmake_minimum_required(VERSION 3.16)
project(hrt_tool VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)
find_program(CARGO_EXECUTABLE cargo REQUIRED)

add_library(hrtcore STATIC
    native/core/version.c
    native/core/crypto.c
    native/core/base64.c
    native/core/pk.c
)
target_include_directories(hrtcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hrtcore PUBLIC OpenSSL::Crypto)

set(HRTPRINT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/rust/hrt-print)
set(HRTPRINT_LIB ${HRTPRINT_DIR}/target/release/libhrtprint.a)
add_custom_command(
    OUTPUT ${HRTPRINT_LIB}
    COMMAND ${CARGO_EXECUTABLE} build --release --offline
            --manifest-path ${HRTPRINT_DIR}/Cargo.toml
    DEPENDS ${HRTPRINT_DIR}/Cargo.toml ${HRTPRINT_DIR}/src/lib.rs
    WORKING_DIRECTORY ${HRTPRINT_DIR}
    COMMENT "Building Rust hrtprint"
    VERBATIM
)
add_custom_target(hrtprint_build ALL DEPENDS ${HRTPRINT_LIB})

add_library(hrtcpp STATIC
    src/api_stub.cpp
    src/cli.cpp
    src/commands.cpp
    src/entropy.cpp
    src/http.cpp
    src/log.cpp
    src/model.cpp
    src/paths.cpp
    src/pk.cpp
    src/prompt.cpp
    src/trust.cpp
    src/vault.cpp
    src/version.cpp
)
target_include_directories(hrtcpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hrtcpp PUBLIC
    hrtcore
    nlohmann_json::nlohmann_json
    ${HRTPRINT_LIB}
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
)
add_dependencies(hrtcpp hrtprint_build)

add_executable(hrt-tool src/main.cpp)
target_link_libraries(hrt-tool PRIVATE
    hrtcpp
    CURL::libcurl
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)
add_dependencies(hrt-tool hrtprint_build)

include(CTest)
if(BUILD_TESTING)
    add_executable(version_test tests/version_test.cpp)
    target_link_libraries(version_test PRIVATE hrtcpp)
    add_dependencies(version_test hrtprint_build)
    add_test(NAME version COMMAND version_test)

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

    add_executable(http_test tests/http_test.cpp)
    target_link_libraries(http_test PRIVATE hrtcpp)
    add_test(NAME http COMMAND http_test)

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
endif()

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
    src/api_live.cpp
    src/backup_envelope.cpp
    src/cli.cpp
    src/command_flags.cpp
    src/commands.cpp
    src/encrypted_file.cpp
    src/entropy.cpp
    src/exit_codes.cpp
    src/file_io.cpp
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
    src/session.cpp
    src/snapshot.cpp
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

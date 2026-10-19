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

add_library(hrtcore STATIC native/core/version.c)
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

add_executable(hrt-tool src/main.cpp src/version.cpp)
target_include_directories(hrt-tool PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hrt-tool PRIVATE
    hrtcore
    ${HRTPRINT_LIB}
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)
add_dependencies(hrt-tool hrtprint_build)

include(CTest)
if(BUILD_TESTING)
    add_executable(version_test tests/version_test.cpp src/version.cpp)
    target_include_directories(version_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(version_test PRIVATE hrtcore ${HRTPRINT_LIB})
    add_dependencies(version_test hrtprint_build)
    add_test(NAME version COMMAND version_test)
endif()

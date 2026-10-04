cmake_minimum_required(VERSION 3.14)

project(kvcache_project VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${PROJECT_SOURCE_DIR}/include)


add_library(resp_parser STATIC src/resp_parser.cpp)

add_library(store STATIC src/store.cpp)

add_library(query_executer STATIC src/query_executer.cpp)
target_link_libraries(query_executer PRIVATE store)

add_library(kvcache_server STATIC src/server.cpp)
target_link_libraries(kvcache_server PRIVATE resp_parser query_executer)


add_executable(kvcache src/main.cpp )
target_link_libraries(kvcache PRIVATE kvcache_server)
target_compile_options(kvcache PRIVATE
    -O2
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(kvcache_debug src/main.cpp )
target_compile_options(kvcache_debug PRIVATE
-O0
-g
-Wall
-Wextra
-Wpedantic
-fsanitize=address,undefined
-fno-omit-frame-pointer
)
target_link_options(kvcache_debug PRIVATE
-fsanitize=address,undefined
)
target_link_libraries(kvcache_debug PRIVATE kvcache_server)
# --------------------------------------------------
# Google Tests Import
# --------------------------------------------------

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY  https://github.com/google/googletest
    GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)
enable_testing()



# --------------------------------------------------
# KVcache Tests
# --------------------------------------------------

add_executable(kvcache_test
    tests/test_main.cpp
)

target_compile_options(kvcache_test PRIVATE
    -O0
    -g
    -Wall
    -Wextra
    -Wpedantic
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
)

target_link_options(kvcache_test PRIVATE
    -fsanitize=address,undefined
)

target_link_libraries(kvcache_test PRIVATE gtest_main)

# --------------------------------------------------
# Smoke Tests
# --------------------------------------------------

add_executable(smoke_test 
    tests/smoke_test.cpp
)
target_link_libraries(smoke_test PRIVATE gtest_main)


# --------------------------------------------------
# Store Tests
# --------------------------------------------------

add_executable(store_tests tests/store_tests.cpp)
target_link_libraries(store_tests PRIVATE gtest_main store)


# --------------------------------------------------
# Server Tests
# --------------------------------------------------

add_executable(server_tests tests/server_tests.cpp)
target_link_libraries(server_tests PRIVATE gtest_main kvcache_server)


# --------------------------------------------------
# Tests Discovery
# --------------------------------------------------

include(GoogleTest)
gtest_discover_tests(smoke_test)
gtest_discover_tests(store_tests)
gtest_discover_tests(server_tests)
gtest_discover_tests(kvcache_test)
# add_test(
#     NAME SmokeTest
#     COMMAND smoke_test
# )
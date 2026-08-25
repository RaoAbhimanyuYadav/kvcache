cmake_minimum_required(VERSION 3.14)

project(Parser VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# set(CORE_SOURCES
#     include/resp_parser.hpp
# )

# add_executable(parser_main tests/manual_resp_test.cpp ${CORE_SOURCES})


add_library(resp_parser STATIC src/resp_parser.cpp)
target_include_directories(resp_parser PUBLIC include)


add_executable(parser_main tests/manual_resp_test.cpp)
target_link_libraries(parser_main PRIVATE resp_parser)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY  https://github.com/google/googletest
    GIT_TAG        v1.15.2
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(run_tests tests/resp_parser_test.cpp)
target_include_directories(run_tests PRIVATE include)
target_link_libraries(run_tests PRIVATE resp_parser gtest gtest_main pthread)


include(GoogleTest)
gtest_discover_tests(run_tests)

add_test(
    NAME RESPParserTestSuite 
    COMMAND run_tests
)
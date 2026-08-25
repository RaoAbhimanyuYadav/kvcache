cmake_minimum_required(VERSION 3.14)

project(kvcache_project VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${PROJECT_SOURCE_DIR}/include)






include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY  https://github.com/google/googletest
    GIT_TAG        v1.15.2
)

FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(smoke_test 
    tests/smoke_test.cpp
)


target_link_libraries(smoke_test PRIVATE gtest_main)

include(GoogleTest)

gtest_discover_tests(smoke_test)

# add_test(
#     NAME SmokeTest
#     COMMAND smoke_test
# )
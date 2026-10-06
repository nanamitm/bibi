# Unit tests for the parts of the reader that do not need Qt WebEngine.
# A separate project so it builds with a plain Qt install (Core, Xml, Test):
#
#   cmake -S qt-reader/tests -B qt-reader/tests/build
#   cmake --build qt-reader/tests/build
#   ctest --test-dir qt-reader/tests/build --output-on-failure
cmake_minimum_required(VERSION 3.19)
project(BibiQtReaderTests LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Xml Test)

include(FetchContent)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(miniz)

enable_testing()

set(READER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

function(bibi_add_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${READER_SRC})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Xml Qt6::Test miniz)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

bibi_add_test(tst_bookmarkmanager
    tst_bookmarkmanager.cpp
    ${READER_SRC}/bookmarkmanager.cpp)

bibi_add_test(tst_epubreader
    tst_epubreader.cpp
    ${READER_SRC}/epubreader.cpp)
target_compile_definitions(tst_epubreader PRIVATE
    BIBI_SEARCH_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/search")

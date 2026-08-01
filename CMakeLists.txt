cmake_minimum_required(VERSION 3.12...3.28)
project(toyvm LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OMR_COMPILER ON CACHE BOOL "" FORCE)
set(OMR_JITBUILDER ON CACHE BOOL "" FORCE)
set(OMR_TEST_COMPILER ON CACHE BOOL "" FORCE)
set(OMR_EXAMPLE ON CACHE BOOL "" FORCE)
set(OMR_GC ON CACHE BOOL "" FORCE)
set(OMR_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

add_subdirectory(omr)

add_executable(toyvm
    src/lexer.cpp
    src/parser.cpp
    src/compiler.cpp
    src/vm.cpp
    src/interpreter.cpp
    src/jit.cpp
    src/main.cpp
)

target_include_directories(toyvm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/include_core
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/jitbuilder/release/cpp/include
    ${CMAKE_CURRENT_BINARY_DIR}/omr
    ${CMAKE_CURRENT_BINARY_DIR}/omr/include_core
)

target_link_libraries(toyvm PRIVATE
    omrgc
    omrport
    omrcore
    omrvmstartup
    jitbuilder
    omr_example_base
    pugixml
    omr_main_function
    j9thrstatic
)

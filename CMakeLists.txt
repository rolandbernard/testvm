cmake_minimum_required(VERSION 3.12...3.28)
project(toyvm LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OMR_COMPILER ON CACHE BOOL "" FORCE)
set(OMR_JITBUILDER ON CACHE BOOL "" FORCE)
set(OMR_TEST_COMPILER ON CACHE BOOL "" FORCE)
set(OMR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(OMR_GC ON CACHE BOOL "" FORCE)
set(OMR_FVTEST OFF CACHE BOOL "" FORCE)
set(OMR_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)

# OMR requires language glue, but the example glue roots copied pointers in a
# hash table.  Our glue scans Value slots in the VM's flat stack directly.
add_library(toyvm_glue_base INTERFACE)
target_include_directories(toyvm_glue_base INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glue
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue
)
target_link_libraries(toyvm_glue_base INTERFACE omr_base)

add_library(toyvm_gc_glue INTERFACE)
target_sources(toyvm_gc_glue INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/CollectorLanguageInterfaceImpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/CompactSchemeFixupObject.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/ConcurrentMarkingDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/EnvironmentDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/FrequentObjectsStats.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/GlobalCollectorDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glue/MarkingDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/ObjectIterator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/ObjectModelDelegate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/StartupManagerImpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/VerboseManagerImpl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glue/ScavengerDelegate.cpp
)
target_link_libraries(toyvm_gc_glue INTERFACE toyvm_glue_base)

add_library(toyvm_util_glue INTERFACE)
target_sources(toyvm_util_glue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/UtilGlue.c)
target_link_libraries(toyvm_util_glue INTERFACE toyvm_glue_base)
add_library(toyvm_ras_glue INTERFACE)
target_sources(toyvm_ras_glue INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/Profiling.c)
target_link_libraries(toyvm_ras_glue INTERFACE toyvm_glue_base)
add_library(toyvm_core_glue INTERFACE)
target_sources(toyvm_core_glue INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/omr/example/glue/LanguageVMGlue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glue/toyvm_glue.cpp
)
target_link_libraries(toyvm_core_glue INTERFACE toyvm_glue_base)

set(OMR_GC_GLUE_TARGET toyvm_gc_glue CACHE STRING "" FORCE)
set(OMR_UTIL_GLUE_TARGET toyvm_util_glue CACHE STRING "" FORCE)
set(OMR_RAS_GLUE_TARGET toyvm_ras_glue CACHE STRING "" FORCE)
set(OMR_CORE_GLUE_TARGET toyvm_core_glue CACHE STRING "" FORCE)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/glue
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
    pugixml
    omr_main_function
    j9thrstatic
)

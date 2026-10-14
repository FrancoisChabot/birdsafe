cmake_minimum_required(VERSION 3.20)

project(bs_sets VERSION 0.1.0 LANGUAGES C)

add_library(bs_sets
    src/bs_sets.c
    src/bs_sets_set.c
)
add_library(bs_sets::bs_sets ALIAS bs_sets)
add_library(birdsafe::sets ALIAS bs_sets)
add_library(birdsafe::bs_sets ALIAS bs_sets)

target_include_directories(bs_sets PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(bs_sets PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(bs_sets PUBLIC bs_core::bs_core bs_num::bs_num)
target_link_libraries(bs_sets PRIVATE Threads::Threads)

target_compile_definitions(bs_sets PRIVATE
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_sets,TYPE>,SHARED_LIBRARY>:BS_SETS_BUILDING_DLL>
)
target_compile_definitions(bs_sets PUBLIC
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_sets,TYPE>,SHARED_LIBRARY>:BS_SETS_SHARED>
)

if(BIRDSAFE_BUILD_TESTS)
    add_executable(test_bs_sets tests/test_bs_sets.c)
    target_link_libraries(test_bs_sets PRIVATE bs_sets)
    add_test(NAME test_bs_sets COMMAND test_bs_sets)

    add_executable(test_bs_sets_algebra tests/test_bs_sets_algebra.c)
    target_link_libraries(test_bs_sets_algebra PRIVATE bs_sets)
    add_test(NAME test_bs_sets_algebra COMMAND test_bs_sets_algebra)

    add_executable(test_sets_concurrency tests/test_sets_concurrency.c)
    target_link_libraries(test_sets_concurrency PRIVATE bs_sets Threads::Threads)
    add_test(NAME test_sets_concurrency COMMAND test_sets_concurrency)

    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        set(CMAKE_CXX_STANDARD 11)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        add_executable(test_sets_cpp_compat tests/test_cpp_compat.cpp)
        target_link_libraries(test_sets_cpp_compat PRIVATE bs_sets)
        add_test(NAME test_sets_cpp_compat COMMAND test_sets_cpp_compat)
    endif()
endif()

install(TARGETS bs_sets
    EXPORT birdsafeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

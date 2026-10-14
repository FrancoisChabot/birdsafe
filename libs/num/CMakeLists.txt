cmake_minimum_required(VERSION 3.20)

project(bs_num VERSION 0.1.0 LANGUAGES C)

add_library(bs_num
    src/bs_bignum.c
    src/bs_limb.c
    src/bs_num.c
)
add_library(bs_num::bs_num ALIAS bs_num)
add_library(birdsafe::num ALIAS bs_num)
add_library(birdsafe::bs_num ALIAS bs_num)

target_include_directories(bs_num PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(bs_num PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(bs_num PUBLIC bs_core::bs_core)
target_link_libraries(bs_num PRIVATE Threads::Threads)

target_compile_definitions(bs_num PRIVATE
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_num,TYPE>,SHARED_LIBRARY>:BS_NUM_BUILDING_DLL>
)
target_compile_definitions(bs_num PUBLIC
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_num,TYPE>,SHARED_LIBRARY>:BS_NUM_SHARED>
)

if(BIRDSAFE_BUILD_TESTS)
    add_executable(test_bs_num tests/test_bs_num.c)
    target_link_libraries(test_bs_num PRIVATE bs_num)
    target_include_directories(test_bs_num PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME test_bs_num COMMAND test_bs_num)

    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
        set(CMAKE_CXX_STANDARD 11)
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        add_executable(test_num_cpp_compat tests/test_cpp_compat.cpp)
        target_link_libraries(test_num_cpp_compat PRIVATE bs_num)
        add_test(NAME test_num_cpp_compat COMMAND test_num_cpp_compat)
    endif()
endif()

install(TARGETS bs_num
    EXPORT birdsafeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

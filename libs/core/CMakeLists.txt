cmake_minimum_required(VERSION 3.20)

project(bs_core VERSION 0.1.0 LANGUAGES C)

add_library(bs_core
    src/bs_alloc.c
    src/bs_status.c
    src/bs_truth.c
)
add_library(bs_core::bs_core ALIAS bs_core)
add_library(birdsafe::core ALIAS bs_core)

target_include_directories(bs_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(bs_core PRIVATE
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_core,TYPE>,SHARED_LIBRARY>:BS_CORE_BUILDING_DLL>
)
target_compile_definitions(bs_core PUBLIC
    $<$<STREQUAL:$<TARGET_PROPERTY:bs_core,TYPE>,SHARED_LIBRARY>:BS_CORE_SHARED>
)

if(BIRDSAFE_BUILD_TESTS)
    add_executable(test_bs_core tests/test_bs_core.c)
    target_link_libraries(test_bs_core PRIVATE bs_core)
    add_test(NAME test_bs_core COMMAND test_bs_core)

    add_executable(test_bs_core_default tests/test_bs_core_default.c)
    target_link_libraries(test_bs_core_default PRIVATE bs_core)
    add_test(NAME test_bs_core_default COMMAND test_bs_core_default)
endif()

install(TARGETS bs_core
    EXPORT birdsafeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

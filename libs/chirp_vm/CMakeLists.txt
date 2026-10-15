cmake_minimum_required(VERSION 3.20)

project(chirp_vm VERSION 0.1.0 LANGUAGES C)

add_library(chirp_vm
    src/chirp_symbols.c
    src/chirp_binding.c
    src/chirp_fiber.c
    src/chirp_vm.c
)
add_library(chirp_vm::chirp_vm ALIAS chirp_vm)
add_library(birdsafe::chirp_vm ALIAS chirp_vm)

target_include_directories(chirp_vm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(chirp_vm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(chirp_vm PUBLIC bs_sets::bs_sets bs_num::bs_num bs_core::bs_core)

if(BIRDSAFE_BUILD_TESTS)
    add_executable(test_chirp_vm tests/test_chirp_vm.c)
    target_link_libraries(test_chirp_vm PRIVATE chirp_vm)
    add_test(NAME test_chirp_vm COMMAND test_chirp_vm)
endif()

install(TARGETS chirp_vm
    EXPORT birdsafeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

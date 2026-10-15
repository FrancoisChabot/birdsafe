cmake_minimum_required(VERSION 3.20)

project(chirp_frontend VERSION 0.1.0 LANGUAGES C)

add_library(chirp_frontend
    src/chirp_str_view.c
    src/chirp_token.c
    src/chirp_lexer.c
    src/chirp_ast_arena.c
    src/chirp_ast.c
    src/chirp_ast_printer.c
    src/chirp_parser.c
)
add_library(chirp_frontend::chirp_frontend ALIAS chirp_frontend)
add_library(birdsafe::chirp_frontend ALIAS chirp_frontend)

target_include_directories(chirp_frontend PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(chirp_frontend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(chirp_frontend PUBLIC bs_core::bs_core)

if(BIRDSAFE_BUILD_TESTS)
    add_executable(test_chirp_lexer tests/test_chirp_lexer.c)
    target_link_libraries(test_chirp_lexer PRIVATE chirp_frontend)
    add_test(NAME test_chirp_lexer COMMAND test_chirp_lexer)

    add_executable(test_chirp_ast tests/test_chirp_ast.c)
    target_link_libraries(test_chirp_ast PRIVATE chirp_frontend)
    add_test(NAME test_chirp_ast COMMAND test_chirp_ast)

    add_executable(test_chirp_parser tests/test_chirp_parser.c)
    target_link_libraries(test_chirp_parser PRIVATE chirp_frontend)
    add_test(NAME test_chirp_parser COMMAND test_chirp_parser)
endif()

install(TARGETS chirp_frontend
    EXPORT birdsafeTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

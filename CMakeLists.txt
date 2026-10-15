cmake_minimum_required(VERSION 3.20)

project(birdsafe
    VERSION 0.1.0
    DESCRIPTION "BirdSafe - Foundational C11 Runtime Suite"
    LANGUAGES C
)

# Strict C11 Standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Symbol visibility hidden by default
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Global Build Options
option(BIRDSAFE_BUILD_TESTS "Build BirdSafe test suites" ON)
option(BIRDSAFE_ENABLE_WERROR "Treat compiler warnings as errors" ON)

# Standard warning flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes>
        -Wshadow
        $<$<BOOL:${BIRDSAFE_ENABLE_WERROR}>:-Werror>
    )
elseif(MSVC)
    add_compile_options(
        /W4
        $<$<BOOL:${BIRDSAFE_ENABLE_WERROR}>:/WX>
    )
endif()

# Enable testing before adding library subdirectories
if(BIRDSAFE_BUILD_TESTS)
    enable_testing()
endif()

# Standard installation directories and package helpers
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Core libraries
add_subdirectory(libs/core)
add_subdirectory(libs/num)
add_subdirectory(libs/sets)
add_subdirectory(libs/chirp_vm)

# Suite-wide integration tests
if(BIRDSAFE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Installation and package export configuration
install(EXPORT birdsafeTargets
    FILE birdsafeTargets.cmake
    NAMESPACE birdsafe::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/birdsafe
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/birdsafeConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/birdsafeConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/birdsafe"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/birdsafeConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/birdsafeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/birdsafeConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/birdsafe
)

cmake_minimum_required(VERSION 3.20)
project(parishbsd-compartments VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wunused-parameter)
endif()

list(APPEND CMAKE_PREFIX_PATH /usr/local)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================
# Qt GUI executable
# ============================================================

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/ContainerBackend.cpp
    src/ContainerModel.cpp
    src/ContainerInfo.cpp
    src/ActionsDialog.cpp
    src/ApplicationsDialog.cpp
    src/InstallLogDialog.cpp
    src/CatalogTableModel.cpp
    src/PackageTableModel.cpp
)

set(HEADERS
    src/MainWindow.h
    src/ContainerBackend.h
    src/ContainerModel.h
    src/ContainerInfo.h
    src/ActionsDialog.h
    src/ApplicationsDialog.h
    src/InstallLogDialog.h
    src/CatalogTableModel.h
    src/PackageTableModel.h
    src/CustomApp.h
    src/PackageInfo.h
)

add_executable(parish-compartments ${SOURCES} ${HEADERS})

set_target_properties(parish-compartments PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

target_link_libraries(parish-compartments PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)

# ============================================================
# parish-template-build (C++)
# ============================================================

add_executable(parish-template-build
    cli/parish-template-build.cpp
    cli/template_build.cpp
    cli/clusters.cpp
    cli/state_file.cpp
)
target_include_directories(parish-template-build PRIVATE cli)

# ============================================================
# unit test (build-time only; not installed)
# ============================================================

enable_testing()

add_executable(template_build_test
    cli/test/template_build_test.cpp
    cli/template_build.cpp
    cli/clusters.cpp
    cli/state_file.cpp
)
target_include_directories(template_build_test PRIVATE cli)
add_test(NAME template_build_test COMMAND template_build_test)

# ============================================================
# Install (binaries only)
# ============================================================

include(GNUInstallDirs)

install(TARGETS parish-compartments
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS parish-template-build
    RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
cmake_minimum_required(VERSION 3.20)
project(parish-manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	add_compile_options(-Wall -Wextra -Wpedantic -Who-unusued-parameter)
endif()


list(APPEND CMAKE_PREFIX_PATH /usr/local)

find_package(Qt6 REQUIRED COMPONENTS
	Core Gui Widgets Network
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(SOURCES
	src/main.cpp
	src/MainWindow.cpp
	src/ContainerBackend.cpp
	src/ContainerModel.cpp
	src/ContainerInfo.cpp
	src/ActionsDialog.cpp
)

set(HEADERS
	src/MainWindow.h
	src/ContainerBackend.h
	src/ContainerModel.h
	src/ContainerInfo.h
	src/ActionsDialog.h
)

add_executable(parish-manager
	${SOURCES}
	${HEADERS}
)

set_target_properties(parish-manager PROPERTIES
	AUTOMOC ON
	AUTORCC ON
	AUTOUIC ON
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_link_libraries(parish-manager PRIVATE
	Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)

include(GNUInstallDirs)

install(TARGETS parish-manager RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES src/resources/parish-manager.desktop
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES resources/parishbsd.png
	DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps)

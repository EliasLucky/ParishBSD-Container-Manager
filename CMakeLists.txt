cmake_minimum_required(VERSION 3.20)
project(parish-manager VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
	Core Gui Widgets Network
)

qt_standard_project_setup()

set(SOURCES
	src/main.cpp
	src/MainWindow.cpp
)

set(HEADERS
	src/ContainerInfo.h
)

qt_add_executable(parish-manger
	${SOURCES}
	${HEADERS}
	src/resources/parish-manager.qrc
)

target_link_libraries(parish-manager PRIVATE
	Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)

install(TARGETS parish-manager RUNTIME DESTINATION bin)
install(FILES resources/parish-manager.desktop
	DESTINATION share/applications)
install(FILES resources/parishbsd.png
	DESTINATION share/pixmaps)

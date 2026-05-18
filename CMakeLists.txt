cmake_minimum_required(VERSION 3.21)

project(bcr-demo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Network Widgets WebEngineWidgets)

qt_standard_project_setup()

qt_add_executable(bcr-client
    src/bcrserver.cpp
    src/bcrserver.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/clientmain.cpp
)

target_link_libraries(bcr-client
    PRIVATE
        Qt6::Network
        Qt6::WebEngineWidgets
        Qt6::Widgets
)

qt_add_executable(bcr-agent
    src/bcragentservice.cpp
    src/bcragentservice.h
    src/agentmain.cpp
)

target_link_libraries(bcr-agent
    PRIVATE
        Qt6::Core
        Qt6::Network
)

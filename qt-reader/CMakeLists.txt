cmake_minimum_required(VERSION 3.19)
project(BibiQtReader VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core Gui Widgets WebEngineWidgets WebEngineCore Xml Concurrent)

include(FetchContent)
FetchContent_Declare(
    miniz
    GIT_REPOSITORY https://github.com/richgel999/miniz.git
    GIT_TAG        3.0.2
    GIT_SHALLOW    TRUE
)
# MakeAvailable runs miniz's CMakeLists, generating miniz_export.h
FetchContent_MakeAvailable(miniz)

set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/epubreader.cpp
    src/epuburlscheme.cpp
    src/epubwebpage.cpp
    src/bookmarkmanager.cpp
    src/recentfiles.cpp
    src/searchdialog.cpp
    resources.qrc
)

if(APPLE)
    qt_add_executable(BibiQtReader MACOSX_BUNDLE ${SOURCES})
    set_target_properties(BibiQtReader PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.bibi.qtreader"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
elseif(WIN32)
    qt_add_executable(BibiQtReader WIN32 ${SOURCES} app.rc)
else()
    qt_add_executable(BibiQtReader ${SOURCES})
endif()

target_include_directories(BibiQtReader PRIVATE src)

target_link_libraries(BibiQtReader PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::WebEngineCore
    Qt6::Xml
    Qt6::Concurrent
    miniz
)

install(TARGETS BibiQtReader
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

cmake_minimum_required(VERSION 3.21)

project(CARTON VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Concurrent)
find_library(ZSTD_LIBRARY zstd REQUIRED)
find_path(ZSTD_INCLUDE_DIR zstd.h REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

add_library(carton_hic STATIC
    straw.cpp
)
target_include_directories(carton_hic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(carton_hic PRIVATE ${ZSTD_INCLUDE_DIR})
target_link_libraries(carton_hic
    PUBLIC
        CURL::libcurl
        ZLIB::ZLIB
        Threads::Threads
        ${ZSTD_LIBRARY}
)

add_executable(straw main.cpp)
target_link_libraries(straw PRIVATE carton_hic)

qt_add_executable(carton
    src/app_main.cpp
    src/HicDataController.cpp
    src/HicDataController.h
    src/GenomicTrackReader.cpp
    src/GenomicTrackReader.h
    src/HicHeatmapItem.cpp
    src/HicHeatmapItem.h
    src/HicTileCache.h
)

qt_add_qml_module(carton
    URI Carton
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_include_directories(carton PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(carton
    PRIVATE
        carton_hic
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
)

set_target_properties(carton PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.carton.viewer
    MACOSX_BUNDLE_BUNDLE_NAME CARTON
)

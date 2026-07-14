cmake_minimum_required(VERSION 3.21)

project(CARTON VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

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
qt_policy(SET QTP0004 NEW)

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

set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/carton.icns")
set_source_files_properties("${APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

add_library(carton_core STATIC
    src/HicDataController.cpp
    src/HicDataController.h
    src/GenomicTrackReader.cpp
    src/GenomicTrackReader.h
    src/HicTileCache.h
    src/WorkspaceListModel.cpp
    src/WorkspaceListModel.h
)
target_include_directories(carton_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(carton_core
    PUBLIC
        carton_hic
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
)

qt_add_executable(carton
    src/app_main.cpp
    src/HicHeatmapItem.cpp
    src/HicHeatmapItem.h
    "${APP_ICON}"
)

# Singleton metadata must be present before qt_add_qml_module generates the
# qmldir; setting this afterwards leaves Theme unresolved at runtime.
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE true)

qt_add_qml_module(carton
    URI Carton
    VERSION 1.0
    NO_IMPORT_SCAN
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/AppButton.qml
        qml/AppToolButton.qml
        qml/AppComboBox.qml
        qml/AppTextField.qml
        qml/AppCheckBox.qml
        qml/AppTabButton.qml
        qml/Card.qml
        qml/DialogFrame.qml
        qml/SvgIcon.qml
        qml/NavigationPanel.qml
        qml/ComparisonViewport.qml
    RESOURCES
        qml/icons/navigation.svg
        qml/icons/compare.svg
        qml/icons/inspector.svg
)

target_include_directories(carton PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(carton
    PRIVATE
        carton_core
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Concurrent
)

if(BUILD_TESTING)
    add_executable(carton_core_smoke tests/controller_smoke.cpp)
    target_link_libraries(carton_core_smoke PRIVATE carton_core Qt6::Gui)
    add_test(NAME carton_core_smoke COMMAND carton_core_smoke)
    set_tests_properties(carton_core_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

set_target_properties(carton PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER org.carton.viewer
    MACOSX_BUNDLE_BUNDLE_NAME CARTON
    MACOSX_BUNDLE_ICON_FILE carton.icns
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024 CARTON"
)

# ── Install ────────────────────────────────────────────────────────────────────
install(TARGETS carton
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
# ── macOS Qt deployment (runs during `cmake --install`) ──────────────────────
if(APPLE)
    set(DEPLOY_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_macos.cmake")
    file(WRITE "${DEPLOY_SCRIPT}"
        "execute_process(\n"
        "    COMMAND \"/opt/homebrew/bin/macdeployqt\" \"\${CMAKE_INSTALL_PREFIX}/carton.app\""
        " \"-qmldir=${CMAKE_CURRENT_SOURCE_DIR}/qml\"\n"
        "    RESULT_VARIABLE _r\n"
        ")\n"
        "if(NOT _r EQUAL 0)\n"
        "    message(FATAL_ERROR \"macdeployqt failed\")\n"
        "endif()\n"
        # macdeployqt rewrites load commands (rpaths) after the linker's ad-hoc
        # signature was embedded, which invalidates it; re-sign or launch fails
        # with a "Code Signature Invalid" SIGKILL.
        "execute_process(\n"
        "    COMMAND \"codesign\" \"--force\" \"--deep\" \"--sign\" \"-\" \"\${CMAKE_INSTALL_PREFIX}/carton.app\"\n"
        "    RESULT_VARIABLE _rs\n"
        ")\n"
        "if(NOT _rs EQUAL 0)\n"
        "    message(FATAL_ERROR \"codesign failed\")\n"
        "endif()\n"
    )
    install(SCRIPT "${DEPLOY_SCRIPT}")
endif()

# ── CPack DragNDrop DMG ────────────────────────────────────────────────────────
set(CPACK_GENERATOR "DragNDrop")
set(CPACK_PACKAGE_NAME "CARTON")
set(CPACK_PACKAGE_VENDOR "CARTON")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CARTON Hi-C desktop viewer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "CARTON-${PROJECT_VERSION}-macOS")
set(CPACK_DMG_VOLUME_NAME "CARTON")
set(CPACK_DMG_FORMAT "UDZO")
set(CPACK_STRIP_FILES TRUE)
include(CPack)

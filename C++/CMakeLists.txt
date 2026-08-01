cmake_minimum_required(VERSION 3.24)

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

set(IGVCPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../igv-cpp" CACHE PATH
    "Path to an igv-cpp source checkout; leave invalid to use an installed package")
if(NOT TARGET igv::igv)
    if(EXISTS "${IGVCPP_SOURCE_DIR}/CMakeLists.txt")
        set(IGVCPP_BUILD_TESTS OFF CACHE BOOL "Build igv-cpp tests" FORCE)
        set(IGVCPP_BUILD_BENCHMARKS OFF CACHE BOOL "Build igv-cpp benchmarks" FORCE)
        add_subdirectory("${IGVCPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/igv-cpp" EXCLUDE_FROM_ALL)
    else()
        find_package(igv-cpp CONFIG REQUIRED)
    endif()
endif()

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
    src/DatasetRegistry.cpp
    src/DatasetRegistry.h
    src/HicDataController.cpp
    src/HicDataController.h
    src/RegionSetModel.cpp
    src/RegionSetModel.h
    src/TabSession.cpp
    src/TabSession.h
    src/GenomicTrackReader.cpp
    src/GenomicTrackReader.h
    src/HicTileCache.h
    src/WorkspaceListModel.cpp
    src/WorkspaceListModel.h
    src/MatrixAnalysis.cpp
    src/MatrixAnalysis.h
)
target_include_directories(carton_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(carton_core
    PUBLIC
        carton_hic
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
    PRIVATE
        igv::igv
)

qt_add_executable(carton
    src/app_main.cpp
    src/HicHeatmapItem.cpp
    src/HicHeatmapItem.h
    src/AnalysisItems.cpp
    src/AnalysisItems.h
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
        qml/MapViewport.qml
        qml/TabWorkspaceView.qml
        qml/TrackAxisStrip.qml
        qml/Rotated45View.qml
        qml/BullseyeView.qml
        qml/Virtual4CView.qml
        qml/ProcessingView.qml
    RESOURCES
        qml/icons/navigation.svg
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

    add_executable(carton_render_smoke
        tests/render_smoke.cpp
        src/AnalysisItems.cpp
        src/AnalysisItems.h
    )
    target_include_directories(carton_render_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(carton_render_smoke
        PRIVATE carton_core Qt6::Quick Qt6::Concurrent
    )
    target_compile_definitions(carton_render_smoke PRIVATE
        CARTON_TEST_HIC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../igv-main/test/data/hic/test_chr22.hic"
    )
    add_test(NAME carton_render_smoke COMMAND carton_render_smoke)
    set_tests_properties(carton_render_smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software"
        TIMEOUT 20
    )
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

# macdeployqt copies frameworks, plugins, QML modules, and a bundle signature
# into carton.app. Those files are not CMake outputs, so a later incremental
# build can otherwise relink only the executable and leave a mixed bundle: the
# executable loads Homebrew Qt while the stale platform plugin loads the copied
# Qt runtime. Qt detects the duplicate runtime and aborts before main starts.
# Keep the build-tree app development-only; packaging deploys a separate copy.
if(APPLE)
    add_custom_target(carton_clean_stale_deployment
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_BUNDLE_DIR:carton>/Contents/Frameworks"
            "$<TARGET_BUNDLE_DIR:carton>/Contents/PlugIns"
            "$<TARGET_BUNDLE_DIR:carton>/Contents/Resources/qml"
            "$<TARGET_BUNDLE_DIR:carton>/Contents/_CodeSignature"
        COMMENT "Removing stale deployed Qt runtime from development bundle"
        VERBATIM
    )
    add_dependencies(carton carton_clean_stale_deployment)
endif()

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

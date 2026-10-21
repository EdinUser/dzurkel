cmake_minimum_required(VERSION 3.22)
project(Dzurkel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

set(DZURKEL_HIGHLIGHTING "AUTO" CACHE STRING "Syntax highlighting backend: AUTO, ON or OFF")
set_property(CACHE DZURKEL_HIGHLIGHTING PROPERTY STRINGS AUTO ON OFF)
if(NOT DZURKEL_HIGHLIGHTING MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "DZURKEL_HIGHLIGHTING must be AUTO, ON or OFF")
endif()

# Bounded worker-thread decoding with native Qt widgets and PDF rendering.
set(DZURKEL_MIN_QT "6.4")
set(DZURKEL_MIN_LIBZIP "1.7")
find_package(Qt6 ${DZURKEL_MIN_QT} REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Pdf PdfWidgets)
set(DZURKEL_AUDIO "AUTO" CACHE STRING "Audio/video media adapter: AUTO, ON or OFF")
set_property(CACHE DZURKEL_AUDIO PROPERTY STRINGS AUTO ON OFF)
if(NOT DZURKEL_AUDIO MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "DZURKEL_AUDIO must be AUTO, ON or OFF")
endif()
if(DZURKEL_AUDIO STREQUAL "ON")
    find_package(Qt6Multimedia 6.4 REQUIRED)
    find_package(Qt6MultimediaWidgets 6.4 REQUIRED)
elseif(DZURKEL_AUDIO STREQUAL "AUTO")
    find_package(Qt6Multimedia 6.4 QUIET)
    find_package(Qt6MultimediaWidgets 6.4 QUIET)
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip>=${DZURKEL_MIN_LIBZIP})
find_package(Iconv REQUIRED)
if(DZURKEL_HIGHLIGHTING STREQUAL "ON")
    find_package(KF6SyntaxHighlighting 6.0 REQUIRED)
elseif(DZURKEL_HIGHLIGHTING STREQUAL "AUTO")
    find_package(KF6SyntaxHighlighting 6.0 QUIET)
endif()
option(DZURKEL_WITH_KDE_ACTIVATION "Use KDE's public Wayland activation support" ON)
if(DZURKEL_WITH_KDE_ACTIVATION)
    find_package(KF6WindowSystem 6.0 REQUIRED)
endif()
add_library(dzurkel_media_source STATIC src/resources/media/StreamingMediaSource.cpp)
set_target_properties(dzurkel_media_source PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(dzurkel_media_source PUBLIC src)
target_link_libraries(dzurkel_media_source PUBLIC Qt6::Core)
add_library(dzurkel_core STATIC
    src/application/CommandLine.cpp
    src/application/RuntimeCompatibility.cpp
    src/application/InstanceCoordinator.cpp
    src/controller/PreviewController.cpp
    src/desktop/DesktopIntegration.cpp
    src/desktop/WindowPlacement.cpp
    src/desktop/ChooserIntegration.cpp
    src/desktop/FolderSizeBackend.cpp
    src/desktop/DolphinShortcuts.cpp
    src/desktop/DolphinSession.cpp
    src/desktop/NautilusIntegration.cpp
    src/desktop/ThunarIntegration.cpp
    src/ipc/NautilusPreviewService.cpp
    src/ipc/PreviewService.cpp
    src/preview/PreviewRegistry.cpp
    src/preview/PreviewLibrary.cpp
    src/preview/LibraryAvailability.cpp
    src/preview/providers/CsvPreviewProvider.cpp
    src/preview/providers/XlsxPreviewProvider.cpp
    src/preview/providers/DocumentPreviewProvider.cpp
    src/preview/readers/DocumentReader.cpp
    src/preview/readers/RtfReader.cpp
    src/preview/readers/MarkdownReader.cpp
    src/preview/readers/LegacyDocReader.cpp
    src/preview/readers/EbookReader.cpp
    src/preview/document/DocumentView.cpp
    src/preview/readers/XlsxReader.cpp
    src/preview/readers/CsvReader.cpp
    src/preview/readers/CsvRecordParser.cpp
    src/preview/readers/TextDecoder.cpp
    src/preview/readers/TextCodec.cpp
    src/preview/table/SpreadsheetView.cpp
    src/preview/table/TableRowBatch.cpp
    src/preview/providers/ImagePreviewProvider.cpp
    src/preview/providers/PdfPreviewProvider.cpp
    src/preview/image/ImageHelper.cpp
    src/preview/image/ImageDecodeJob.cpp
    src/preview/StructuredDecodeJob.cpp
    src/preview/lazy/LazyCoordinator.cpp
    src/preview/lazy/LazyHelper.cpp
    src/preview/lazy/TextPortionReader.cpp
    src/preview/lazy/TextSource.cpp
    src/preview/lazy/CsvPortionReader.cpp
    src/preview/lazy/LazyTextWindow.cpp
    src/preview/lazy/LazyWindow.cpp
    src/preview/lazy/LazyTableWindow.cpp
    src/preview/document/DocumentHelper.cpp
    src/preview/xlsx/XlsxHelper.cpp
    src/preview/pdf/PdfHelper.cpp
    src/preview/pdf/PdfSession.cpp
    src/preview/pdf/PdfView.cpp
    src/preview/providers/TextPreviewProvider.cpp
    src/preview/text/TextHighlighting.cpp
    src/preview/providers/FileInfoProvider.cpp
    src/preview/providers/AudioPreviewProvider.cpp
    src/preview/providers/VideoPreviewProvider.cpp
    src/preview/media/MediaBackend.cpp
    src/resources/media/MediaSourceHelper.cpp
    src/resources/FolderNavigation.cpp
    src/resources/FileResource.cpp
    src/resources/ResourceAccessPolicy.cpp
    src/resources/ResourcePreparation.cpp
    src/ui/ZoomPreview.h
    src/ui/PreviewWindow.cpp
    src/ui/SettingsDialog.cpp
)
target_include_directories(dzurkel_core PUBLIC src)
target_link_libraries(dzurkel_core PUBLIC Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::PdfWidgets dzurkel_media_source)
target_link_libraries(dzurkel_core PRIVATE PkgConfig::LIBZIP)
target_link_libraries(dzurkel_core PRIVATE Iconv::Iconv)
file(RELATIVE_PATH DZURKEL_COMPATIBILITY_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_DATADIR}/dzurkel/build-compatibility.json")
target_compile_definitions(dzurkel_core PRIVATE
    DZURKEL_COMPATIBILITY_INSTALL_PATH="${DZURKEL_COMPATIBILITY_INSTALL_PATH}"
    DZURKEL_VERSION="${PROJECT_VERSION}"
    DZURKEL_MIN_QT="${DZURKEL_MIN_QT}"
    DZURKEL_MIN_LIBZIP="${DZURKEL_MIN_LIBZIP}")
file(RELATIVE_PATH DZURKEL_HIGHLIGHTING_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/libdzurkel_highlighting.so")
target_compile_definitions(dzurkel_core PRIVATE
    DZURKEL_HIGHLIGHTING_INSTALL_PATH="${DZURKEL_HIGHLIGHTING_INSTALL_PATH}")
if(NOT DZURKEL_HIGHLIGHTING STREQUAL "OFF" AND KF6SyntaxHighlighting_FOUND)
    add_library(dzurkel_highlighting MODULE src/preview/text/KdeTextHighlighting.cpp)
    target_compile_definitions(dzurkel_core PRIVATE DZURKEL_HAS_HIGHLIGHTING_ADAPTER)
    target_include_directories(dzurkel_highlighting PRIVATE src/preview/text)
    target_link_libraries(dzurkel_highlighting PRIVATE Qt6::Widgets KF6::SyntaxHighlighting)
    set_target_properties(dzurkel_highlighting PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-plugins")
    install(TARGETS dzurkel_highlighting LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/plugins)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dzurkel_highlighting PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    message(STATUS "Syntax highlighting: optional runtime adapter enabled (KSyntaxHighlighting >= 6.0)")
else()
    message(STATUS "Syntax highlighting: plain-text fallback (adapter not built)")
endif()
file(RELATIVE_PATH DZURKEL_AUDIO_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/libdzurkel_audio.so")
file(RELATIVE_PATH DZURKEL_DESKTOP_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/libdzurkel_desktop.so")
target_compile_definitions(dzurkel_core PRIVATE
    DZURKEL_AUDIO_INSTALL_PATH="${DZURKEL_AUDIO_INSTALL_PATH}"
    DZURKEL_DESKTOP_INSTALL_PATH="${DZURKEL_DESKTOP_INSTALL_PATH}")
if(NOT DZURKEL_AUDIO STREQUAL "OFF" AND TARGET Qt6::Multimedia AND TARGET Qt6::MultimediaWidgets)
    add_library(dzurkel_audio MODULE src/preview/media/QtMediaBackend.cpp src/preview/media/MediaPreviewWidget.cpp src/preview/media/MediaSession.cpp)
    target_include_directories(dzurkel_audio PRIVATE src)
    target_link_libraries(dzurkel_audio PRIVATE Qt6::Widgets Qt6::MultimediaWidgets dzurkel_media_source)
    set_target_properties(dzurkel_audio PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-plugins")
    install(TARGETS dzurkel_audio LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/plugins)
    message(STATUS "Audio/video: optional runtime media adapter enabled")
else()
    message(STATUS "Audio/video: adapter not built; baseline previews remain available")
endif()
if(DZURKEL_WITH_KDE_ACTIVATION)
    add_library(dzurkel_desktop MODULE src/desktop/KdeDesktopBackend.cpp)
    target_include_directories(dzurkel_desktop PRIVATE src/desktop)
    target_link_libraries(dzurkel_desktop PRIVATE Qt6::Gui KF6::WindowSystem)
    set_target_properties(dzurkel_desktop PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-plugins")
    install(TARGETS dzurkel_desktop LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/plugins)
endif()

# Opt-in KDE chooser integration, isolated from the executable's dependencies.
set(DZURKEL_KDE_CHOOSER "AUTO" CACHE STRING "KDE chooser adapter: AUTO, ON or OFF")
set_property(CACHE DZURKEL_KDE_CHOOSER PROPERTY STRINGS AUTO ON OFF)
if(NOT DZURKEL_KDE_CHOOSER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "DZURKEL_KDE_CHOOSER must be AUTO, ON or OFF")
endif()
file(RELATIVE_PATH DZURKEL_CHOOSER_PLUGIN_ROOT "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/qt-plugins")
target_compile_definitions(dzurkel_core PRIVATE DZURKEL_CHOOSER_PLUGIN_ROOT="${DZURKEL_CHOOSER_PLUGIN_ROOT}")
if(NOT DZURKEL_KDE_CHOOSER STREQUAL "OFF")
    # Only the public headers used by the adapter are needed; avoid importing
    # unused KIO targets and their full SDK dependency graph into baseline builds.
    find_path(DZURKEL_KIO_WIDGETS_INCLUDE KFileWidget PATH_SUFFIXES KF6/KIOFileWidgets)
    find_path(DZURKEL_KIO_CORE_INCLUDE KFileItem PATH_SUFFIXES KF6/KIOCore)
    find_path(DZURKEL_KIO_ENUM_INCLUDE kfile.h PATH_SUFFIXES KF6/KIOWidgets)
    find_path(DZURKEL_KIO_VERSION_INCLUDE kio_version.h PATH_SUFFIXES KF6/KIO)
    find_path(DZURKEL_COREADDONS_INCLUDE KJob PATH_SUFFIXES KF6/KCoreAddons)
    find_library(DZURKEL_KIO_WIDGETS_LIBRARY NAMES KF6KIOFileWidgets libKF6KIOFileWidgets.so.6)
    find_library(DZURKEL_KIO_CORE_LIBRARY NAMES KF6KIOCore libKF6KIOCore.so.6)
    if(DZURKEL_KIO_WIDGETS_INCLUDE AND DZURKEL_KIO_CORE_INCLUDE AND DZURKEL_KIO_ENUM_INCLUDE
        AND DZURKEL_KIO_VERSION_INCLUDE AND DZURKEL_COREADDONS_INCLUDE
        AND DZURKEL_KIO_WIDGETS_LIBRARY AND DZURKEL_KIO_CORE_LIBRARY
        AND TARGET KF6::WindowSystem AND KF6WindowSystem_VERSION VERSION_GREATER_EQUAL 6.19)
        add_library(dzurkel_chooser MODULE src/integrations/kde/KdeChooserPlugin.cpp)
        target_include_directories(dzurkel_chooser SYSTEM PRIVATE ${DZURKEL_KIO_WIDGETS_INCLUDE}
            ${DZURKEL_KIO_CORE_INCLUDE} ${DZURKEL_KIO_ENUM_INCLUDE} ${DZURKEL_KIO_VERSION_INCLUDE} ${DZURKEL_COREADDONS_INCLUDE})
        target_link_libraries(dzurkel_chooser PRIVATE Qt6::Widgets Qt6::DBus KF6::WindowSystem
            ${DZURKEL_KIO_WIDGETS_LIBRARY} ${DZURKEL_KIO_CORE_LIBRARY})
        target_compile_definitions(dzurkel_chooser PRIVATE DZURKEL_CHOOSER_EXECUTABLE="${CMAKE_INSTALL_FULL_BINDIR}/dzurkel")
        set_target_properties(dzurkel_chooser PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-qt-plugins/generic")
        install(TARGETS dzurkel_chooser LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/qt-plugins/generic)
    elseif(DZURKEL_KDE_CHOOSER STREQUAL "ON")
        message(FATAL_ERROR "KDE chooser adapter needs KIO/CoreAddons headers and runtimes, and KF6 WindowSystem >= 6.19")
    else()
        message(STATUS "KDE chooser adapter not built; normal previews remain available")
    endif()
endif()

# Reuse the desktop calculator, with no KIO dependency in the baseline binary.
set(DZURKEL_FOLDER_SIZE "AUTO" CACHE STRING "Desktop folder size: AUTO, ON or OFF")
set_property(CACHE DZURKEL_FOLDER_SIZE PROPERTY STRINGS AUTO ON OFF)
if(NOT DZURKEL_FOLDER_SIZE MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "DZURKEL_FOLDER_SIZE must be AUTO, ON or OFF")
endif()
file(RELATIVE_PATH DZURKEL_FOLDER_SIZE_INSTALL_PATH "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/libdzurkel_folder_size.so")
target_compile_definitions(dzurkel_core PRIVATE DZURKEL_FOLDER_SIZE_INSTALL_PATH="${DZURKEL_FOLDER_SIZE_INSTALL_PATH}")
if(NOT DZURKEL_FOLDER_SIZE STREQUAL "OFF")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GIO QUIET IMPORTED_TARGET gio-2.0>=2.78)
    endif()
    if(TARGET PkgConfig::GIO)
        find_package(Threads REQUIRED)
        add_library(dzurkel_folder_size_gio MODULE src/desktop/GioFolderSizeBackend.cpp)
        target_include_directories(dzurkel_folder_size_gio PRIVATE src/desktop)
        target_link_libraries(dzurkel_folder_size_gio PRIVATE Qt6::Core PkgConfig::GIO Threads::Threads)
        set_target_properties(dzurkel_folder_size_gio PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-plugins")
        install(TARGETS dzurkel_folder_size_gio LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/plugins)
    endif()
    find_path(DZURKEL_KIO_VERSION_INCLUDE kio_version.h PATH_SUFFIXES KF6/KIO)
    find_path(DZURKEL_KIO_CORE_INCLUDE KFileItem PATH_SUFFIXES KF6/KIOCore)
    find_path(DZURKEL_COREADDONS_INCLUDE KJob PATH_SUFFIXES KF6/KCoreAddons)
    find_library(DZURKEL_KIO_CORE_LIBRARY NAMES KF6KIOCore libKF6KIOCore.so.6)
    find_library(DZURKEL_COREADDONS_LIBRARY NAMES KF6CoreAddons libKF6CoreAddons.so.6)
    if(DZURKEL_KIO_VERSION_INCLUDE)
        file(STRINGS "${DZURKEL_KIO_VERSION_INCLUDE}/kio_version.h" folder_kio_version REGEX "^#define KIO_VERSION_STRING ")
        string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" folder_kio_version "${folder_kio_version}")
    endif()
    # Older DirectorySizeJob totals include directory metadata, not contents only.
    if(DZURKEL_KIO_CORE_INCLUDE AND DZURKEL_COREADDONS_INCLUDE AND DZURKEL_KIO_VERSION_INCLUDE
        AND DZURKEL_KIO_CORE_LIBRARY AND DZURKEL_COREADDONS_LIBRARY AND folder_kio_version VERSION_GREATER_EQUAL 6.30)
        add_library(dzurkel_folder_size MODULE src/desktop/KdeFolderSizeBackend.cpp)
        target_include_directories(dzurkel_folder_size PRIVATE src/desktop)
        target_include_directories(dzurkel_folder_size SYSTEM PRIVATE
            ${DZURKEL_KIO_CORE_INCLUDE} ${DZURKEL_COREADDONS_INCLUDE} ${DZURKEL_KIO_VERSION_INCLUDE})
        target_link_libraries(dzurkel_folder_size PRIVATE Qt6::Core
            ${DZURKEL_KIO_CORE_LIBRARY} ${DZURKEL_COREADDONS_LIBRARY})
        set_target_properties(dzurkel_folder_size PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-plugins")
        install(TARGETS dzurkel_folder_size LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/dzurkel/plugins)
    elseif(DZURKEL_FOLDER_SIZE STREQUAL "ON" AND NOT TARGET dzurkel_folder_size_gio)
        message(FATAL_ERROR "Folder size needs GIO >= 2.78 or KIOCore/CoreAddons >= 6.30 headers and libraries")
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dzurkel_core PUBLIC -Wall -Wextra -Wpedantic)
    target_compile_options(dzurkel_media_source PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(dzurkel src/main.cpp src/resources/icons/icons.qrc)
set_target_properties(dzurkel PROPERTIES AUTORCC ON)
target_link_libraries(dzurkel PRIVATE dzurkel_core)
target_compile_definitions(dzurkel PRIVATE DZURKEL_VERSION="${PROJECT_VERSION}")
foreach(adapter dzurkel_highlighting dzurkel_audio dzurkel_desktop dzurkel_chooser dzurkel_folder_size dzurkel_folder_size_gio)
    if(TARGET ${adapter})
        add_dependencies(dzurkel ${adapter})
        set_property(TARGET dzurkel APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:${adapter}>)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${adapter} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()

# Compatibility metadata belongs to the release artifact, never to app startup.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(NOT CMAKE_READELF)
    find_program(CMAKE_READELF readelf REQUIRED)
endif()
set(compatibility_adapter_args)
foreach(adapter dzurkel_highlighting dzurkel_audio dzurkel_desktop dzurkel_chooser dzurkel_folder_size dzurkel_folder_size_gio)
    if(TARGET ${adapter})
        list(APPEND compatibility_adapter_args --adapter $<TARGET_FILE:${adapter}>)
    endif()
endforeach()
set(compatibility_report "${CMAKE_CURRENT_BINARY_DIR}/build-compatibility.json")
set(compatibility_script "${CMAKE_CURRENT_SOURCE_DIR}/packaging/compatibility/report.py")
set(compatibility_catalog "${CMAKE_CURRENT_SOURCE_DIR}/packaging/compatibility/platforms.json")
set_property(TARGET dzurkel APPEND PROPERTY LINK_DEPENDS "${compatibility_script}" "${compatibility_catalog}")
add_custom_command(TARGET dzurkel POST_BUILD
    COMMAND ${Python3_EXECUTABLE} "${compatibility_script}"
        --catalog "${compatibility_catalog}" --binary $<TARGET_FILE:dzurkel>
        --readelf "${CMAKE_READELF}" --output "${compatibility_report}"
        --release "${PROJECT_VERSION}" --baseline-qt "${DZURKEL_MIN_QT}"
        --baseline-zip "${DZURKEL_MIN_LIBZIP}" --built-qt "${Qt6Core_VERSION}"
        --built-pdf "${Qt6Pdf_VERSION}" ${compatibility_adapter_args}
    BYPRODUCTS "${compatibility_report}" VERBATIM)
install(FILES "${compatibility_report}" DESTINATION ${CMAKE_INSTALL_DATADIR}/dzurkel)

option(DZURKEL_ENABLE_IPO "Enable link-time optimization for optimized app builds" OFF)
option(DZURKEL_GC_SECTIONS "Remove unused application code/data sections on Linux" OFF)
if(DZURKEL_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES CXX)
    if(NOT ipo_supported)
        message(FATAL_ERROR "Requested link-time optimization is unavailable: ${ipo_error}")
    endif()
    set_target_properties(dzurkel_core dzurkel PROPERTIES
        INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
        INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
        INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()
if(DZURKEL_GC_SECTIONS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Section removal requires the supported Linux GCC/Clang toolchain")
    endif()
    target_compile_options(dzurkel_core PRIVATE -ffunction-sections -fdata-sections)
    target_compile_options(dzurkel PRIVATE -ffunction-sections -fdata-sections)
    target_link_options(dzurkel PRIVATE -Wl,--gc-sections)
endif()

# Escape the executable for both desktop-entry string and Exec parsing rules.
set(DZURKEL_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/dzurkel")
string(REPLACE "\\" "\\\\\\\\" DZURKEL_EXEC "${DZURKEL_EXEC}")
string(REPLACE "\"" "\\\\\"" DZURKEL_EXEC "${DZURKEL_EXEC}")
string(REPLACE "$" "\\\\$" DZURKEL_EXEC "${DZURKEL_EXEC}")
string(REPLACE "`" "\\\\`" DZURKEL_EXEC "${DZURKEL_EXEC}")
string(REPLACE "%" "%%" DZURKEL_EXEC "${DZURKEL_EXEC}")
configure_file(integrations/dolphin/dzurkel.desktop.in dzurkel.desktop @ONLY)
configure_file(integrations/desktop/dzurkel.desktop.in dzurkel-application.desktop @ONLY)
configure_file(integrations/desktop/dzurkel-settings.desktop.in dzurkel-settings.desktop @ONLY)
install(TARGETS dzurkel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Executable permission authorizes a user-local KDE service menu.
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/dzurkel.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-application.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications RENAME dzurkel.desktop)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-settings.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY src/resources/icons/hicolor/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
    FILES_MATCHING PATTERN "*.png" PATTERN "*.svg")

option(DZURKEL_BUILD_BENCHMARKS "Build the isolated-session latency and stability tool" OFF)
if(DZURKEL_BUILD_BENCHMARKS)
    add_executable(dzurkel_benchmark tests/performance/PreviewBenchmark.cpp)
    target_link_libraries(dzurkel_benchmark PRIVATE Qt6::Gui Qt6::DBus)
    target_compile_definitions(dzurkel_benchmark PRIVATE
        DZURKEL_EXECUTABLE="$<TARGET_FILE:dzurkel>" DZURKEL_VERSION="${PROJECT_VERSION}")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dzurkel_benchmark PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_dependencies(dzurkel_benchmark dzurkel)
endif()

option(DZURKEL_BUILD_MEDIA_EXPERIMENT "Build the uninstalled helper-backed media experiment" OFF)
if(DZURKEL_BUILD_MEDIA_EXPERIMENT OR BUILD_TESTING)
    add_executable(dzurkel_media_source_helper tests/experiments/MediaSourceHelper.cpp)
    target_link_libraries(dzurkel_media_source_helper PRIVATE dzurkel_core dzurkel_media_source)
endif()
if(DZURKEL_BUILD_MEDIA_EXPERIMENT)
    find_package(Qt6Multimedia 6.4 REQUIRED)
    find_package(Qt6MultimediaWidgets 6.4 REQUIRED)
    add_executable(dzurkel_video_experiment tests/experiments/VideoExperiment.cpp)
    target_link_libraries(dzurkel_video_experiment PRIVATE dzurkel_media_source Qt6::MultimediaWidgets)
    target_compile_definitions(dzurkel_video_experiment PRIVATE
        DZURKEL_MEDIA_HELPER="$<TARGET_FILE:dzurkel_media_source_helper>")
    add_dependencies(dzurkel_video_experiment dzurkel_media_source_helper)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dzurkel_video_experiment PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_executable(dzurkel_media_experiment tests/experiments/MediaExperiment.cpp)
    target_link_libraries(dzurkel_media_experiment PRIVATE dzurkel_media_source Qt6::Multimedia)
    target_compile_definitions(dzurkel_media_experiment PRIVATE
        DZURKEL_MEDIA_HELPER="$<TARGET_FILE:dzurkel_media_source_helper>")
    add_dependencies(dzurkel_media_experiment dzurkel_media_source_helper)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dzurkel_media_experiment PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(BUILD_TESTING)
        find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)
        find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
        add_executable(dzurkel_media_playback_tests tests/integration/MediaPlaybackTest.cpp)
        target_link_libraries(dzurkel_media_playback_tests PRIVATE dzurkel_media_source Qt6::Multimedia Qt6::Test)
        target_compile_definitions(dzurkel_media_playback_tests PRIVATE
            DZURKEL_MEDIA_HELPER="$<TARGET_FILE:dzurkel_media_source_helper>")
        add_dependencies(dzurkel_media_playback_tests dzurkel_media_source_helper)
        add_test(NAME dzurkel_media_playback COMMAND dzurkel_media_playback_tests)
        set_tests_properties(dzurkel_media_playback PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_MEDIA_BACKEND=ffmpeg")
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(dzurkel_folder_size_tests tests/integration/FolderSizeTest.cpp)
    target_link_libraries(dzurkel_folder_size_tests PRIVATE dzurkel_core Qt6::Test)
    add_dependencies(dzurkel_folder_size_tests dzurkel)
    if(TARGET dzurkel_folder_size OR TARGET dzurkel_folder_size_gio)
        target_compile_definitions(dzurkel_folder_size_tests PRIVATE DZURKEL_TEST_FOLDER_SIZE)
    endif()
    if(TARGET dzurkel_folder_size_gio)
        target_compile_definitions(dzurkel_folder_size_tests PRIVATE DZURKEL_TEST_GIO_FOLDER_SIZE)
        add_test(NAME dzurkel_folder_size_gio COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_folder_size_tests>)
        set_tests_properties(dzurkel_folder_size_gio PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CURRENT_DESKTOP=GNOME")
    endif()
    foreach(folder_adapter dzurkel_folder_size dzurkel_folder_size_gio)
        if(TARGET ${folder_adapter})
            add_test(NAME ${folder_adapter}_fallback COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/FolderSizeFallbackTest.py
                $<TARGET_FILE:dzurkel_folder_size_tests> $<TARGET_FILE:${folder_adapter}>)
            set_tests_properties(${folder_adapter}_fallback PROPERTIES TIMEOUT 60)
        endif()
    endforeach()
    add_test(NAME dzurkel_folder_size COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_folder_size_tests>)
    if(TARGET dzurkel_folder_size)
        set(folder_test_desktop KDE)
    else()
        set(folder_test_desktop GNOME)
    endif()
    set_tests_properties(dzurkel_folder_size PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;XDG_CURRENT_DESKTOP=${folder_test_desktop}")
    if(TARGET dzurkel_chooser)
        add_executable(dzurkel_chooser_smoke tests/support/QtChooserSmoke.cpp)
        target_link_libraries(dzurkel_chooser_smoke PRIVATE Qt6::Widgets Qt6::Test)
        add_test(NAME dzurkel_chooser_fallback COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/KdeChooserFallbackTest.py
            $<TARGET_FILE:dzurkel_chooser_smoke> $<TARGET_FILE:dzurkel_chooser>)
        set_tests_properties(dzurkel_chooser_fallback PROPERTIES TIMEOUT 30)
        add_library(dzurkel_chooser_testdriver MODULE tests/support/KdeChooserDriver.cpp)
        target_include_directories(dzurkel_chooser_testdriver SYSTEM PRIVATE ${DZURKEL_KIO_WIDGETS_INCLUDE}
            ${DZURKEL_KIO_CORE_INCLUDE} ${DZURKEL_KIO_ENUM_INCLUDE} ${DZURKEL_KIO_VERSION_INCLUDE} ${DZURKEL_COREADDONS_INCLUDE})
        target_link_libraries(dzurkel_chooser_testdriver PRIVATE Qt6::Widgets Qt6::Test ${DZURKEL_KIO_WIDGETS_LIBRARY} ${DZURKEL_KIO_CORE_LIBRARY})
        set_target_properties(dzurkel_chooser_testdriver PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-qt-plugins/generic")
        add_executable(dzurkel_chooser_tests tests/integration/KdeChooserTest.cpp)
        target_include_directories(dzurkel_chooser_tests SYSTEM PRIVATE ${DZURKEL_KIO_WIDGETS_INCLUDE}
            ${DZURKEL_KIO_CORE_INCLUDE} ${DZURKEL_KIO_ENUM_INCLUDE} ${DZURKEL_KIO_VERSION_INCLUDE} ${DZURKEL_COREADDONS_INCLUDE})
        target_link_libraries(dzurkel_chooser_tests PRIVATE dzurkel_core Qt6::Test ${DZURKEL_KIO_WIDGETS_LIBRARY} ${DZURKEL_KIO_CORE_LIBRARY})
        target_compile_definitions(dzurkel_chooser_tests PRIVATE DZURKEL_EXECUTABLE="$<TARGET_FILE:dzurkel>"
            CHOOSER_PLUGIN_ROOT="${CMAKE_CURRENT_BINARY_DIR}/dzurkel-qt-plugins")
        add_dependencies(dzurkel_chooser_tests dzurkel)
        add_test(NAME dzurkel_chooser COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_chooser_tests>)
        set_tests_properties(dzurkel_chooser PROPERTIES TIMEOUT 60 ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${CMAKE_CURRENT_BINARY_DIR}/dzurkel-qt-plugins;QT_QPA_GENERIC_PLUGINS=dzurkelchooser:$<TARGET_FILE:dzurkel>")
    endif()
    add_executable(dzurkel_highlighting_tests tests/unit/TextHighlightingTest.cpp)
    target_link_libraries(dzurkel_highlighting_tests PRIVATE dzurkel_core Qt6::Test)
    if(TARGET dzurkel_highlighting)
        add_dependencies(dzurkel_highlighting_tests dzurkel_highlighting)
        target_compile_definitions(dzurkel_highlighting_tests PRIVATE DZURKEL_TEST_HIGHLIGHTING)
    endif()
    add_test(NAME dzurkel_highlighting COMMAND dzurkel_highlighting_tests)
    set_tests_properties(dzurkel_highlighting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    if(TARGET dzurkel_audio)
        add_executable(dzurkel_video_tests tests/integration/VideoTest.cpp)
        target_link_libraries(dzurkel_video_tests PRIVATE dzurkel_core Qt6::Test Qt6::MultimediaWidgets)
        add_dependencies(dzurkel_video_tests dzurkel)
        add_test(NAME dzurkel_video COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_video_tests>)
        set_tests_properties(dzurkel_video PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=ffmpeg" TIMEOUT 180)
        add_executable(dzurkel_audio_tests tests/integration/AudioTest.cpp)
        target_link_libraries(dzurkel_audio_tests PRIVATE dzurkel_core Qt6::Test Qt6::Multimedia)
        add_dependencies(dzurkel_audio_tests dzurkel)
        add_test(NAME dzurkel_audio COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_audio_tests>)
        set_tests_properties(dzurkel_audio PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    endif()
    add_executable(dzurkel_optional_readers_tests tests/integration/OptionalReadersTest.cpp)
    target_link_libraries(dzurkel_optional_readers_tests PRIVATE dzurkel_core Qt6::Test)
    add_dependencies(dzurkel_optional_readers_tests dzurkel)
    add_test(NAME dzurkel_optional_readers COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_optional_readers_tests>)
    set_tests_properties(dzurkel_optional_readers PROPERTIES TIMEOUT 45
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DZURKEL_TEST_AUDIO_AVAILABLE=$<IF:$<TARGET_EXISTS:dzurkel_audio>,1,0>")
    if(TARGET dzurkel_audio)
        add_test(NAME dzurkel_audio_fallback COMMAND ${DBUS_RUN_SESSION} -- ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/AudioFallbackTest.py
            $<TARGET_FILE:dzurkel_optional_readers_tests> $<TARGET_FILE:dzurkel> $<TARGET_FILE:dzurkel_audio>)
        set_tests_properties(dzurkel_audio_fallback PROPERTIES TIMEOUT 180)
    endif()
    add_test(NAME dzurkel_compatibility COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/CompatibilityReportTest.py)
    add_executable(dzurkel_media_source_tests tests/integration/MediaSourceTest.cpp)
    target_link_libraries(dzurkel_media_source_tests PRIVATE dzurkel_media_source Qt6::Test Qt6::Concurrent Qt6::DBus)
    target_compile_definitions(dzurkel_media_source_tests PRIVATE
        DZURKEL_MEDIA_HELPER="$<TARGET_FILE:dzurkel_media_source_helper>")
    add_dependencies(dzurkel_media_source_tests dzurkel_media_source_helper)
    add_test(NAME dzurkel_media_source COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_media_source_tests>)
    set_tests_properties(dzurkel_media_source PROPERTIES TIMEOUT 45)
    add_executable(dzurkel_helper_shutdown_probe tests/support/HelperShutdownProbe.cpp)
    target_link_libraries(dzurkel_helper_shutdown_probe PRIVATE dzurkel_core Qt6::Test)
    add_library(dzurkel_test_suppress_kill SHARED tests/support/SuppressHelperKill.cpp)
    target_link_libraries(dzurkel_test_suppress_kill PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME dzurkel_helper_shutdown COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/HelperShutdownTest.py
        $<TARGET_FILE:dzurkel_helper_shutdown_probe> $<TARGET_FILE:dzurkel_test_suppress_kill>)
    set_tests_properties(dzurkel_helper_shutdown PROPERTIES TIMEOUT 120)
    add_executable(dzurkel_thunar_tests tests/integration/ThunarTest.cpp)
    target_link_libraries(dzurkel_thunar_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_thunar COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_thunar_tests>)
    set_tests_properties(dzurkel_thunar PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_executable(dzurkel_nautilus_adapter_tests tests/integration/NautilusTest.cpp)
    target_link_libraries(dzurkel_nautilus_adapter_tests PRIVATE dzurkel_core Qt6::Test)
    target_compile_definitions(dzurkel_nautilus_adapter_tests PRIVATE DZURKEL_EXECUTABLE="$<TARGET_FILE:dzurkel>")
    add_dependencies(dzurkel_nautilus_adapter_tests dzurkel)
    add_test(NAME dzurkel_nautilus_adapter COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_nautilus_adapter_tests>)
    set_tests_properties(dzurkel_nautilus_adapter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_tests tests/unit/DzurkelTest.cpp)
    add_dependencies(dzurkel_tests dzurkel)
    target_link_libraries(dzurkel_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_tests COMMAND dzurkel_tests)
    set_tests_properties(dzurkel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(dzurkel_preview_tests tests/unit/PreviewTest.cpp)
    add_dependencies(dzurkel_preview_tests dzurkel)
    target_link_libraries(dzurkel_preview_tests PRIVATE dzurkel_core Qt6::Test)
    target_compile_definitions(dzurkel_preview_tests PRIVATE DZURKEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME dzurkel_previews COMMAND dzurkel_preview_tests)
    set_tests_properties(dzurkel_previews PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_pdf_tests tests/integration/PdfTest.cpp)
    add_executable(dzurkel_lazy_tests tests/integration/LazyTest.cpp)
    add_executable(dzurkel_csv_lazy_tests tests/integration/CsvLazyTest.cpp)
    add_dependencies(dzurkel_csv_lazy_tests dzurkel)
    target_link_libraries(dzurkel_csv_lazy_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_csv_lazy COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_csv_lazy_tests>)
    set_tests_properties(dzurkel_csv_lazy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_dependencies(dzurkel_lazy_tests dzurkel)
    target_link_libraries(dzurkel_lazy_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_lazy COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_lazy_tests>)
    set_tests_properties(dzurkel_lazy PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_dependencies(dzurkel_pdf_tests dzurkel)
    target_link_libraries(dzurkel_pdf_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_pdf COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_pdf_tests>)
    set_tests_properties(dzurkel_pdf PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
    add_executable(dzurkel_image_tests tests/integration/ImageTest.cpp)
    add_dependencies(dzurkel_image_tests dzurkel)
    target_link_libraries(dzurkel_image_tests PRIVATE dzurkel_core Qt6::Test)
    target_compile_definitions(dzurkel_image_tests PRIVATE DZURKEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME dzurkel_images COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_image_tests>)
    set_tests_properties(dzurkel_images PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
    add_test(NAME dzurkel_gnome_backend_guard COMMAND ${CMAKE_COMMAND}
        -DPREVIEW_TESTS=$<TARGET_FILE:dzurkel_preview_tests>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/GnomeGuard.cmake)
    add_executable(dzurkel_dbus_tests tests/integration/DBusTest.cpp)
    target_link_libraries(dzurkel_dbus_tests PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    target_compile_definitions(dzurkel_dbus_tests PRIVATE DZURKEL_EXECUTABLE="$<TARGET_FILE:dzurkel>")
    add_dependencies(dzurkel_dbus_tests dzurkel)
    add_test(NAME dzurkel_dbus COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_dbus_tests>)
    set_tests_properties(dzurkel_dbus PROPERTIES TIMEOUT 60)
    add_executable(dzurkel_settings_tests tests/integration/SettingsTest.cpp)
    add_dependencies(dzurkel_settings_tests dzurkel)
    target_link_libraries(dzurkel_settings_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_settings COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_settings_tests>)
    set_tests_properties(dzurkel_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_resource_tests tests/integration/ResourceTest.cpp)
    target_link_libraries(dzurkel_resource_tests PRIVATE dzurkel_core Qt6::Test)
    add_dependencies(dzurkel_resource_tests dzurkel)
    add_test(NAME dzurkel_resources COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_resource_tests>)
    set_tests_properties(dzurkel_resources PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_interaction_tests tests/integration/InteractionTest.cpp)
    target_link_libraries(dzurkel_interaction_tests PRIVATE dzurkel_core Qt6::Test)
    target_compile_definitions(dzurkel_interaction_tests PRIVATE DZURKEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_dependencies(dzurkel_interaction_tests dzurkel)
    add_test(NAME dzurkel_interactions COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_interaction_tests>)
    set_tests_properties(dzurkel_interactions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_library_tests tests/integration/LibraryTest.cpp)
    target_link_libraries(dzurkel_library_tests PRIVATE dzurkel_core Qt6::Test)
    add_dependencies(dzurkel_library_tests dzurkel)
    add_test(NAME dzurkel_library COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_library_tests>)
    set_tests_properties(dzurkel_library PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_xlsx_tests tests/integration/XlsxTest.cpp)
    add_executable(dzurkel_document_tests tests/integration/DocumentTest.cpp)
    target_link_libraries(dzurkel_document_tests PRIVATE dzurkel_core Qt6::Test PkgConfig::LIBZIP)
    add_dependencies(dzurkel_document_tests dzurkel)
    add_test(NAME dzurkel_documents COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_document_tests>)
    set_tests_properties(dzurkel_documents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    target_link_libraries(dzurkel_xlsx_tests PRIVATE dzurkel_core Qt6::Test PkgConfig::LIBZIP)
    add_dependencies(dzurkel_xlsx_tests dzurkel)
    add_test(NAME dzurkel_xlsx COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_xlsx_tests>)
    set_tests_properties(dzurkel_xlsx PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_test(NAME dzurkel_cli COMMAND ${CMAKE_COMMAND}
        -DDZURKEL=$<TARGET_FILE:dzurkel>
        -DEXPECTED_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CommandLine.cmake)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(TARGET dzurkel_highlighting)
        add_test(NAME dzurkel_highlighting_fallback COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/HighlightingFallbackTest.py
            $<TARGET_FILE:dzurkel_highlighting_tests> $<TARGET_FILE:dzurkel_highlighting>
            ${DZURKEL_HIGHLIGHTING_INSTALL_PATH})
        set_tests_properties(dzurkel_highlighting_fallback PROPERTIES TIMEOUT 110)
    endif()
    add_test(NAME dzurkel_gnome_frame_assertions COMMAND ${Python3_EXECUTABLE} -B
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/GnomeFrameTest.py)
    option(DZURKEL_BUILD_GNOME_TESTS "Build the isolated native GNOME desktop regression gate" OFF)
    if(DZURKEL_BUILD_GNOME_TESTS)
        add_executable(dzurkel_gnome_probe tests/integration/GnomeTestProbe.cpp)
        target_link_libraries(dzurkel_gnome_probe PRIVATE dzurkel_core)
        add_dependencies(dzurkel_gnome_probe dzurkel dzurkel_preview_tests)
        add_test(NAME dzurkel_gnome_native COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/GnomeNativeTest.py
            --binary $<TARGET_FILE:dzurkel> --probe $<TARGET_FILE:dzurkel_gnome_probe>
            --preview-tests $<TARGET_FILE:dzurkel_preview_tests>
            --report-dir ${CMAKE_CURRENT_BINARY_DIR}/gnome-native-artifacts)
        set_tests_properties(dzurkel_gnome_native PROPERTIES TIMEOUT 180 LABELS "gnome-native")
    endif()
    add_test(NAME dzurkel_nautilus COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/NautilusTest.py)
    set_tests_properties(dzurkel_nautilus PROPERTIES TIMEOUT 30)
    add_test(NAME dzurkel_install COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/InstallTest.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cmake ${CMAKE_COMMAND}
        --bindir ${CMAKE_INSTALL_FULL_BINDIR}
        --datadir ${CMAKE_INSTALL_FULL_DATADIR}
        --version ${PROJECT_VERSION})
    if(TARGET dzurkel_highlighting)
        set_property(TEST dzurkel_install APPEND PROPERTY ENVIRONMENT
            "DZURKEL_TEST_HIGHLIGHTING_PLUGIN=${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/libdzurkel_highlighting.so")
    endif()
    foreach(adapter dzurkel_audio dzurkel_desktop dzurkel_folder_size dzurkel_folder_size_gio)
        if(TARGET ${adapter})
            set_property(TEST dzurkel_install APPEND PROPERTY ENVIRONMENT
                "DZURKEL_TEST_${adapter}=${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/plugins/lib${adapter}.so")
        endif()
    endforeach()
    foreach(test_target dzurkel_settings_tests dzurkel_thunar_tests dzurkel_preview_tests dzurkel_highlighting_tests)
        foreach(adapter dzurkel_highlighting dzurkel_audio dzurkel_desktop)
            if(TARGET ${adapter})
                add_dependencies(${test_target} ${adapter})
            endif()
        endforeach()
    endforeach()
    if(TARGET dzurkel_chooser)
        set_property(TEST dzurkel_install APPEND PROPERTY ENVIRONMENT
            "DZURKEL_TEST_CHOOSER_PLUGIN=${CMAKE_INSTALL_FULL_LIBDIR}/dzurkel/qt-plugins/generic/libdzurkel_chooser.so")
    endif()
    set_tests_properties(dzurkel_install PROPERTIES TIMEOUT 60)
endif()

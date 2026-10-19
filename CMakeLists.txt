cmake_minimum_required(VERSION 3.22)
project(Dzurkel VERSION 0.7.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

# Bounded worker-thread decoding with native Qt widgets and PDF rendering.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Pdf PdfWidgets Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET libzip>=1.7)
find_package(Iconv REQUIRED)
option(DZURKEL_WITH_KDE_ACTIVATION "Use KDE's public Wayland activation support" ON)
if(DZURKEL_WITH_KDE_ACTIVATION)
    find_package(KF6WindowSystem 6.0 REQUIRED)
endif()
add_library(dzurkel_media_source STATIC src/resources/media/StreamingMediaSource.cpp)
target_include_directories(dzurkel_media_source PUBLIC src)
target_link_libraries(dzurkel_media_source PUBLIC Qt6::Core)
add_library(dzurkel_core STATIC
    src/application/CommandLine.cpp
    src/application/InstanceCoordinator.cpp
    src/controller/PreviewController.cpp
    src/desktop/DesktopIntegration.cpp
    src/desktop/WindowPlacement.cpp
    src/desktop/DolphinShortcuts.cpp
    src/desktop/DolphinSession.cpp
    src/desktop/NautilusIntegration.cpp
    src/desktop/ThunarIntegration.cpp
    src/ipc/NautilusPreviewService.cpp
    src/ipc/PreviewService.cpp
    src/preview/PreviewRegistry.cpp
    src/preview/PreviewLibrary.cpp
    src/preview/providers/CsvPreviewProvider.cpp
    src/preview/providers/XlsxPreviewProvider.cpp
    src/preview/providers/DocumentPreviewProvider.cpp
    src/preview/readers/DocumentReader.cpp
    src/preview/readers/RtfReader.cpp
    src/preview/readers/LegacyDocReader.cpp
    src/preview/readers/EbookReader.cpp
    src/preview/document/DocumentView.cpp
    src/preview/readers/XlsxReader.cpp
    src/preview/readers/CsvReader.cpp
    src/preview/readers/TextDecoder.cpp
    src/preview/table/SpreadsheetView.cpp
    src/preview/providers/ImagePreviewProvider.cpp
    src/preview/providers/PdfPreviewProvider.cpp
    src/preview/providers/TextPreviewProvider.cpp
    src/preview/providers/FileInfoProvider.cpp
    src/preview/providers/AudioPreviewProvider.cpp
    src/preview/media/AudioPreviewWidget.cpp
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
target_link_libraries(dzurkel_core PUBLIC Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::PdfWidgets Qt6::Multimedia dzurkel_media_source)
target_link_libraries(dzurkel_core PRIVATE PkgConfig::LIBZIP)
target_link_libraries(dzurkel_core PRIVATE Iconv::Iconv)
if(DZURKEL_WITH_KDE_ACTIVATION)
    target_link_libraries(dzurkel_core PRIVATE KF6::WindowSystem)
    target_compile_definitions(dzurkel_core PRIVATE DZURKEL_KDE_ACTIVATION)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dzurkel_core PUBLIC -Wall -Wextra -Wpedantic)
    target_compile_options(dzurkel_media_source PUBLIC -Wall -Wextra -Wpedantic)
endif()

add_executable(dzurkel src/main.cpp)
target_link_libraries(dzurkel PRIVATE dzurkel_core)
target_compile_definitions(dzurkel PRIVATE DZURKEL_VERSION="${PROJECT_VERSION}")

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
    add_executable(dzurkel_audio_tests tests/integration/AudioTest.cpp)
    target_link_libraries(dzurkel_audio_tests PRIVATE dzurkel_core Qt6::Test)
    add_dependencies(dzurkel_audio_tests dzurkel)
    add_test(NAME dzurkel_audio COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_audio_tests>)
    set_tests_properties(dzurkel_audio PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(dzurkel_media_source_tests tests/integration/MediaSourceTest.cpp)
    target_link_libraries(dzurkel_media_source_tests PRIVATE dzurkel_media_source Qt6::Test Qt6::Concurrent Qt6::DBus)
    target_compile_definitions(dzurkel_media_source_tests PRIVATE
        DZURKEL_MEDIA_HELPER="$<TARGET_FILE:dzurkel_media_source_helper>")
    add_dependencies(dzurkel_media_source_tests dzurkel_media_source_helper)
    add_test(NAME dzurkel_media_source COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_media_source_tests>)
    set_tests_properties(dzurkel_media_source PROPERTIES TIMEOUT 45)
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
    set_tests_properties(dzurkel_install PROPERTIES TIMEOUT 60)
endif()

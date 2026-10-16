cmake_minimum_required(VERSION 3.22)
project(Dzurkel VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)
include(CTest)

# Bounded worker-thread decoding with native Qt widgets and PDF rendering.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Pdf PdfWidgets)
option(DZURKEL_WITH_KDE_ACTIVATION "Use KDE's public Wayland activation support" ON)
if(DZURKEL_WITH_KDE_ACTIVATION)
    find_package(KF6WindowSystem 6.0 REQUIRED)
endif()
add_library(dzurkel_core STATIC
    src/application/CommandLine.cpp
    src/application/InstanceCoordinator.cpp
    src/controller/PreviewController.cpp
    src/desktop/DesktopIntegration.cpp
    src/ipc/PreviewService.cpp
    src/preview/PreviewRegistry.cpp
    src/preview/providers/ImagePreviewProvider.cpp
    src/preview/providers/PdfPreviewProvider.cpp
    src/preview/providers/TextPreviewProvider.cpp
    src/preview/providers/FileInfoProvider.cpp
    src/resources/FileResource.cpp
    src/ui/PreviewWindow.cpp
)
target_include_directories(dzurkel_core PUBLIC src)
target_link_libraries(dzurkel_core PUBLIC Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::PdfWidgets)
if(DZURKEL_WITH_KDE_ACTIVATION)
    target_link_libraries(dzurkel_core PRIVATE KF6::WindowSystem)
    target_compile_definitions(dzurkel_core PRIVATE DZURKEL_KDE_ACTIVATION)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dzurkel_core PUBLIC -Wall -Wextra -Wpedantic)
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
install(TARGETS dzurkel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Executable permission authorizes a user-local KDE service menu.
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/dzurkel.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/kio/servicemenus)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/dzurkel-application.desktop"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications RENAME dzurkel.desktop)

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

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    find_program(DBUS_RUN_SESSION dbus-run-session REQUIRED)
    add_executable(dzurkel_tests tests/unit/DzurkelTest.cpp)
    target_link_libraries(dzurkel_tests PRIVATE dzurkel_core Qt6::Test)
    add_test(NAME dzurkel_tests COMMAND dzurkel_tests)
    set_tests_properties(dzurkel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(dzurkel_preview_tests tests/unit/PreviewTest.cpp)
    target_link_libraries(dzurkel_preview_tests PRIVATE dzurkel_core Qt6::Test)
    target_compile_definitions(dzurkel_preview_tests PRIVATE DZURKEL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME dzurkel_previews COMMAND dzurkel_preview_tests)
    set_tests_properties(dzurkel_previews PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(dzurkel_dbus_tests tests/integration/DBusTest.cpp)
    target_link_libraries(dzurkel_dbus_tests PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
    target_compile_definitions(dzurkel_dbus_tests PRIVATE DZURKEL_EXECUTABLE="$<TARGET_FILE:dzurkel>")
    add_dependencies(dzurkel_dbus_tests dzurkel)
    add_test(NAME dzurkel_dbus COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:dzurkel_dbus_tests>)
    set_tests_properties(dzurkel_dbus PROPERTIES TIMEOUT 60)
    add_test(NAME dzurkel_cli COMMAND ${CMAKE_COMMAND}
        -DDZURKEL=$<TARGET_FILE:dzurkel>
        -DEXPECTED_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/CommandLine.cmake)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME dzurkel_install COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/InstallTest.py
        --build-dir ${CMAKE_CURRENT_BINARY_DIR}
        --cmake ${CMAKE_COMMAND}
        --bindir ${CMAKE_INSTALL_FULL_BINDIR}
        --datadir ${CMAKE_INSTALL_FULL_DATADIR}
        --version ${PROJECT_VERSION})
    set_tests_properties(dzurkel_install PROPERTIES TIMEOUT 60)
endif()

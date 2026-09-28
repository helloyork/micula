cmake_minimum_required(VERSION 3.20)

# The version is written down once, in the header, and read back out of it here. The header owns
# it because the library is header-only: somebody who fetches include/micula and never runs CMake
# still gets the numbers, which a generated header would not give them. See include/micula/version.h.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/micula/version.h" _micula_version_lines
     REGEX "^#define MICULA_VERSION_(MAJOR|MINOR|PATCH) ")
set(_micula_version_parts "")
foreach(_part MAJOR MINOR PATCH)
    # No ^ anchor: file(STRINGS) hands back a list, which reaches string() as one string joined
    # with semicolons, so only the first line would ever be at the start of it.
    string(REGEX MATCH "#define MICULA_VERSION_${_part} ([0-9]+)" _match "${_micula_version_lines}")
    if(NOT _match)
        message(FATAL_ERROR "include/micula/version.h does not define MICULA_VERSION_${_part}")
    endif()
    list(APPEND _micula_version_parts "${CMAKE_MATCH_1}")
endforeach()
list(JOIN _micula_version_parts "." _micula_version)
unset(_match)
unset(_part)

project(micula VERSION ${_micula_version} DESCRIPTION "A small Fluent control set for plain Win32" LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Micula is a Windows library: it draws with Direct2D over DirectComposition.")
endif()

# Header-only. The target carries the include path, the language level and the system
# libraries; there is nothing to compile until a program includes it.
add_library(micula INTERFACE)
add_library(micula::micula ALIAS micula)

target_include_directories(micula INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(micula INTERFACE cxx_std_17)
# window.h calls the wide API throughout, so a consumer has to compile with UNICODE as
# well: without it its own <windows.h> still answers with the A variants, and whatever
# it hands in -- a class name, a font name -- is a byte string these wide calls read as
# UTF-16. On the library rather than on the examples, because the header *is* the
# interface.
target_compile_definitions(micula INTERFACE UNICODE _UNICODE)
# `WS_EX_NOREDIRECTIONBITMAP` sits behind `WINVER >= 0x0602` in some SDKs -- MinGW-w64's,
# whose default is lower, is one -- and this window is Windows 10+ by construction. Said
# out loud rather than inherited, so the same source builds under more than one toolchain.
target_compile_definitions(micula INTERFACE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
# MSVC gets these from window.h's #pragma comment lines as well; other toolchains need them
# spelled out. UIAutomationCore is deliberately not here: the four functions used out of it are
# looked up at run time (see the UIA section of window.h), because MinGW-w64 ships no import
# library for it, and a load-time dependency on an export is what frameclock::Resolve exists to
# avoid in the first place.
target_link_libraries(micula INTERFACE
    d2d1 d3d11 dxgi dcomp dwrite dwmapi imm32 windowscodecs user32 gdi32 ole32 oleaut32 advapi32)

option(MICULA_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
if(MICULA_BUILD_EXAMPLES)
    foreach(example gallery settings nav)
        add_executable(micula-${example} WIN32 examples/${example}/${example}.cpp)
        target_link_libraries(micula-${example} PRIVATE micula::micula)
        # The entry point is wWinMain, which MSVC's linker finds on its own from the
        # subsystem and MinGW's does not: without -municode the startup code looks for
        # WinMain and the link fails on the one symbol the program never mentions.
        if(MINGW)
            target_link_options(micula-${example} PRIVATE -municode)
        endif()
        if(MSVC)
            target_compile_options(micula-${example} PRIVATE /W4 /permissive-)
        endif()
    endforeach()
endif()

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
install(DIRECTORY include/micula DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS micula EXPORT micula-targets)
install(EXPORT micula-targets FILE micula-config.cmake NAMESPACE micula::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/micula)
# Without this file `find_package(micula 0.2)` is a request nothing can answer: it is what turns
# the version into a check. SameMinorVersion, because this is 0.x -- a minor version may break,
# so a request for one must not be answered with another.
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/micula-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/micula-config-version.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/micula)

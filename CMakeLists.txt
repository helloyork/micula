cmake_minimum_required(VERSION 3.20)

# The version is written down once, in the header, and read back out of it here. The header owns
# it because the library is header-only: somebody who fetches include/micula and never runs CMake
# still gets the numbers, which a generated header would not give them. See include/micula/version.h.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/micula/version.h" _micula_version_lines
     REGEX "^#define MICULA_VERSION_(MAJOR|MINOR|PATCH) ")
# Reading the version is the one thing here that is not re-done by a build: the numbers are baked
# into micula-config-version.cmake at *configure* time, which is what find_package answers with, so
# a bump followed by only a build installs a package that still says the previous version. This is
# what makes editing version.h re-run CMake, which is the only other thing that would.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS include/micula/version.h)
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
# Draw what the layouts worked out, over the pages in the examples: the rectangle each widget was
# arranged into, the rectangle a widget is gliding toward while it is not there yet, the box its own
# layout places children in, and the page's clip. Off by default -- it is a thing to look at while a
# layout is being written rather than a thing to ship -- and a build that has it can still turn it
# off at run time with `micula::debug::layout = false`, without a rebuild.
option(MICULA_DEBUG_LAYOUT "Build the examples with the layout debug overlay" OFF)
if(MICULA_BUILD_EXAMPLES)
    # gallery is the only example ported to the tree so far. It is a *console* program rather than a
    # WIN32 one because of its `--dump`: the rectangles a layout came out as are printed instead of
    # drawn, and that needs somewhere to print. It drops the console it was given when it was
    # launched from Explorer and keeps the one it was launched from a terminal with.
    #
    # examples/settings and examples/nav are still written against the flat widget list this branch
    # replaced -- each builds its controls by hand and places them itself -- so they are not in the
    # build. They are kept as the reference for the controls still to be ported, and `git show
    # master:examples/nav/nav.cpp` is where they are if this tree outgrows them.
    add_executable(micula-gallery examples/gallery/gallery.cpp)
    target_link_libraries(micula-gallery PRIVATE micula::micula)
    if(MICULA_DEBUG_LAYOUT)
        target_compile_definitions(micula-gallery PRIVATE MICULA_DEBUG_LAYOUT=1)
    endif()
    # The entry point is wmain, which MSVC's linker finds on its own from the subsystem (and from
    # the startup code, for a console program) and MinGW's does not: without -municode it looks for
    # WinMain and the link fails on the one symbol the program never mentions.
    if(MINGW)
        target_link_options(micula-gallery PRIVATE -municode)
    endif()
    if(MSVC)
        # /utf-8 because the example's own strings are Chinese and MSVC reads a source file in the
        # system code page otherwise: on a machine set to 936 that turns every literal in it into
        # mojibake, and the compiler then reports the string as an unterminated one. On the example
        # rather than on the library, whose headers stay ASCII for exactly this reason -- see the
        # note over glyphs.h.
        target_compile_options(micula-gallery PRIVATE /W4 /permissive- /utf-8)
    endif()
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

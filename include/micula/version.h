// Micula / version.h
//
// The version, written down once. `CMakeLists.txt` reads these numbers out of this file and
// hands them to `project()`, so a bump happens here and nowhere else -- and it is the header
// that owns them rather than the build file, because this library is header-only: somebody
// who fetches `include/micula` and never runs CMake still gets the numbers. A generated
// header would give that person nothing.
//
// What reads it:
//
//   - a program, at compile time, through `MICULA_VERSION` or the three numbers below. A
//     build that needs a newer library can say so and fail with a sentence rather than with
//     a missing symbol.
//   - CMake at configure time, for `project(... VERSION ...)`, which is what makes
//     `find_package(micula 0.2)` a request that can be answered at all -- the package
//     version file is written from it.
//
// **This is 0.x**: a minor version may break, and the package version file therefore asks for
// the same minor rather than the same major. Bump the minor for anything a consumer can
// notice, the patch for a fix that changes nothing they depend on.

#pragma once

#define MICULA_VERSION_MAJOR 0
#define MICULA_VERSION_MINOR 9
#define MICULA_VERSION_PATCH 1

// Built from the numbers rather than written out a second time: a string that says 0.1.0
// beside numbers that say 0.2.0 is the kind of thing only ever noticed by a user.
#define MICULA_STRINGIFY_(x) #x
#define MICULA_STRINGIFY(x) MICULA_STRINGIFY_(x)
#define MICULA_VERSION_STRING \
    MICULA_STRINGIFY(MICULA_VERSION_MAJOR) "." MICULA_STRINGIFY(MICULA_VERSION_MINOR) "." \
    MICULA_STRINGIFY(MICULA_VERSION_PATCH)

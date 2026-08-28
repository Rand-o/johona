cmake_minimum_required(VERSION 3.22)
project(johona
    VERSION 1.0.0
    DESCRIPTION "Johona Wallpaper - time-of-day desktop wallpaper changer"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(JOHONA_BUILD_TESTS "Build unit tests" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui DBus Network Widgets Test Concurrent Svg)

# ---------------------------------------------------------------------------
# libzip (vendored, built statically)
#
# The org.kde.Sdk / org.freedesktop.Platform runtimes do not ship libzip,
# so we build the vendored source tree into a static library.  This keeps
# the development build and the Flatpak build identical.
# ---------------------------------------------------------------------------
set(LIBZIP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third-party/libzip-1.11.4")
if(NOT EXISTS "${LIBZIP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "libzip source not found at ${LIBZIP_SOURCE_DIR}. "
        "Run: tar xJf third-party/libzip-1.11.4.tar.xz -C third-party/")
endif()

# libzip needs zlib for deflate entries; the Qt SDK bundles it.
find_package(ZLIB REQUIRED)

# Johona only *reads* zip archives (theme import), so disable every
# optional codec/crypto backend and all non-library targets.  (zlib is
# always required by libzip for deflate entries.)
set(ENABLE_COMMONCRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_GNUTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_MBEDTLS OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENSSL OFF CACHE BOOL "" FORCE)
set(ENABLE_WINDOWS_CRYPTO OFF CACHE BOOL "" FORCE)
set(ENABLE_BZIP2 OFF CACHE BOOL "" FORCE)
set(ENABLE_LZMA OFF CACHE BOOL "" FORCE)
set(ENABLE_ZSTD OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
set(BUILD_OSSFUZZ OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LIBZIP_DO_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third-party/libzip-1.11.4 EXCLUDE_FROM_ALL)

add_subdirectory(src/core)
add_subdirectory(src/gui)

if(JOHONA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Data files
# ---------------------------------------------------------------------------
set(JOHONA_APP_ID "top.spelunk.johona")
install(FILES data/${JOHONA_APP_ID}.desktop
        DESTINATION share/applications)
install(FILES data/${JOHONA_APP_ID}.metainfo.xml
        DESTINATION share/metainfo)
install(FILES data/${JOHONA_APP_ID}.autostart.desktop
        RENAME ${JOHONA_APP_ID}.desktop
        DESTINATION share/autostart)

# The tz → coordinates table (location fallback lookup, spec §10).
install(FILES resources/tz_coordinates.json DESTINATION share/johona)

# Icons (hicolor theme layout: hicolor/<size|scalable>/apps/...).
file(GLOB_RECURSE JOHONA_ICONS data/icons/hicolor/*)
foreach(icon ${JOHONA_ICONS})
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_SOURCE_DIR}/data/icons" "${icon}")
    get_filename_component(dest "share/icons/${rel}" NAME)
    get_filename_component(parent "share/icons/${rel}" DIRECTORY)
    install(FILES "${icon}" DESTINATION "${parent}")
endforeach()

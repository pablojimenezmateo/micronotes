cmake_minimum_required(VERSION 3.28)

project(micronotes VERSION 0.1.2 LANGUAGES CXX C)

if(NOT UNIX OR APPLE)
  message(FATAL_ERROR "micronotes is Linux-only for this product scope.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GNUInstallDirs)

enable_testing()

option(MICRONOTES_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(MICRONOTES_ENABLE_SDL3_IMAGE "Enable SDL3_image when available" ON)
option(MICRONOTES_ENABLE_SDL3_TTF "Enable SDL3_ttf when available" ON)
option(MICRONOTES_PERF_HARNESS_BUILD "Build performance harness" ON)
option(MICRONOTES_ASSERT_OFFLINE_BUILD "Fail if generated dependency-fetch hooks are present" ON)

find_package(PkgConfig QUIET)

find_package(SDL3 CONFIG QUIET)
set(MICRONOTES_SDL3_TARGET "")
if(TARGET SDL3::SDL3)
  set(MICRONOTES_SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-shared)
  set(MICRONOTES_SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3-static)
  set(MICRONOTES_SDL3_TARGET SDL3::SDL3-static)
elseif(PkgConfig_FOUND)
  pkg_check_modules(SDL3 QUIET IMPORTED_TARGET sdl3)
  if(TARGET PkgConfig::SDL3)
    set(MICRONOTES_SDL3_TARGET PkgConfig::SDL3)
  endif()
endif()
if(MICRONOTES_SDL3_TARGET STREQUAL "")
  message(FATAL_ERROR "SDL3 development files were not found. Install SDL3 and rerun CMake.")
endif()

set(MICRONOTES_SQLITE_TARGET "")
find_package(SQLite3 QUIET)
if(TARGET SQLite::SQLite3)
  set(MICRONOTES_SQLITE_TARGET SQLite::SQLite3)
elseif(PkgConfig_FOUND)
  pkg_check_modules(SQLITE3 QUIET IMPORTED_TARGET sqlite3)
  if(TARGET PkgConfig::SQLITE3)
    set(MICRONOTES_SQLITE_TARGET PkgConfig::SQLITE3)
  endif()
endif()
if(MICRONOTES_SQLITE_TARGET STREQUAL "")
  find_path(MICRONOTES_SQLITE_INCLUDE_DIR sqlite3.h)
  find_library(MICRONOTES_SQLITE_LIBRARY sqlite3)
  if(MICRONOTES_SQLITE_INCLUDE_DIR AND MICRONOTES_SQLITE_LIBRARY)
    add_library(micronotes_sqlite INTERFACE)
    target_include_directories(micronotes_sqlite INTERFACE "${MICRONOTES_SQLITE_INCLUDE_DIR}")
    target_link_libraries(micronotes_sqlite INTERFACE "${MICRONOTES_SQLITE_LIBRARY}")
    set(MICRONOTES_SQLITE_TARGET micronotes_sqlite)
  else()
    message(FATAL_ERROR "SQLite3 development files were not found. Install sqlite3 headers and rerun CMake.")
  endif()
endif()

set(MICRONOTES_SDL3_IMAGE_TARGET "")
if(MICRONOTES_ENABLE_SDL3_IMAGE)
  find_package(SDL3_image CONFIG QUIET)
  if(TARGET SDL3_image::SDL3_image)
    set(MICRONOTES_SDL3_IMAGE_TARGET SDL3_image::SDL3_image)
  elseif(PkgConfig_FOUND)
    pkg_check_modules(SDL3_IMAGE QUIET IMPORTED_TARGET SDL3_image)
    if(TARGET PkgConfig::SDL3_IMAGE)
      set(MICRONOTES_SDL3_IMAGE_TARGET PkgConfig::SDL3_IMAGE)
    endif()
  endif()
endif()

set(MICRONOTES_SDL3_TTF_TARGET "")
if(MICRONOTES_ENABLE_SDL3_TTF)
  find_package(SDL3_ttf CONFIG QUIET)
  if(TARGET SDL3_ttf::SDL3_ttf)
    set(MICRONOTES_SDL3_TTF_TARGET SDL3_ttf::SDL3_ttf)
  elseif(PkgConfig_FOUND)
    pkg_check_modules(SDL3_TTF QUIET IMPORTED_TARGET SDL3_ttf)
    if(TARGET PkgConfig::SDL3_TTF)
      set(MICRONOTES_SDL3_TTF_TARGET PkgConfig::SDL3_TTF)
    endif()
  endif()
endif()

if(MICRONOTES_ASSERT_OFFLINE_BUILD)
  file(GLOB_RECURSE MICRONOTES_FETCH_MARKERS
    "${CMAKE_SOURCE_DIR}/cmake/*.cmake"
  )
  set(MICRONOTES_FETCH_TOKEN_A "Fetch")
  set(MICRONOTES_FETCH_TOKEN_B "Content")
  set(MICRONOTES_EXTERNAL_TOKEN_A "External")
  set(MICRONOTES_EXTERNAL_TOKEN_B "Project_Add")
  set(MICRONOTES_URL_TOKEN "://")
  foreach(marker_file IN LISTS MICRONOTES_FETCH_MARKERS)
    file(READ "${marker_file}" marker_contents)
    if(marker_contents MATCHES "${MICRONOTES_FETCH_TOKEN_A}${MICRONOTES_FETCH_TOKEN_B}|${MICRONOTES_EXTERNAL_TOKEN_A}${MICRONOTES_EXTERNAL_TOKEN_B}|git[ ]+clone|${MICRONOTES_URL_TOKEN}")
      message(FATAL_ERROR "Offline build invariant failed: ${marker_file} contains dependency-fetch syntax.")
    endif()
  endforeach()
endif()

add_library(md4c STATIC third_party/md4c/src/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c/src)
add_library(md4c_html STATIC third_party/md4c/src/md4c-html.c third_party/md4c/src/entity.c)
target_include_directories(md4c_html PUBLIC third_party/md4c/src)
target_link_libraries(md4c_html PUBLIC md4c)

set(MICRONOTES_CORE_SOURCES
  src/attachments/AttachmentService.cpp
  src/editor/MarkdownEditor.cpp
  src/editor/SoftWrap.cpp
  src/library/Library.cpp
  src/library/LibraryIndex.cpp
  src/library/Metadata.cpp
  src/library/Organization.cpp
  src/markdown/MarkdownParser.cpp
  src/markdown/RenderModel.cpp
  src/perf/Perf.cpp
  src/platform/DefaultOpener.cpp
  src/platform/DurableFile.cpp
  src/platform/PathUtils.cpp
  src/ui/AppState.cpp
)

add_library(micronotes_core STATIC ${MICRONOTES_CORE_SOURCES})
target_include_directories(micronotes_core PUBLIC src)
target_link_libraries(micronotes_core PUBLIC md4c md4c_html ${MICRONOTES_SQLITE_TARGET} ${MICRONOTES_SDL3_TARGET})
target_compile_definitions(micronotes_core PUBLIC
  MICRONOTES_HAS_SDL3_IMAGE=$<BOOL:${MICRONOTES_SDL3_IMAGE_TARGET}>
  MICRONOTES_HAS_SDL3_TTF=$<BOOL:${MICRONOTES_SDL3_TTF_TARGET}>
)

add_executable(micronotes src/app/main.cpp src/app/Application.cpp)
target_include_directories(micronotes PRIVATE src)
target_link_libraries(micronotes PRIVATE micronotes_core ${MICRONOTES_SDL3_TARGET})
if(NOT MICRONOTES_SDL3_IMAGE_TARGET STREQUAL "")
  target_link_libraries(micronotes PRIVATE ${MICRONOTES_SDL3_IMAGE_TARGET})
endif()
if(NOT MICRONOTES_SDL3_TTF_TARGET STREQUAL "")
  target_link_libraries(micronotes PRIVATE ${MICRONOTES_SDL3_TTF_TARGET})
endif()

add_executable(micronotes_tests
  tests/TestMain.cpp
  tests/AppStateTests.cpp
  tests/AttachmentTests.cpp
  tests/EditorViewerTests.cpp
  tests/LibraryTests.cpp
  tests/MarkdownParserTests.cpp
  tests/PathUtilsTests.cpp
  tests/SearchTests.cpp
)
target_include_directories(micronotes_tests PRIVATE src tests)
target_compile_definitions(micronotes_tests PRIVATE MICRONOTES_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(micronotes_tests PRIVATE micronotes_core)
add_test(NAME micronotes_tests COMMAND micronotes_tests)

if(MICRONOTES_PERF_HARNESS_BUILD)
  add_executable(micronotes_perf tools/PerfMain.cpp)
  target_include_directories(micronotes_perf PRIVATE src)
  target_link_libraries(micronotes_perf PRIVATE micronotes_core)
endif()

foreach(target micronotes_core micronotes micronotes_tests)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(MICRONOTES_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endforeach()

set(MICRONOTES_DESKTOP_FILE "${CMAKE_CURRENT_BINARY_DIR}/micronotes.desktop")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/packaging/micronotes.desktop.in"
  "${MICRONOTES_DESKTOP_FILE}"
  @ONLY
)

install(TARGETS micronotes RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${MICRONOTES_DESKTOP_FILE}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/micronotes.svg" DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${PROJECT_NAME}")
set(CPACK_PACKAGE_CONTACT "micronotes")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "micronotes - a small Linux Markdown notes app")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

include(CPack)

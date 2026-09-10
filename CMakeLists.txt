cmake_minimum_required(VERSION 3.28)

project(micronotes VERSION 0.3.0 LANGUAGES CXX C)

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

# The recovery writer runs on a thread of its own, so that a durable write --
# two fsync barriers, ~9 ms -- never happens on the thread drawing the window.
find_package(Threads REQUIRED)

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

# fontconfig resolves the faces the user actually configured, the same way every
# other app on the desktop does. Optional: without it FontResolver falls back to
# scanning the standard font directories.
set(MICRONOTES_FONTCONFIG_TARGET "")
if(PkgConfig_FOUND)
  pkg_check_modules(FONTCONFIG QUIET IMPORTED_TARGET fontconfig)
  if(TARGET PkgConfig::FONTCONFIG)
    set(MICRONOTES_FONTCONFIG_TARGET PkgConfig::FONTCONFIG)
  endif()
endif()
if(MICRONOTES_FONTCONFIG_TARGET STREQUAL "")
  message(STATUS "fontconfig not found; font resolution falls back to a directory scan")
endif()

add_library(md4c STATIC third_party/md4c/src/md4c.c)
target_include_directories(md4c PUBLIC third_party/md4c/src)
add_library(md4c_html STATIC third_party/md4c/src/md4c-html.c third_party/md4c/src/entity.c)
target_include_directories(md4c_html PUBLIC third_party/md4c/src)
target_link_libraries(md4c_html PUBLIC md4c)

# The app-agnostic core: markdown, editing, viewing, persistence, perf and the
# platform helpers. It names no application -- `ArchitectureTests` enforces that
# -- and reaches the app's identity through the one macro below, which is what
# keeps it a separable layer rather than another folder of app code.
set(MICROCORE_SOURCES
  src/core/attachments/AttachmentService.cpp
  src/core/editor/MarkdownEditor.cpp
  src/core/editor/SingleLineEditor.cpp
  src/core/editor/SingleLineView.cpp
  src/core/editor/SoftWrap.cpp
  src/core/editor/TextField.cpp
  src/core/editor/WrappedLines.cpp
  src/core/markdown/BareUrl.cpp
  src/core/markdown/MarkdownParser.cpp
  src/core/markdown/RenderModel.cpp
  src/core/perf/Perf.cpp
  src/core/perf/PerformanceCounters.cpp
  src/core/perf/TraceChannel.cpp
  src/core/render/ColorMath.cpp
  src/core/render/FontResolver.cpp
  src/core/render/TextTextureCache.cpp
  src/core/persistence/SqliteDb.cpp
  src/core/platform/DefaultOpener.cpp
  src/core/platform/DirectoryWatcher.cpp
  src/core/platform/DurableFile.cpp
  src/core/platform/PathUtils.cpp
  src/core/util/Fuzzy.cpp
  src/core/util/StringUtil.cpp
  src/core/util/TextSearch.cpp
  src/core/util/Utf8.cpp
)

# micronotes-only code: the live-Markdown document model, the note library,
# and the app shell state.
set(MICRONOTES_APP_SOURCES
  src/app/EditCommands.cpp
  src/app/EditorBlocks.cpp
  src/app/FramePolicy.cpp
  src/app/FrameTrace.cpp
  src/doc/BlockScan.cpp
  src/doc/Edits.cpp
  src/doc/Fold.cpp
  src/doc/InlineScan.cpp
  src/doc/LinkTarget.cpp
  src/doc/WikiLink.cpp
  src/doc/Layout.cpp
  src/doc/LayoutUpdate.cpp
  src/doc/LayoutQueries.cpp
  src/library/Library.cpp
  src/library/LibraryIndex.cpp
  src/library/LibraryIndexSearch.cpp
  src/library/Metadata.cpp
  src/library/NoteCatalog.cpp
  src/library/Organization.cpp
  src/library/RecoveryStore.cpp
  src/library/SearchScope.cpp
  src/library/Trash.cpp
  src/library/WikiResolve.cpp
  src/ui/Actions.cpp
  src/ui/AppState.cpp
  src/ui/OpenNoteRecord.cpp
  src/ui/FoldState.cpp
  src/ui/CaretBlink.cpp
  src/ui/NoteProperties.cpp
  src/ui/Menus.cpp
  src/ui/Outline.cpp
  src/ui/ScrollList.cpp
  src/ui/Settings.cpp
  src/ui/SettingsSurface.cpp
  src/ui/ShellLayout.cpp
  src/ui/TagColors.cpp
  src/ui/Tabs.cpp
  src/ui/UiStateFile.cpp
  src/ui/Tooltip.cpp
  src/ui/Theme.cpp
  src/ui/WorkspaceModel.cpp
  src/ui/TreeModel.cpp
)

set(MICRONOTES_CORE_SOURCES ${MICROCORE_SOURCES} ${MICRONOTES_APP_SOURCES})

add_library(micronotes_core STATIC ${MICRONOTES_CORE_SOURCES})
target_include_directories(micronotes_core PUBLIC src)
target_link_libraries(micronotes_core PUBLIC md4c md4c_html ${MICRONOTES_SQLITE_TARGET} ${MICRONOTES_SDL3_TARGET} Threads::Threads)
if(NOT MICRONOTES_FONTCONFIG_TARGET STREQUAL "")
  target_link_libraries(micronotes_core PUBLIC ${MICRONOTES_FONTCONFIG_TARGET})
endif()
target_compile_definitions(micronotes_core PUBLIC
  MICROCORE_HAS_FONTCONFIG=$<BOOL:${MICRONOTES_FONTCONFIG_TARGET}>
  MICROCORE_APP_NAME="micronotes"
  # What the About page prints. From the project version rather than a string in
  # a source file, so the packaged version and the reported one cannot differ.
  MICRONOTES_VERSION="${PROJECT_VERSION}"
  MICRONOTES_HAS_SDL3_IMAGE=$<BOOL:${MICRONOTES_SDL3_IMAGE_TARGET}>
  MICRONOTES_HAS_SDL3_TTF=$<BOOL:${MICRONOTES_SDL3_TTF_TARGET}>
)

# The shell: every surface that draws, plus the fonts, the draw helpers and the
# overlay stack they draw through. A library rather than a pile of sources
# inside the executable so that the test binary can link it too -- panes,
# models and policies under src/app/ used to be reachable only from `main`,
# which is why the sidebar's row search had to be pushed into src/ui/ to be
# testable at all.
set(MICRONOTES_SHELL_SOURCES
  src/app/Application.cpp
  src/app/AppWindow.cpp
  src/app/Autosave.cpp
  src/app/CaretPolicy.cpp
  src/app/CursorTheme.cpp
  src/app/BlockMenus.cpp
  src/app/Breadcrumb.cpp
  src/app/Chrome.cpp
  src/app/Clipboard.cpp
  src/app/Commands.cpp
  src/app/ContextMenus.cpp
  src/app/Cursor.cpp
  src/app/Desktop.cpp
  src/app/Dismiss.cpp
  src/app/Fields.cpp
  src/app/FindBar.cpp
  src/app/FocusedEdits.cpp
  src/app/Folds.cpp
  src/app/EventRouter.cpp
  src/app/Frame.cpp
  src/app/InlineText.cpp
  src/app/KeyRouter.cpp
  src/app/Layout.cpp
  src/app/KeySurfaces.cpp
  src/app/LivePage.cpp
  src/app/MarkdownBlocks.cpp
  src/app/MenuBar.cpp
  src/app/NoteCaches.cpp
  src/app/Notes.cpp
  src/app/Options.cpp
  src/app/OverlayRouter.cpp
  src/app/PageChrome.cpp
  src/app/PageHeader.cpp
  src/app/PageImages.cpp
  src/app/PagePress.cpp
  src/app/PageView.cpp
  src/app/PageViewPaint.cpp
  src/app/PageViewStyle.cpp
  src/app/PointerRouter.cpp
  src/app/Prompts.cpp
  src/app/RawPane.cpp
  src/app/PageSurface.cpp
  src/app/ReadingPage.cpp
  src/app/RightPanel.cpp
  src/app/Screenshot.cpp
  src/app/Scroll.cpp
  src/app/SessionState.cpp
  src/app/SettingsInput.cpp
  src/app/SettingsPane.cpp
  src/app/Sidebar.cpp
  src/app/SidebarInput.cpp
  src/app/SidebarModel.cpp
  src/app/Startup.cpp
  src/app/StatusBar.cpp
  src/app/TabStrip.cpp
  src/app/WikiLinks.cpp
  src/app/WindowChrome.cpp
  src/ui/Fonts.cpp
  src/ui/Glyphs.cpp
  src/ui/ImageCache.cpp
  src/ui/Overlay.cpp
  src/ui/OverlayPaint.cpp
  src/ui/Painter.cpp
  src/ui/Scrollbar.cpp
  src/ui/TextFit.cpp
  src/ui/TextMeasureCache.cpp
  src/ui/TextRenderer.cpp
  src/ui/Widgets.cpp
)

add_library(micronotes_shell STATIC ${MICRONOTES_SHELL_SOURCES})
target_include_directories(micronotes_shell PUBLIC src)
# Lets a build-tree run find the vendored faces without an install step.
target_compile_definitions(micronotes_shell PUBLIC
  MICRONOTES_SOURCE_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts"
)
target_link_libraries(micronotes_shell PUBLIC micronotes_core ${MICRONOTES_SDL3_TARGET})
if(NOT MICRONOTES_SDL3_IMAGE_TARGET STREQUAL "")
  target_link_libraries(micronotes_shell PUBLIC ${MICRONOTES_SDL3_IMAGE_TARGET})
endif()
if(NOT MICRONOTES_SDL3_TTF_TARGET STREQUAL "")
  target_link_libraries(micronotes_shell PUBLIC ${MICRONOTES_SDL3_TTF_TARGET})
endif()

add_executable(micronotes src/app/main.cpp)
target_link_libraries(micronotes PRIVATE micronotes_shell)

add_executable(micronotes_tests
  tests/TestMain.cpp
  tests/TooltipTests.cpp
  tests/TreeModelTests.cpp
  tests/ActionsTests.cpp
  tests/AppStateTests.cpp
  tests/RecoveryStoreTests.cpp
  tests/ArchitectureTests.cpp
  tests/AttachmentTests.cpp
  tests/BlockScanTests.cpp
  tests/ClipGuardTests.cpp
  tests/CaretBlinkTests.cpp
  tests/ColorMathTests.cpp
  tests/CursorThemeTests.cpp
  tests/DirectoryWatcherTests.cpp
  tests/DurableFileTests.cpp
  tests/EditorSoftWrapTests.cpp
  tests/EditorTests.cpp
  tests/EditorUndoTests.cpp
  tests/EditsTests.cpp
  tests/FlowTests.cpp
  tests/FocusedEditsTests.cpp
  tests/FoldTests.cpp
  tests/TokenizeTests.cpp
  tests/FramePolicyTests.cpp
  tests/FrameTraceTests.cpp
  tests/FuzzyTests.cpp
  tests/InlineScanTests.cpp
  tests/LayoutBlockTests.cpp
  tests/LayoutQueryTests.cpp
  tests/LayoutUpdateTests.cpp
  tests/LibraryTests.cpp
  tests/MarkdownParserTests.cpp
  tests/NotePropertiesTests.cpp
  tests/OutlineTests.cpp
  tests/OverlayTests.cpp
  tests/PathUtilsTests.cpp
  tests/PerfTraceTests.cpp
  tests/MenusTests.cpp
  tests/SearchTests.cpp
  tests/LibraryIndexTests.cpp
  tests/BacklinkTests.cpp
  tests/OrganizationTests.cpp
  tests/TextSearchTests.cpp
  tests/FindBarTests.cpp
  tests/StatusBarTests.cpp
  tests/StringUtilTests.cpp
  tests/SettingsTests.cpp
  tests/SettingsSurfaceTests.cpp
  tests/ShellLayoutTests.cpp
  tests/ShellLinkTests.cpp
  tests/ShellPageTests.cpp
  tests/ShellSessionTests.cpp
  tests/ShellSidebarTests.cpp
  tests/SidebarRowsMemoTests.cpp
  tests/SidebarRowsTests.cpp
  tests/SingleLineEditorTests.cpp
  tests/TabStripTests.cpp
  tests/TabsTests.cpp
  tests/TextFitTests.cpp
  tests/TagColorTests.cpp
  tests/ThemeTests.cpp
  tests/TextFieldTests.cpp
  tests/RowCursorTests.cpp
  tests/ScrollListTests.cpp
  tests/WikiLinkTests.cpp
  tests/WorkspaceModelTests.cpp
  tests/WrappedLinesTests.cpp
)
target_include_directories(micronotes_tests PRIVATE src tests)
target_compile_definitions(micronotes_tests PRIVATE MICRONOTES_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(micronotes_tests PRIVATE micronotes_shell)
add_test(NAME micronotes_tests COMMAND micronotes_tests)

if(MICRONOTES_PERF_HARNESS_BUILD)
  add_executable(micronotes_perf
    tools/PerfMain.cpp
    tools/perf/DocumentLanes.cpp
    tools/perf/FontLane.cpp
    tools/perf/Harness.cpp
    tools/perf/InteractionLane.cpp
    tools/perf/PersistenceLane.cpp
    tools/perf/SearchLane.cpp
    tools/perf/ShellLane.cpp
    tools/perf/UndoLane.cpp
  )
  target_include_directories(micronotes_perf PRIVATE src tools)
  target_link_libraries(micronotes_perf PRIVATE micronotes_shell)
endif()

foreach(target micronotes_core micronotes_shell micronotes micronotes_tests)
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
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/" DESTINATION ${CMAKE_INSTALL_DATADIR}/micronotes/fonts)

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

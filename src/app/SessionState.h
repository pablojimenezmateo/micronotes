#pragma once

#include <filesystem>
#include <optional>

namespace micronotes::app {

struct UiRuntime;

// Where a session's state lives on disk, and the two operations that move it.
//
// Three files sit beside a library, in its own `.micronotes` directory, and one
// sits in the user's config directory. None of them is a note: which folders
// are open, which toggles are collapsed, which note was showing and which
// folder to open at all are view state, and putting any of it into a `.md` file
// would mean a disclosure triangle changing a file another tool reads.
//
// The two functions below are a pair, and that is the point of the unit: what
// `openLibraryRoot` restores is exactly what `persistLibraryState` wrote, so a
// piece of state added to one and forgotten in the other is visible here rather
// than spread across a three-thousand-line file.

std::filesystem::path uiStatePath(const std::filesystem::path& root);
std::filesystem::path foldStatePath(const std::filesystem::path& root);
std::filesystem::path treeStatePath(const std::filesystem::path& root);

// The library the app opens when it is started with no `--library`. Kept in the
// config directory rather than in any library, because it names which one.
std::filesystem::path libraryPathConfigPath();
std::optional<std::filesystem::path> readConfiguredLibraryRoot();
bool writeConfiguredLibraryRoot(const std::filesystem::path& root);

// Everything about a library that lives outside its notes. Written when the app
// closes, and again before it opens a different library, so the view state of
// the one being left is never spent on the one being opened.
void persistLibraryState(UiRuntime& ui);

// Opens a library and restores the view state stored beside it. The one path
// into a library, taken by startup and by the settings dialog alike, so a
// library opened from inside the app comes up exactly as it would on the next
// launch.
bool openLibraryRoot(UiRuntime& ui, const std::filesystem::path& root);

}

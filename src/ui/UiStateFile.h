#pragma once

#include "CoreAliases.h"

#include "ui/WorkspaceModel.h"

#include <filesystem>

// The view-state file, beside each library: which notes are in tabs, how wide
// the panels are, which bands are shut, which colour each tag was given.
//
// A file format, and that is why it is its own unit. It lived in `AppState`,
// which is the class that decides whether a note can safely be written to disk
// -- so the most safety-critical code in the app shared a translation unit with
// a hundred and sixty lines of `out << "key=" << value`, on no better grounds
// than that the serializer had `workspace_` in scope.
//
// It also carries a compatibility story of its own, which is the part worth
// being able to read in one place: keys are **added, never redefined**, so a
// file written by an older binary still opens and a file written by this one
// still opens in an older binary. `pane=` and `note=` predate tabs and a
// single-tab session is synthesised from them; only the *shut* sidebar bands
// are written, so the common case writes nothing and an older file reads as all
// four open; only tags whose colour was actually chosen are written, so a tag
// on its derived colour is not frozen against a future palette.
namespace micronotes::ui {

struct UiSelection;

// Writes durably: a torn view-state file would lose the session rather than a
// note, but it would lose it silently.
bool writeUiState(const std::filesystem::path& path, const WorkspaceModel& workspace,
                      const UiSelection& selection);

// Replaces `workspace` and `selection` with whatever the file says -- including
// when it says nothing, which is why both are cleared before it is opened: a
// library with no state file of its own must not inherit the favorites and the
// open note of the one before it.
//
// Also applies the three global view settings the file carries (theme, text
// size, page width), because they are per-library preferences rather than
// per-window ones.
bool readUiState(const std::filesystem::path& path, WorkspaceModel& workspace,
                     UiSelection& selection);

}

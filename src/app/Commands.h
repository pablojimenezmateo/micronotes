#pragma once

#include "ui/WorkspaceModel.h"

#include <string>

// The one place a named command turns into behaviour.
//
// Four surfaces offer commands -- the palette, the menu bar, the key chain and
// the buttons a pane draws -- and none of them may know what a command *does*,
// only what it is called. `performCommand` is the chain they all funnel into,
// and `ArchitectureTests` checks that every name any of them offers has a
// branch here, because a name with no branch is a control that silently does
// nothing when clicked.
//
// It sat in Application.cpp between the palettes it opens and the key handler
// that calls it, which made "what can this app be asked to do" a question you
// answered by reading a two-thousand-line file. It is a hundred lines on its
// own.
namespace micronotes::app {

struct UiRuntime;

// Runs the command named `id`. The names are `ui::actionSpecs()`'s.
void performCommand(UiRuntime& ui, const std::string& id);

// Which pane shows the note. Not a plain setter: the pane decides where focus
// goes and whether a block selection survives, and a caller that reaches past
// this into `WorkspaceModel` gets neither.
void setPaneMode(UiRuntime& ui, ui::PaneMode mode);
void cyclePaneMode(UiRuntime& ui);

// Opens a different library without restarting. The one being left is written
// out first, so its open note and favorites go with it rather than
// following the user into the new one.
void switchLibrary(UiRuntime& ui, const std::string& typed);

}

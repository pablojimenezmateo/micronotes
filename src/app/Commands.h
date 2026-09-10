#pragma once

#include "ui/WorkspaceModel.h"

#include <span>
#include <string>
#include <string_view>

// The one place a named command turns into behaviour.
//
// Four surfaces offer commands -- the palette, the menu bar, the key chain and
// the buttons a pane draws -- and none of them may know what a command *does*,
// only what it is called. `performCommand` is what they all funnel into, and
// `ArchitectureTests` checks that every name any of them offers reaches one,
// because a name that reaches none is a control that silently does nothing
// when clicked.
//
// It sat in Application.cpp between the palettes it opens and the key handler
// that calls it, which made "what can this app be asked to do" a question you
// answered by reading a two-thousand-line file. It is a hundred lines on its
// own.
namespace micronotes::app {

struct UiRuntime;

// One command: the name a surface offers and what running it does.
//
// A table rather than the ninety-branch `if`/`else if` chain this was, and the
// reason is entirely in what can be *checked*. The chain could only be checked
// by reading `src/app/*.cpp` as text and looking for the literal `== "name"`
// -- a test that cannot see whether the branch it found is reachable, and that
// had to assert `performCommand`'s own signature line still existed so it
// would not quietly scan for a spelling nothing used any more. A branch nested
// inside another id's `if`, or one placed after an arm that already matched,
// passed it.
//
// A table cannot have an unreachable row, and the two registries can be
// compared directly: `commandSpecs()` and `ui::actionSpecs()` name the same
// set, which is asserted both ways. An action with no command is a dead menu
// row or a dead shortcut -- which `F2`, `Ctrl+Q` and `Ctrl+O` all once were --
// and a command with no action is a command nothing can reach.
//
// `run` is a plain function pointer, so every row is a captureless lambda and
// the table is `constexpr`. Nothing here closes over anything: an arm that
// looks like it needs to -- the pane it sets, the tab scope it closes -- reads
// it from `ui`, which is the only state a command has.
struct CommandSpec {
  std::string_view name;
  void (*run)(UiRuntime& ui);
};

// Every command, in the order a reader would want them: what opens something,
// what edits the note, what moves the view. The order is not the dispatch --
// `performCommand` matches by name -- so it is free to be the readable one.
std::span<const CommandSpec> commandSpecs();

// Runs the command named `id`. The names are `ui::actionSpecs()`'s.
void performCommand(UiRuntime& ui, const std::string& id);

// Which pane shows the note. Not a plain setter: the pane decides where focus
// goes and whether a block selection survives, and a caller that reaches past
// this into `WorkspaceModel` gets neither.
void setPaneMode(UiRuntime& ui, ui::PaneMode mode);
void cyclePaneMode(UiRuntime& ui);

// Opens a different library without restarting. The one being left is written
// out first, so its open note and its pinned notes go with it rather than
// following the user into the new one.
void switchLibrary(UiRuntime& ui, const std::string& typed);

}

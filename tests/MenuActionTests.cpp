#include "TestSupport.h"
#include "ShellFixture.h"

#include "app/Commands.h"
#include "app/ContextMenus.h"
#include "app/Notes.h"
#include "app/OverlayRouter.h"
#include "app/Shell.h"
#include "app/TabStrip.h"
#include "library/Library.h"
#include "app/Focus.h"
#include "ui/Actions.h"
#include "ui/Theme.h"
#include "ui/Menus.h"
#include "ui/Overlay.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// Every row of every menu does something -- the bar's seven and the six the
// overlay backs.
//
// The menu bar has had this check since it was written -- an action the bar
// offers and nothing dispatches fails the build. The context menus had none,
// and they are the surface where it is easiest to lose one: their rows carry
// bare string ids rather than `ui::ActionId`s, and each menu's arm of
// `handleOverlayResult` ends in a fall-through that quietly swallows anything
// it does not recognise. Renaming one id from `export-pdf` to
// `export-folder-pdf` was enough to make "Export as PDF" on a notebook do
// nothing at all, with every other test still green.

namespace {

using micronotes::app::UiRuntime;

// What "something happened" is, for a menu row: everything about the shell a
// menu row could plausibly move, as one string.
//
// Deliberately broad, because the rows are. One puts a prompt up, one reports
// in the status line, one narrows the selection, one closes a tab and says
// nothing at all -- and a witness narrower than the union of those reads a row
// that worked as a row that did not. What it must never miss is a row that
// moved nothing, which is the only thing this is trying to catch.
std::string witnessOf(const UiRuntime& ui) {
  const auto& workspace = ui.state.workspace();
  const auto& selection = ui.state.selection();
  std::string out;
  out += "status=" + ui.status.text;
  out += "|overlay=" + std::string(ui.overlays.top() != nullptr ? ui.overlays.top()->id : "");
  out += "|window=" + std::to_string(static_cast<int>(ui.chrome.pendingWindowAction));
  out += "|tabs=" + std::to_string(workspace.tabs.size());
  out += "|active=" + std::to_string(workspace.activeTab);
  out += "|note=" + selection.noteId;
  out += "|folder=" + selection.folder.generic_string();
  out += "|tag=" + selection.tag;
  out += "|pane=" + std::to_string(static_cast<int>(workspace.paneMode()));
  out += "|pinned=" + std::to_string(workspace.pinnedNotes.size());
  out += "|colours=" + std::to_string(workspace.tagColors.choices().size());
  out += "|text=" + ui.editor.text();
  out += "|caret=" + std::to_string(ui.editor.cursor());
  out += "|selected=" + std::to_string(ui.editor.hasSelection() ? 1 : 0);
  out += "|focus=" + std::string(micronotes::app::focusName(ui.focus));
  // The two surfaces that are not overlays: the settings card (which is also
  // the About page, in its other mode) and the find bar.
  out += "|card=" + std::to_string(ui.settings.visible ? 1 : 0);
  out += "/" + std::to_string(static_cast<int>(ui.settings.mode));
  out += "|find=" + std::to_string(ui.find.open ? 1 : 0);
  out += "|panels=" + std::to_string(workspace.sidebarVisible ? 1 : 0) +
         std::to_string(workspace.rightPanelVisible ? 1 : 0) +
         std::to_string(static_cast<int>(workspace.rightPanelView));
  out += "|theme=" + std::string(micronotes::ui::themeModeName(micronotes::ui::themeMode()));
  return out;
}

// The rows a menu offers, in the order it offers them. Opening it is the only
// way to ask: the tables are locals inside `app/ContextMenus.cpp`.
std::vector<std::string> rowsOf(UiRuntime& ui) {
  std::vector<std::string> ids;
  const auto* overlay = ui.overlays.top();
  micronotes::tests::require(overlay != nullptr, "the menu did not open");
  for(const auto& item : overlay->items) {
    if(item.separator || item.id.empty()) continue;
    ids.push_back(item.id);
  }
  ui.overlays.close();
  return ids;
}

// Runs each row of a menu on a shell that has just been put back the way it
// started, and requires that each one moved something.
void everyRowDoesSomething(UiRuntime& ui, const std::string& overlayId,
                           const std::vector<std::string>& ids,
                           const std::vector<std::string>& allowedToDoNothing = {}) {
  micronotes::tests::require(!ids.empty(), overlayId + " offered no rows at all");
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    ui.chrome.pendingWindowAction = micronotes::app::WindowAction::None;
    const std::string before = witnessOf(ui);

    micronotes::app::handleOverlayResult(ui, {overlayId, id, ""});

    const bool expected = std::find(allowedToDoNothing.begin(), allowedToDoNothing.end(), id) ==
                          allowedToDoNothing.end();
    const bool moved = witnessOf(ui) != before;
    micronotes::tests::require(
      moved == expected,
      overlayId + "'s \"" + id + "\" row " + (moved ? "did something unexpectedly" : "did nothing") +
        " -- a row that runs no command, opens nothing and says nothing is a row "
        "that looks broken and is");
  }
  while(ui.overlays.active()) ui.overlays.close();
}

}

MICRONOTES_TEST(context_menu_every_note_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-note", "# Note\n\nBody\n");
  micronotes::app::openNoteMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "note-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_notebook_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-folder", "# Note\n\nBody\n");
  micronotes::app::openFolderMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "folder-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_tab_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-tab", "# Note\n\nBody\n");
  const auto noteId = ui.state.selection().noteId;
  MICRONOTES_REQUIRE(!noteId.empty());
  micronotes::app::openTabMenu(ui, noteId, 100.0f, 100.0f);
  const auto ids = rowsOf(ui);
  // The tab menu carries which tab it is about in `value`, so the rows have to
  // be run with it rather than through the plain helper.
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    const std::string before = witnessOf(ui);
    micronotes::app::handleOverlayResult(ui, {"tab-menu", id, noteId});
    micronotes::tests::require(witnessOf(ui) != before,
                               "the tab menu's \"" + id + "\" row did nothing");
    // Closing a tab is the one row that leaves the fixture without the note
    // the rest are about, so it is run last by being restored here.
    if(ui.state.workspace().tabs.empty()) micronotes::app::selectNoteById(ui, noteId);
  }
}

MICRONOTES_TEST(context_menu_every_tag_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-tag", "---\ntags: [work]\n---\n\nBody\n");
  micronotes::app::openTagMenu(ui, "work", 100.0f, 100.0f);
  const auto ids = rowsOf(ui);
  for(const auto& id : ids) {
    while(ui.overlays.active()) ui.overlays.close();
    ui.status = std::string();
    ui.status.at = 0;
    const std::string before = witnessOf(ui);
    micronotes::app::handleOverlayResult(ui, {"tag-menu", id, "work"});
    micronotes::tests::require(witnessOf(ui) != before,
                               "the tag menu's \"" + id + "\" row did nothing");
  }
}

// The two menus over the `files` tree. They share the shape of the note's and
// the notebook's but not one line of their routing, and their target is a path
// on `ui.sidebar` rather than the selection -- which is exactly the kind of
// second copy that goes stale unnoticed.
MICRONOTES_TEST(context_menu_every_companion_file_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-file", "# Note\n\nBody\n");
  const auto files = scratch.root() / std::string(micronotes::library::kFilesDirName);
  std::filesystem::create_directories(files);
  { std::ofstream out(files / "diagram.svg"); out << "<svg/>\n"; }
  ui.state.refreshLibrary();

  ui.sidebar.companionTarget = std::filesystem::path(micronotes::library::kFilesDirName) / "diagram.svg";
  micronotes::app::openFileMenu(ui, 100.0f, 100.0f);
  // "Open" hands the file to the desktop, which on a test machine with no
  // handler reports either way -- both are a status line, which is the
  // witness. Nothing else here touches disk.
  everyRowDoesSomething(ui, "file-menu", rowsOf(ui));
}

MICRONOTES_TEST(context_menu_every_companion_folder_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-files-dir", "# Note\n\nBody\n");
  const auto files = scratch.root() / std::string(micronotes::library::kFilesDirName);
  std::filesystem::create_directories(files / "diagrams");
  ui.state.refreshLibrary();

  ui.sidebar.companionTarget = std::filesystem::path(micronotes::library::kFilesDirName) / "diagrams";
  micronotes::app::openFilesFolderMenu(ui, 100.0f, 100.0f);
  everyRowDoesSomething(ui, "files-folder-menu", rowsOf(ui));
}

// The bar's own rows. `architecture_every_offered_action_is_dispatched` reads
// the sources and proves a branch *exists* for each; this runs them and proves
// the branch does something. The two are not the same check: a branch that
// calls a function whose first line refuses is a branch, and reads as one.
MICRONOTES_TEST(menu_bar_every_row_does_something) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-bar", "# Note\n\n- [ ] task\n\nBody\n");
  const auto noteId = ui.state.selection().noteId;
  // A second tab, so the two that step between them have somewhere to step.
  micronotes::app::createNote(ui, "Second");
  micronotes::app::selectNoteById(ui, noteId);

  // Rows whose whole effect is outside the shell, so this witness cannot see
  // them however wide it gets. Each is named rather than the list being
  // loosened, because the point of the list is that it is short.
  const std::vector<std::string> outsideTheShell {
    // Write to the clipboard and to X11's primary selection. Nothing about the
    // shell changes, deliberately: the status bar reports state and does not
    // echo actions, and a copy is the action people take most often.
    "copy",
    // Read from a clipboard that a headless test has not put anything in.
    "paste",
    "paste-plain",
    // Steps the find bar's matches, and with the bar shut there are none. It
    // is listed and refused rather than hidden, which `ActionSpec::needsNote`
    // explains, and doing nothing is what "refused" looks like here.
    "find-next",
    "find-previous",
    // The one row below whose empty stack is the fixture rather than the
    // shell: the setup makes an edit, so Undo has something and Redo cannot.
    // Undoing first would test Redo and stop testing Undo.
    "redo",
  };

  std::string idle;
  for(const auto& menu : micronotes::ui::menuSpecs()) {
    for(const auto& item : menu.items) {
      if(item.separator) continue;
      const auto* spec = micronotes::ui::findAction(item.action);
      MICRONOTES_REQUIRE(spec != nullptr);
      const std::string name(spec->name);
      if(std::find(outsideTheShell.begin(), outsideTheShell.end(), name) != outsideTheShell.end()) {
        continue;
      }

      // Put the shell back the way every other row sees it. A row that closed
      // a tab, moved the caret or shut a panel must not decide what the next
      // row is judged against.
      while(ui.overlays.active()) ui.overlays.close();
      ui.settings = {};
      ui.find = {};
      micronotes::app::selectNoteById(ui, noteId);
      ui.focus = micronotes::app::FocusArea::Editor;
      ui.editor.setText("# Note\n\nfirst para\n\nsecond para\n\nthird para\n");
      // In the middle block, with a word selected and one edit behind it: what
      // the note looks like when somebody reaches for the menu at all. Without
      // it, half these rows are correctly refusing an empty document and the
      // test is measuring the fixture.
      const std::size_t caret = ui.editor.text().find("second");
      ui.editor.moveCursor(caret);
      ui.editor.insert("a");
      ui.editor.selectRange(caret, caret + 3);
      ui.status = std::string();
      ui.status.at = 0;
      ui.chrome.pendingWindowAction = micronotes::app::WindowAction::None;
      const std::string before = witnessOf(ui);

      micronotes::app::performCommand(ui, name);

      if(witnessOf(ui) == before) idle += (idle.empty() ? "" : ", ") + name;
    }
  }
  while(ui.overlays.active()) ui.overlays.close();
  micronotes::tests::require(
    idle.empty(),
    "menu rows that ran and moved nothing: " + idle +
      " -- the bar is the shell's readable surface, and a row that does nothing "
      "when chosen teaches the reader the menu is decorative");
}

// Every context menu shows all of itself.
//
// The row cap on an `Overlay` defaults to a *palette's* -- twelve, with the
// rest scrolled -- and a menu that scrolls hides its last row, which by
// convention here is the destructive one, behind a scrollbar nobody expects on
// a menu. Each of these used to set the cap from its own row count on the line
// before opening, and the tag menu did not; it has five rows, so nothing
// noticed. `ui::openMenu` sets it where the rows are known, and this is what
// says every menu goes through it.
MICRONOTES_TEST(context_menus_show_every_row_they_offer) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-rows", "# Note\n\nBody\n");
  const auto noteId = ui.state.selection().noteId;
  ui.sidebar.companionTarget = "files/a.txt";

  const auto showsEveryRow = [&](const char* what) {
    const auto* overlay = ui.overlays.top();
    micronotes::tests::require(overlay != nullptr, std::string(what) + " did not open");
    micronotes::tests::require(
      overlay->maxRows >= static_cast<int>(overlay->items.size()),
      std::string(what) + " would scroll: " + std::to_string(overlay->items.size()) +
        " rows against a cap of " + std::to_string(overlay->maxRows));
    while(ui.overlays.active()) ui.overlays.close();
  };

  micronotes::app::openNoteMenu(ui, 100.0f, 100.0f);
  showsEveryRow("the note menu");
  micronotes::app::openFolderMenu(ui, 100.0f, 100.0f);
  showsEveryRow("the notebook menu");
  micronotes::app::openTabMenu(ui, noteId, 100.0f, 100.0f);
  showsEveryRow("the tab menu");
  micronotes::app::openTagMenu(ui, "work", 100.0f, 100.0f);
  showsEveryRow("the tag menu");
  micronotes::app::openFileMenu(ui, 100.0f, 100.0f);
  showsEveryRow("the file menu");
  micronotes::app::openFilesFolderMenu(ui, 100.0f, 100.0f);
  showsEveryRow("the files folder menu");
}

// The three path commands, and the `PathCommand` that keeps them apart.
//
// They were the one place in `commandSpecs()` where a row could be wrong
// without being unreachable: all three went through `handleNotePathCommand`,
// which told them apart by the id it was *handed* rather than by the id the
// row is keyed on. A row passing its neighbour's spelling dispatched, did
// something, and did the wrong thing -- and both spellings being real ids
// meant nothing structural saw it, while a test with no clipboard cannot tell
// two copies apart by their effect either.
//
// So the string is gone from the rows and the enum decides. What is left to
// check is the parse the routers still need, and that each value reaches a
// different arm.
MICRONOTES_TEST(the_three_path_commands_are_told_apart_by_type_not_by_spelling) {
  using micronotes::app::PathCommand;
  MICRONOTES_REQUIRE(micronotes::app::pathCommandFor("show-on-disk") == PathCommand::ShowOnDisk);
  MICRONOTES_REQUIRE(micronotes::app::pathCommandFor("copy-relative-path") ==
                     PathCommand::CopyRelative);
  MICRONOTES_REQUIRE(micronotes::app::pathCommandFor("copy-absolute-path") ==
                     PathCommand::CopyAbsolute);
  // Everything else is not one, which is what lets a router hand it any
  // overlay item id and use the answer to decide whether it handled it.
  MICRONOTES_REQUIRE(!micronotes::app::pathCommandFor("save").has_value());
  MICRONOTES_REQUIRE(!micronotes::app::pathCommandFor("").has_value());
  MICRONOTES_REQUIRE(!micronotes::app::pathCommandFor("show-on-disk ").has_value());

  // And the three do different things. Only `show-on-disk` is distinguishable
  // from the other two without a clipboard -- which is the whole reason the
  // enum exists rather than a test standing in for it.
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-paths", "# Note\n\nBody\n");
  MICRONOTES_REQUIRE(!ui.state.selection().noteId.empty());
  const auto say = [&](const char* command) {
    ui.status = std::string();
    micronotes::app::performCommand(ui, command);
    return std::string(ui.status.text);
  };
  const std::string shown = say("show-on-disk");
  const std::string copied = say("copy-absolute-path");
  micronotes::tests::require(!shown.empty() && !copied.empty(),
                             "a path command said nothing at all");
  micronotes::tests::require(shown != copied,
                             "show-on-disk and copy-absolute-path said the same thing: '" + shown +
                               "' -- one of them is running the other");
  micronotes::tests::require(shown.rfind("Copied", 0) != 0,
                             "show-on-disk copied something: '" + shown + "'");
}

// "Show on disk" asks the desktop for the *containing directory*, and asks it
// through the seam rather than by forking.
//
// Both halves matter and the second is why the seam exists. This suite runs
// every row of every context menu, and five of them carry this command -- so
// before the launcher could be replaced, `ctest` forked `xdg-open` five or six
// times a run and left that many file manager windows open on the developer's
// desktop. Nothing failed. Asserting on the recorder is what turns a side
// effect nobody could see into the thing being checked.
MICRONOTES_TEST(show_on_disk_asks_the_desktop_for_the_containing_directory) {
  UiRuntime ui;
  const micronotes::tests::ScratchNote scratch(ui, "menu-reveal", "# Note\n\nBody\n");
  auto& launches = micronotes::tests::desktopLaunches();
  const std::size_t before = launches.size();

  micronotes::app::performCommand(ui, "show-on-disk");

  micronotes::tests::require(launches.size() == before + 1,
                             "show-on-disk asked the desktop " +
                               std::to_string(launches.size() - before) + " times, not once");
  const auto& command = launches.back();
  MICRONOTES_REQUIRE(command.size() == 2);
  MICRONOTES_REQUIRE(command[0] == "xdg-open");
  // The directory the note is in, not the note: `xdg-open` on a `.md` launches
  // a text editor on it, which is the one thing the reader already has.
  micronotes::tests::require(command[1] == scratch.root().string(),
                             "show-on-disk opened '" + command[1] + "' rather than the folder '" +
                               scratch.root().string() + "'");

  // And copying a path asks the desktop for nothing at all.
  const std::size_t after = launches.size();
  micronotes::app::performCommand(ui, "copy-absolute-path");
  micronotes::tests::require(launches.size() == after,
                             "copy-absolute-path launched something");
}

#include "TestSupport.h"
#include "TempDir.h"

#include "app/InlineText.h"
#include "app/MarkdownBlocks.h"
#include "app/PageChrome.h"
#include "app/PageView.h"
#include "app/RightPanel.h"
#include "app/Shell.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/BlockScan.h"
#include "doc/LinkTarget.h"
#include "app/SidebarModel.h"
#include "app/Dismiss.h"
#include "app/Fields.h"
#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/WikiLinks.h"
#include "ui/ImageCache.h"
#include "ui/TextRenderer.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Nothing under src/app/ used to be reachable from a test: every file there was
// compiled straight into the executable, so a pane, a model or a policy could
// only be checked by taking a screenshot of the running app. `micronotes_shell`
// is a library now and the test binary links it, so these are ordinary unit
// tests over code that had none.

using micronotes::doc::headingAnchor;
using micronotes::app::InlineRun;
using micronotes::app::inlineRuns;
using micronotes::app::searchResultRowHeight;
using micronotes::app::SidebarMetrics;
using micronotes::app::sidebarMetrics;
using micronotes::app::sidebarRowRange;
using micronotes::app::SidebarRow;

// The shell around the note: opening one, the tabs that result, what a change
// made outside the app does to the buffer, and what Escape steps back out of.

// The whole external-change path, end to end, through a real `UiRuntime`: a
// real library, a real watcher, a real editor buffer, and a write that does not
// go through micronotes.
//
// This is the behaviour the app was missing entirely. Editing a note in another
// program left the copy on screen stale and the next autosave wrote it back
// over the top; there was no watcher, and the focus-gained refresh updated the
// index and never the buffer.
MICRONOTES_TEST(shell_reloads_a_watched_note_that_changed_outside_the_app) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-watch");
  const auto& root = rootDir.path();

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::createNote(ui, "Untitled");
  ui.editor.setText("mine\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  MICRONOTES_REQUIRE(ui.editor.text() == "mine\n");
  const auto path = ui.state.openNote().path();
  const auto noteId = ui.state.selection().noteId;
  MICRONOTES_REQUIRE(ui.watcher.active());

  // The app's own save is reported back by the watcher like any other write --
  // nothing filters it out -- and it must come to nothing: the file matches
  // what was written, so no reload, no re-index, and not even a bump of the
  // library revision the view memos are keyed on. Echo suppression falls out
  // of comparing the disk rather than remembering who wrote it.
  ui.editor.setText("mine, edited\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  const auto revisionAfterSave = ui.state.catalog().revision();
  for(int attempt = 0; attempt < 60; ++attempt) {
    micronotes::app::applyWatchedChanges(ui);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  MICRONOTES_REQUIRE(ui.editor.text() == "mine, edited\n");
  MICRONOTES_REQUIRE(!ui.editor.dirty());
  MICRONOTES_REQUIRE(ui.state.catalog().revision() == revisionAfterSave);

  // Now somebody else rewrites it.
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << noteId << "\ntitle: Untitled\n---\n\nfrom another program\n";
  }
  const auto later = std::filesystem::last_write_time(path) + std::chrono::seconds(2);
  std::filesystem::last_write_time(path, later);

  bool applied = false;
  for(int attempt = 0; attempt < 400 && !applied; ++attempt) {
    applied = micronotes::app::applyWatchedChanges(ui);
    if(!applied) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  MICRONOTES_REQUIRE(applied);
  // A clean buffer took the new text, without waiting for a focus change.
  MICRONOTES_REQUIRE(ui.editor.text() == "from another program\n");
  MICRONOTES_REQUIRE(!ui.editor.dirty());
  MICRONOTES_REQUIRE(ui.status.text.find("Reloaded") != std::string::npos);
  // And the index followed, so search and the sidebar agree with the page.
  ui.state.setSearch("another program");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);

}

// The other half of the rule: unsaved work is never replaced by what is on
// disk. The save path is what resolves it, and it keeps both versions.
MICRONOTES_TEST(shell_keeps_a_dirty_buffer_when_the_file_changes_outside) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-watch-dirty");
  const auto& root = rootDir.path();

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::createNote(ui, "Untitled");
  ui.editor.setText("mine\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  const auto path = ui.state.openNote().path();
  const auto noteId = ui.state.selection().noteId;

  // Unsaved work in the buffer, and a change on disk underneath it.
  ui.editor.setText("my unsaved draft\n");
  ui.editor.markDirty();
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << noteId << "\ntitle: Untitled\n---\n\ntheirs\n";
  }
  std::filesystem::last_write_time(path, std::filesystem::last_write_time(path) + std::chrono::seconds(2));

  MICRONOTES_REQUIRE(!micronotes::app::reloadSelectedIfChangedOnDisk(ui));
  MICRONOTES_REQUIRE(ui.editor.text() == "my unsaved draft\n");
  MICRONOTES_REQUIRE(ui.editor.dirty());

  // Saving keeps both: the buffer lands in the note, their text becomes a note
  // of its own, and the status line names it so it can be found.
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui, /*quiet=*/true));
  MICRONOTES_REQUIRE(ui.status.text.find("was kept as") != std::string::npos);
  MICRONOTES_REQUIRE(ui.state.catalog().notes().size() == 2);
  ui.state.setSearch("theirs");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);
  ui.state.setSearch("my unsaved draft");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);

}

// Opening a note has to move the *sidebar* to it, not just the breadcrumb.
//
// `selectFolder` and `tree.reveal` are one action -- make this folder the
// context and open the tree onto it -- and they were said as two statements at
// four call sites. The fifth, the one a click on a RECENT, FAVORITES or search
// row goes through, said only the first half. So clicking a recent note filed
// in a collapsed notebook left the note showing nowhere in the tree: the only
// row for it was the flat one that had just been clicked, sitting at the top
// level, outside the folder the breadcrumb had that instant started naming.
//
// `showFolder` is the pair, and this is the property it exists for.
MICRONOTES_TEST(shell_opening_a_note_opens_the_tree_onto_its_folder) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-showfolder");
  const auto& root = rootDir.path();
  // A space in the name, because that is what a real notebook is called and it
  // is the shape a path used as a map key gets wrong.
  const std::filesystem::path folder = "General information";
  std::filesystem::create_directories(root / folder);
  {
    std::ofstream note(root / folder / "Alpha.md");
    note << "---\nid: gi-alpha\ntitle: Alpha\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));

  // Collapsed to start with, which is the state the bug needed: the folder is
  // shut, so nothing in it is on screen.
  ui.sidebar.tree.setExpanded(folder, false);
  MICRONOTES_REQUIRE(!ui.sidebar.tree.expanded(folder));

  const auto* alpha = ui.state.catalog().noteById("gi-alpha");
  MICRONOTES_REQUIRE(alpha != nullptr);
  MICRONOTES_REQUIRE(alpha->folder == folder);

  micronotes::app::showFolder(ui, alpha->folder);

  // Both halves: the context moved, and the tree opened.
  MICRONOTES_REQUIRE(ui.state.selection().folder == folder);
  MICRONOTES_REQUIRE(ui.sidebar.tree.expanded(folder));
  MICRONOTES_REQUIRE(ui.sidebar.tree.expanded({}));  // and every ancestor of it

  // And the note is now a row inside that folder rather than only in a flat
  // list above it -- which is the thing the user was looking for.
  const auto rows = ui.sidebar.tree.rows(ui.state.catalog().folders(), ui.state.catalog().notes());
  bool folderRow = false;
  bool noteUnderIt = false;
  for(const auto& row : rows) {
    if(row.kind == micronotes::ui::TreeRowKind::Folder && row.folder == folder) {
      folderRow = true;
      continue;
    }
    if(folderRow && row.kind == micronotes::ui::TreeRowKind::Note && row.noteId == "gi-alpha") {
      noteUnderIt = row.folder == folder;
      break;
    }
  }
  MICRONOTES_REQUIRE(folderRow);
  MICRONOTES_REQUIRE(noteUnderIt);
}

// Nothing in the app could open a note in a second tab.
//
// `WorkspaceModel::openNote` took an `inNewTab` flag and honoured it, and its
// own unit test passed -- but exactly one caller in the whole app ever passed
// `true`, the Ctrl+Shift+T palette. Every other route to a note (the sidebar
// tree, RECENT, FAVORITES, a search hit, a backlink, a wiki link) went through
// `selectNoteById`, which had no such parameter, so every one of them replaced
// the note being read.
//
// So the model was right, the model's test was right, and the behaviour was
// missing anyway: **a flag nothing sets is a feature nothing has.**
MICRONOTES_TEST(shell_opening_a_note_opens_a_tab_on_it) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-newtab");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  for(const char* name : {"Alpha", "Beta", "Gamma"}) {
    std::ofstream note(root / (std::string(name) + ".md"));
    note << "---\nid: nt-" << name << "\ntitle: " << name << "\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  const auto& tabs = ui.state.workspace().tabs;

  micronotes::app::selectNoteById(ui, "nt-Alpha");
  MICRONOTES_REQUIRE(tabs.size() == 1);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "nt-Alpha");

  // Each note gets its own tab, and what was open stays open.
  micronotes::app::selectNoteById(ui, "nt-Beta");
  MICRONOTES_REQUIRE(tabs.size() == 2);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "nt-Beta");
  MICRONOTES_REQUIRE(ui.state.workspace().findTab("nt-Alpha") != std::string::npos);

  micronotes::app::selectNoteById(ui, "nt-Gamma");
  MICRONOTES_REQUIRE(tabs.size() == 3);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "nt-Gamma");
  // Beside the one it came from, not at the end of the strip.
  MICRONOTES_REQUIRE(ui.state.workspace().activeTab == 2);

  // A note already open is gone to rather than opened twice -- clicking a name
  // is never a request for a duplicate tab.
  micronotes::app::selectNoteById(ui, "nt-Beta");
  MICRONOTES_REQUIRE(tabs.size() == 3);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "nt-Beta");

  // The cursor's policy is the exception, and it is what keeps arrowing through
  // the sidebar from opening a tab per note in the library.
  micronotes::app::selectNoteById(ui, "nt-Alpha", micronotes::ui::TabPolicy::Reuse);
  MICRONOTES_REQUIRE(tabs.size() == 3);
}

// One press of Escape undoes one narrowing, innermost first.
//
// The pile of `if`s this replaced fired all of them at once, so a reader who
// had typed a query while a tag filter was running got both cleared by a single
// press and no way to see the middle state -- and the tag filter, which nothing
// else could undo, was not in the pile at all.
MICRONOTES_TEST(shell_escape_undoes_one_narrowing_at_a_time) {
  using micronotes::app::Dismissed;
  const micronotes::tests::TempDir rootDir("micronotes-shell-dismiss");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  {
    std::ofstream out(root / "plan.md", std::ios::binary | std::ios::trunc);
    out << "---\nid: dm-plan\ntitle: Plan\ntags: work\n---\n\nFindable body.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));

  // Nothing narrowed: Esc has nothing to undo and says so, which is what lets
  // the caller give the key to whatever has focus instead of swallowing it.
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::Nothing);

  // Stack all four up, outermost first, in the order a reader would reach them.
  micronotes::app::selectTag(ui, "work");
  ui.sidebar.creatingFolder = true;
  // The find bar being *open* is the narrowing, not the field having text in
  // it: a reader who clicked back into the note still has the bar and its
  // highlights over the page.
  ui.fields.find.beginWith("body");
  ui.find.open = true;
  ui.fields.search.beginWith("Findable");
  ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
  MICRONOTES_REQUIRE(!ui.state.currentSearchResults().empty());

  // And they come off one at a time, innermost first. Each assertion also
  // pins that the *others* are still standing, which is the property the
  // pile of `if`s did not have.
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::Search);
  MICRONOTES_REQUIRE(ui.fields.search.empty());
  MICRONOTES_REQUIRE(ui.find.open);
  MICRONOTES_REQUIRE(ui.sidebar.creatingFolder);
  MICRONOTES_REQUIRE(ui.state.selection().tag == "work");

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::Find);
  MICRONOTES_REQUIRE(!ui.find.open);
  MICRONOTES_REQUIRE(ui.fields.find.empty());
  MICRONOTES_REQUIRE(ui.sidebar.creatingFolder);
  MICRONOTES_REQUIRE(ui.state.selection().tag == "work");

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::FolderName);
  MICRONOTES_REQUIRE(!ui.sidebar.creatingFolder);
  MICRONOTES_REQUIRE(ui.state.selection().tag == "work");

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::TagFilter);
  MICRONOTES_REQUIRE(ui.state.selection().tag.empty());

  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::Nothing);
}

// "Copy relative path" / "Copy absolute path" / "Show on disk", ported from the
// sibling microide, whose file tree and tab strip both carry them.
//
// The relative one is the one worth having and the one with a way to be wrong:
// it is the spelling that means the same thing to somebody else looking at the
// same library, so it has to be relative to the library root and it has to
// refuse rather than quietly hand back an absolute path when it cannot be.
MICRONOTES_TEST(shell_a_note_reports_both_spellings_of_its_path) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-note-paths");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root / "work" / "deep");
  const auto note = [&](const std::filesystem::path& relative, const char* id) {
    std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << id << "\ntitle: " << id << "\n---\n\nBody.\n";
  };
  note("top.md", "np-top");
  note("work/deep/buried.md", "np-buried");
  note("a note with spaces.md", "np-spaced");

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));

  const auto paths = [&](const char* id) { return micronotes::app::notePathsFor(ui, id); };

  // Relative to the library root, in generic separators -- the spelling that
  // goes into a note or a message rather than the platform's own.
  MICRONOTES_REQUIRE(paths("np-top").relative == "top.md");
  MICRONOTES_REQUIRE(paths("np-buried").relative == "work/deep/buried.md");
  MICRONOTES_REQUIRE(paths("np-spaced").relative == "a note with spaces.md");
  // And the absolute one is the whole path, so it ends with the relative one.
  for(const char* id : {"np-top", "np-buried", "np-spaced"}) {
    const auto both = paths(id);
    micronotes::tests::require(both.absolute.size() > both.relative.size(),
                               std::string(id) + ": the absolute path is not longer than the "
                                                 "relative one");
    micronotes::tests::require(both.absolute.ends_with(both.relative),
                               std::string(id) + ": " + both.absolute + " does not end with " +
                                 both.relative);
    MICRONOTES_REQUIRE(both.absolute.front() == '/');
  }

  // A note nothing names has neither. Empty rather than a guess, so the command
  // can say "no note to locate" instead of copying something arbitrary.
  MICRONOTES_REQUIRE(paths("no-such-note").absolute.empty());
  MICRONOTES_REQUIRE(paths("no-such-note").relative.empty());

  // Empty means "the note on the page", which is what the palette and the menu
  // bar mean by "the note".
  micronotes::app::selectNoteById(ui, "np-buried");
  MICRONOTES_REQUIRE(micronotes::app::notePathsFor(ui, {}).relative == "work/deep/buried.md");
  // And naming one overrides that, which is what lets a right click on a tab or
  // a sidebar row answer about *that* one rather than about whatever is open.
  MICRONOTES_REQUIRE(paths("np-top").relative == "top.md");

  // The commands themselves answer to their action names and to nothing else,
  // so a name added to a menu without a branch here is caught by
  // `architecture_every_offered_action_is_dispatched` rather than silently
  // doing nothing.
  MICRONOTES_REQUIRE(micronotes::app::handleNotePathCommand(ui, "copy-relative-path", {}));
  MICRONOTES_REQUIRE(micronotes::app::handleNotePathCommand(ui, "copy-absolute-path", {}));
  MICRONOTES_REQUIRE(micronotes::app::handleNotePathCommand(ui, "show-on-disk", {}));
  MICRONOTES_REQUIRE(!micronotes::app::handleNotePathCommand(ui, "rename", {}));
  MICRONOTES_REQUIRE(!micronotes::app::handleNotePathCommand(ui, "", {}));
  // A note nothing names is reported rather than passed over.
  MICRONOTES_REQUIRE(micronotes::app::handleNotePathCommand(ui, "copy-absolute-path", "no-such"));
  MICRONOTES_REQUIRE(ui.status.text == "No note to locate");

}

// --- the field table ---------------------------------------------------
//
// `app/Fields.h`'s table replaced three hand-written switches over the same
// five `FocusArea` values -- `focusedField`, `caretStateKey` and
// `handleFieldKey`'s Enter arm. A sixth field used to be three edits with no
// compiler help; these two cases are what makes it one.

MICRONOTES_TEST(field_table_names_every_focus_that_is_a_field_exactly_once) {
  using micronotes::app::FocusArea;
  const auto specs = micronotes::app::fieldSpecs();
  // Every value of the enum is either a field with exactly one row, or a
  // surface with none. Listed here rather than derived, so that adding a value
  // to `FocusArea` makes somebody decide which it is.
  const std::pair<FocusArea, int> expected[] = {
    {FocusArea::Folders, 0},      {FocusArea::Editor, 0},
    {FocusArea::Viewer, 0},       {FocusArea::Search, 1},
    {FocusArea::Find, 1},         {FocusArea::TagEditor, 1},
    {FocusArea::RenameNote, 1},   {FocusArea::RenameFolder, 1},
  };
  int rows = 0;
  for(const auto& [focus, want] : expected) {
    int found = 0;
    for(const auto& spec : specs) {
      if(spec.focus == focus) ++found;
    }
    MICRONOTES_REQUIRE(found == want);
    rows += found;
  }
  MICRONOTES_REQUIRE(rows == static_cast<int>(specs.size()));
}

// Each row points at a different member, which is the part a table cannot get
// wrong by omission but can get wrong by copy-paste: two rows sharing a member
// is a field the focus can reach and never edit.
MICRONOTES_TEST(field_table_gives_each_focus_its_own_field) {
  micronotes::app::UiRuntime ui;
  std::vector<const micronotes::editor::TextField*> seen;
  for(const auto& spec : micronotes::app::fieldSpecs()) {
    ui.focus = spec.focus;
    const auto* field = micronotes::app::focusedField(ui);
    MICRONOTES_REQUIRE(field != nullptr);
    MICRONOTES_REQUIRE(std::find(seen.begin(), seen.end(), field) == seen.end());
    seen.push_back(field);
  }
  ui.focus = micronotes::app::FocusArea::Editor;
  MICRONOTES_REQUIRE(micronotes::app::focusedField(ui) == nullptr);
}

#include "TestSupport.h"
#include "TempDir.h"

#include "app/PageChrome.h"
#include "app/Shell.h"
#include "doc/LinkTarget.h"
#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/WikiLinks.h"

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

// Links, and where they land: how a heading becomes an anchor, how a line of
// text splits into runs around a `[[wikilink]]`, and what opening one does.
//
// How a run of inlines *splits* around one is `tests/RenderLayoutTests.cpp`:
// the shaping moved down to `doc::layoutInlines` so the exporter could reach
// it, and its tests went with it.

// An in-note `[#some heading]` and the heading it points at have to slug to the
// same string or the jump silently does nothing.
MICRONOTES_TEST(shell_anchor_slugs_a_heading_the_way_a_link_spells_it) {
  MICRONOTES_REQUIRE(headingAnchor("Some Heading") == "some-heading");
  MICRONOTES_REQUIRE(headingAnchor("  Leading and trailing  ") == "leading-and-trailing");
  MICRONOTES_REQUIRE(headingAnchor("Punctuation: it's here!") == "punctuation-it-s-here");
  MICRONOTES_REQUIRE(headingAnchor("2 + 2") == "2-2");
  MICRONOTES_REQUIRE(headingAnchor("!!!").empty());
}

// Following a `[[wikilink]]` is the other way into a note, and it took the same
// route through selectNoteById -- so following a link replaced the note the
// link was written in, losing the very context you followed it from.
MICRONOTES_TEST(shell_a_wiki_link_opens_a_tab_and_keeps_its_source) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-wikitab");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root);
  {
    std::ofstream from(root / "From.md");
    from << "---\nid: wt-from\ntitle: From\n---\n\nSee [[Target]].\n";
    std::ofstream to(root / "Target.md");
    to << "---\nid: wt-target\ntitle: Target\n---\n\nArrived.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::selectNoteById(ui, "wt-from");
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 1);

  micronotes::app::openWikiLink(ui, "Target");
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "wt-target");
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 2);
  // The note the link was written in is still open, which is the whole point.
  MICRONOTES_REQUIRE(ui.state.workspace().findTab("wt-from") != std::string::npos);

  // A link naming nothing creates the note, and that one opens beside its
  // source too rather than replacing it -- `createNote` used to hardcode the
  // replacement, so writing a link and following it lost the note you wrote it
  // in.
  micronotes::app::openWikiLink(ui, "Brand New");
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 3);
  MICRONOTES_REQUIRE(ui.state.workspace().findTab("wt-from") != std::string::npos);
}

// What the middle-click handler asks before deciding what the click meant.
//
// Middle click is an X11 primary-selection paste on anything that takes text,
// which this app follows deliberately -- and its handler returned for *every*
// middle click, so one on a note row or a link either did nothing or pasted
// into whatever field still had focus. Neither of those takes text, so the
// click is free to mean "open in a new tab" there, and this predicate is how
// the two are told apart.
MICRONOTES_TEST(shell_a_link_region_is_found_under_the_pointer) {
  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(!micronotes::app::pointOnLink(ui, 10.0f, 10.0f));

  ui.linkRegions.push_back({{100.0f, 200.0f, 60.0f, 18.0f}, "Target", true});
  MICRONOTES_REQUIRE(micronotes::app::pointOnLink(ui, 130.0f, 209.0f));
  // Just outside, on all four edges: a paste that lands next to a link must
  // still be a paste.
  MICRONOTES_REQUIRE(!micronotes::app::pointOnLink(ui, 99.0f, 209.0f));
  MICRONOTES_REQUIRE(!micronotes::app::pointOnLink(ui, 161.0f, 209.0f));
  MICRONOTES_REQUIRE(!micronotes::app::pointOnLink(ui, 130.0f, 199.0f));
  MICRONOTES_REQUIRE(!micronotes::app::pointOnLink(ui, 130.0f, 219.0f));
}

// `[the plan](work/project-plan.md)` is how one note links to another in plain
// Markdown -- it is what every other tool writes and what an imported file
// already contains. Following one used to hand the path to `xdg-open`, so the
// click launched whatever the desktop associates with `.md` and the reader
// watched a second application open a file out of the library they were
// already reading. `[[wikilinks]]` navigated and these did not, which is what
// made the viewer's links look inert.
MICRONOTES_TEST(shell_a_markdown_link_resolves_to_the_note_it_names) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-note-links");
  const auto& root = rootDir.path();
  std::filesystem::create_directories(root / "work" / "deep");
  std::filesystem::create_directories(root / "personal");
  const auto note = [&](const std::filesystem::path& relative, const char* id, const char* title) {
    std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << id << "\ntitle: " << title << "\n---\n\nBody.\n";
  };
  note("hub.md", "ln-hub", "Hub");
  note("work/plan.md", "ln-plan", "Plan");
  note("work/deep/buried.md", "ln-buried", "Buried");
  note("personal/list.md", "ln-list", "List");
  note("my note.md", "ln-spaced", "my note");
  // Not a note: an attachment beside them, which stays the desktop's job.
  {
    std::ofstream out(root / "work" / "diagram.png", std::ios::binary | std::ios::trunc);
    out << "not really a png";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  const auto resolves = [&](const char* target) {
    const auto* found = micronotes::app::noteAtLinkTarget(ui, target);
    return found ? found->id : std::string("<none>");
  };

  // From a note at the root, a bare name and a rooted path are the same thing.
  micronotes::app::selectNoteById(ui, "ln-hub");
  MICRONOTES_REQUIRE(resolves("work/plan.md") == "ln-plan");
  MICRONOTES_REQUIRE(resolves("./work/plan.md") == "ln-plan");
  MICRONOTES_REQUIRE(resolves("work/deep/buried.md") == "ln-buried");
  // The escaped form every tool writes for a name with a space in it.
  MICRONOTES_REQUIRE(resolves("my%20note.md") == "ln-spaced");
  MICRONOTES_REQUIRE(resolves("my note.md") == "ln-spaced");

  // From a note in a subfolder, a relative path means relative to *that note*,
  // which is what a link written by hand inside the folder assumes and what
  // every other Markdown renderer does.
  micronotes::app::selectNoteById(ui, "ln-plan");
  MICRONOTES_REQUIRE(resolves("deep/buried.md") == "ln-buried");
  MICRONOTES_REQUIRE(resolves("../hub.md") == "ln-hub");
  MICRONOTES_REQUIRE(resolves("../personal/list.md") == "ln-list");
  // And the root is still tried second, so a link written the other way -- the
  // form that was already in somebody's library -- keeps working. There is no
  // way to tell the two apart other than to try both.
  MICRONOTES_REQUIRE(resolves("personal/list.md") == "ln-list");

  // Everything that is not a note in this library comes back empty, and the
  // caller hands it to the desktop instead.
  MICRONOTES_REQUIRE(resolves("work/diagram.png") == "<none>");
  MICRONOTES_REQUIRE(resolves("work") == "<none>");
  MICRONOTES_REQUIRE(resolves("nothing-here.md") == "<none>");
  MICRONOTES_REQUIRE(resolves("https://example.com/x.md") == "<none>");
  MICRONOTES_REQUIRE(resolves("") == "<none>");
  // A note's text is not a licence to open arbitrary files: an absolute path
  // and one that climbs out of the library are both refused, however many `..`
  // it spends getting there.
  MICRONOTES_REQUIRE(resolves("/etc/passwd") == "<none>");
  MICRONOTES_REQUIRE(resolves("../../../../../../etc/passwd") == "<none>");
  MICRONOTES_REQUIRE(resolves("../hub.md/../../hub.md") == "<none>");

}

// A link that crosses notes names both a note and a place in it, and the two
// cannot be honoured at the same moment: opening the note replaces the buffer,
// but a page's anchor table is built from its own laid-out document and the
// page is still holding the note you came from until the next frame. Asking
// straight after the open searched the wrong note's headings and silently
// dropped the half of the link that said where to go.
MICRONOTES_TEST(shell_a_cross_note_anchor_waits_for_the_layout) {
  micronotes::app::UiRuntime ui;
  // Nothing queued: every frame but one, and it must not cost a lookup or
  // touch the status line.
  ui.status = "untouched";
  micronotes::app::applyQueuedAnchorJump(ui);
  MICRONOTES_REQUIRE(ui.status.text == "untouched");

  micronotes::app::queueAnchorJump(ui, "a-section");
  MICRONOTES_REQUIRE(ui.pendingAnchor == "a-section");
  // Spent on the first call, whether or not it found anything -- a note whose
  // anchor is missing must not ask again on every later frame, which would
  // also mean a reader who scrolled away being dragged back.
  micronotes::app::applyQueuedAnchorJump(ui);
  MICRONOTES_REQUIRE(ui.pendingAnchor.empty());
  // And a broken anchor is reported rather than passed over: the note opening
  // at the top with no explanation looks like the anchor was ignored.
  MICRONOTES_REQUIRE(ui.status.text == "Anchor not found: a-section");

  ui.status = "untouched";
  micronotes::app::applyQueuedAnchorJump(ui);
  MICRONOTES_REQUIRE(ui.status.text == "untouched");
}

#include "TestSupport.h"

#include "app/InlineText.h"
#include "app/MarkdownBlocks.h"
#include "app/PageChrome.h"
#include "app/PageView.h"
#include "app/RightPanel.h"
#include "app/Shell.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/BlockScan.h"
#include "ui/TextUtil.h"
#include "app/SidebarModel.h"
#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/WikiLinks.h"
#include "ui/Draw.h"

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

using micronotes::ui::headingAnchor;
using micronotes::app::InlineRun;
using micronotes::app::inlineRuns;
using micronotes::app::searchResultRowHeight;
using micronotes::app::SidebarMetrics;
using micronotes::app::sidebarMetrics;
using micronotes::app::sidebarRowRange;
using micronotes::app::SidebarRow;

namespace {

std::vector<micronotes::markdown::Inline> textInlines(std::string value) {
  micronotes::markdown::Inline item;
  item.type = micronotes::markdown::InlineType::Text;
  item.text = std::move(value);
  return {item};
}

bool sameColor(SDL_Color a, SDL_Color b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

std::string joined(const std::vector<InlineRun>& runs) {
  std::string out;
  for(const auto& run : runs) out += run.text;
  return out;
}

}

// An in-note `[#some heading]` and the heading it points at have to slug to the
// same string or the jump silently does nothing.
MICRONOTES_TEST(shell_anchor_slugs_a_heading_the_way_a_link_spells_it) {
  MICRONOTES_REQUIRE(headingAnchor("Some Heading") == "some-heading");
  MICRONOTES_REQUIRE(headingAnchor("  Leading and trailing  ") == "leading-and-trailing");
  MICRONOTES_REQUIRE(headingAnchor("Punctuation: it's here!") == "punctuation-it-s-here");
  MICRONOTES_REQUIRE(headingAnchor("2 + 2") == "2-2");
  MICRONOTES_REQUIRE(headingAnchor("!!!").empty());
}

// md4c hands `[[Some Note]]` back as literal text. The pane that exists for
// reading a note used to show the raw brackets and offer nothing to click, so
// the runs are split here by the same rule the live surface applies.
MICRONOTES_TEST(shell_inline_runs_split_a_wikilink_out_of_plain_text) {
  const auto runs = inlineRuns(textInlines("before [[Some Note]] after"));
  MICRONOTES_REQUIRE(joined(runs) == "before Some Note after");
  int wikis = 0;
  for(const auto& run : runs) {
    if(!run.wiki) continue;
    ++wikis;
    MICRONOTES_REQUIRE(run.text == "Some Note");
    MICRONOTES_REQUIRE(run.target == "Some Note");
  }
  MICRONOTES_REQUIRE(wikis == 1);
}

MICRONOTES_TEST(shell_inline_runs_ask_whether_a_wikilink_resolves) {
  const auto colourOf = [](std::string_view target, bool exists) {
    const auto runs = inlineRuns(textInlines("see [[" + std::string(target) + "]] here"),
                                 micronotes::ui::theme().textPrimary,
                                 [exists](std::string_view) { return exists; });
    for(const auto& run : runs) {
      if(run.wiki) return run.color;
    }
    return micronotes::ui::theme().textPrimary;
  };
  const auto present = colourOf("Here", true);
  const auto missing = colourOf("Here", false);
  // A link to a note that does not exist yet is drawn differently, the way the
  // live surface draws it -- otherwise the pane says every name resolves.
  MICRONOTES_REQUIRE(!sameColor(present, missing));
}

// Rows tile the list, so the band is two binary searches. Both readers -- the
// draw and the hit test -- go through it, and a disagreement between them is a
// row that draws in one place and clicks in another.
MICRONOTES_TEST(shell_sidebar_row_band_is_the_rows_that_touch_it) {
  std::vector<SidebarRow> rows;
  for(int i = 0; i < 40; ++i) {
    SidebarRow row;
    row.rect = {0.0f, static_cast<float>(i) * 10.0f, 100.0f, 10.0f};
    rows.push_back(row);
  }
  const auto [first, last] = sidebarRowRange(rows, 95.0f, 145.0f);
  MICRONOTES_REQUIRE(first == 9);
  MICRONOTES_REQUIRE(last == 15);
  const auto whole = sidebarRowRange(rows, -100.0f, 10000.0f);
  MICRONOTES_REQUIRE(whole.first == 0);
  MICRONOTES_REQUIRE(whole.second == rows.size());
  const auto none = sidebarRowRange(rows, 10000.0f, 20000.0f);
  MICRONOTES_REQUIRE(none.first == none.second);
}

// A tree of one-line rows at the density an IDE's file tree has, and never
// tighter than the line of text a row holds -- text grows with the reader's
// size setting, so a row nailed to a constant draws its label into the row
// beneath it at the large size.
MICRONOTES_TEST(shell_sidebar_metrics_never_fall_below_the_default_density) {
  const auto small = sidebarMetrics(10, 8);
  MICRONOTES_REQUIRE(small.row >= micronotes::ui::kSidebarRowHeight);
  MICRONOTES_REQUIRE(small.tag >= micronotes::ui::kSidebarRowHeight);
  // A heading over a list keeps the air that separates it from the list before.
  MICRONOTES_REQUIRE(small.label > small.row);
  const auto large = sidebarMetrics(30, 22);
  MICRONOTES_REQUIRE(large.row > small.row);
  MICRONOTES_REQUIRE(large.snippet > small.snippet);
  // And a row is its line of text plus a hair, not plus a whole spacing step:
  // the density is most of what makes a tree read as a file tree, and a step
  // per row is what made a twenty-note folder need scrolling.
  MICRONOTES_REQUIRE(large.row <= 34.0f);
  // A result row has to hold its title and every matching line it lists.
  MICRONOTES_REQUIRE(searchResultRowHeight(3, large) >
                     searchResultRowHeight(1, large));
}

// `PageView` itself, laid out over a real face. It could not be reached from a
// test at all until the shell became a library, which is why the reading pane's
// correctness used to be checked by comparing screenshots.
namespace {

// A page laid out over the vendored faces. Measurement needs SDL_ttf, not a
// window, so a null renderer is enough -- the draw is what needs one, and
// nothing here draws.
struct LaidOutPage {
  micronotes::ui::TextRenderer text {nullptr};
  micronotes::app::PageView page;

  bool ready() {
    return text.fonts().ready();
  }

  void layout(std::string_view source) {
    page.setRevisions(1, 1);
    page.setHeaderHeight(0.0f);
    page.layout(text, source, micronotes::doc::DocumentLayout::kNone, {0.0f, 0.0f, 800.0f, 600.0f});
  }
};

}

// The anchors an in-note `[#heading]` link lands on, which the reading pane used
// to keep in a private map -- so the live surface could not follow one of these
// at all, and the two panes disagreed about what a note contained.
MICRONOTES_TEST(shell_page_records_an_anchor_for_every_heading) {
  LaidOutPage page;
  if(!page.ready()) return;  // no usable face on this machine
  page.layout("# First heading\n\nSome prose.\n\n## Second Heading!\n\nMore prose.\n");

  const auto first = page.page.anchorScroll("first-heading");
  const auto second = page.page.anchorScroll("second-heading");
  MICRONOTES_REQUIRE(first.has_value());
  MICRONOTES_REQUIRE(second.has_value());
  MICRONOTES_REQUIRE(*first == 0);
  MICRONOTES_REQUIRE(*second > *first);
  MICRONOTES_REQUIRE(!page.page.anchorScroll("no-such-heading").has_value());
}

// A footnote definition is reachable by its own label and by its ordinal, which
// is how `[^1]` and `[^first]` both find the same body.
MICRONOTES_TEST(shell_page_records_an_anchor_for_every_footnote) {
  LaidOutPage page;
  if(!page.ready()) return;
  page.layout("Prose with a reference[^alpha] in it.\n\n[^alpha]: The body.\n\n"
              "[^beta]: The second body.\n");
  MICRONOTES_REQUIRE(page.page.anchorScroll("fn-alpha").has_value());
  MICRONOTES_REQUIRE(page.page.anchorScroll("fn-beta").has_value());
  MICRONOTES_REQUIRE(page.page.anchorScroll("fn-1") == page.page.anchorScroll("fn-alpha"));
  MICRONOTES_REQUIRE(page.page.anchorScroll("fn-2") == page.page.anchorScroll("fn-beta"));
}

// A read-only page never reveals a block's markers, whatever the caret says --
// that is the whole of what "reading" means to the layout, and it is what makes
// the reading pane this renderer rather than a second one.
MICRONOTES_TEST(shell_a_read_only_page_never_reveals_a_blocks_markers) {
  const std::string source = "Body **bold** text\n";
  const auto markerInk = [&source](bool readOnly) {
    LaidOutPage page;
    if(!page.ready()) return -1.0f;
    page.page.setReadOnly(readOnly);
    page.page.setRevisions(1, 1);
    page.page.setHeaderHeight(0.0f);
    page.page.layout(page.text, source, 2, {0.0f, 0.0f, 800.0f, 600.0f});
    float total = 0.0f;
    const auto& block = page.page.document().layout(0);
    for(const auto& line : block.lines) {
      for(const auto& run : block.runsOf(line)) {
        if(run.isMarker) total += run.rect.w;
      }
    }
    return total;
  };
  const float editable = markerInk(false);
  if(editable < 0.0f) return;
  MICRONOTES_REQUIRE(editable > 0.0f);
  MICRONOTES_REQUIRE(markerInk(true) == 0.0f);
}

// The md4c parse of a block the live scanner does not model used to be cached
// behind `if(size() > 64) clear()`, so a note with more tables than that made
// room by throwing away the parses it was in the middle of using: a hit rate of
// exactly zero, and every relayout re-parsed the note. The sweep keeps what the
// note still contains instead, so the cap is the note.
MICRONOTES_TEST(shell_complex_parses_survive_a_note_bigger_than_any_cap) {
  std::string source = "# Tables\n\n";
  for(int i = 0; i < 120; ++i) {
    source += "Paragraph " + std::to_string(i) + ".\n\n| Left " + std::to_string(i) +
              " | Right |\n|:--|--:|\n| a" + std::to_string(i) + " | b |\n\n";
  }
  micronotes::app::UiRuntime ui;
  ui.editor.setText(source);
  micronotes::ui::TextRenderer text(nullptr);
  const auto blocks = micronotes::doc::scanBlocks(source);
  std::size_t complexBlocks = 0;
  for(const auto& block : blocks) {
    if(block.kind == micronotes::doc::BlockKind::Complex) ++complexBlocks;
  }
  MICRONOTES_REQUIRE(complexBlocks == 120);

  using microcore::perf::CounterId;
  const auto layOutEveryComplexBlock = [&] {
    const auto before = microcore::perf::captureCounters();
    for(const auto& block : blocks) {
      if(block.kind != micronotes::doc::BlockKind::Complex) continue;
      (void)micronotes::app::measureComplexBlock(text, ui, block, 600.0f);
    }
    micronotes::app::sweepComplexCache(ui, blocks, source);
    const auto after = microcore::perf::captureCounters();
    return after[static_cast<std::size_t>(CounterId::MarkdownParseCalls)] -
           before[static_cast<std::size_t>(CounterId::MarkdownParseCalls)];
  };

  // The first pass has to parse every one of them; no pass after it parses any.
  MICRONOTES_REQUIRE(layOutEveryComplexBlock() == 120);
  MICRONOTES_REQUIRE(layOutEveryComplexBlock() == 0);
  MICRONOTES_REQUIRE(layOutEveryComplexBlock() == 0);
  MICRONOTES_REQUIRE(ui.complexCache.size() == 120);
}

// And the other half: a parse the note no longer contains does not stay
// forever. Opening a second note is the case that matters, because otherwise
// the cache grows with everything a session has ever looked at.
MICRONOTES_TEST(shell_complex_parses_go_when_the_note_does) {
  const auto tableNote = [](const char* tag, int count) {
    std::string out = "# Tables\n\n";
    for(int i = 0; i < count; ++i) {
      out += std::string("| ") + tag + " " + std::to_string(i) + " | Right |\n|:--|--:|\n| a | b |\n\n";
    }
    return out;
  };
  micronotes::app::UiRuntime ui;
  micronotes::ui::TextRenderer text(nullptr);
  const auto layOut = [&](const std::string& source) {
    ui.editor.setText(source);
    const auto blocks = micronotes::doc::scanBlocks(source);
    for(const auto& block : blocks) {
      if(block.kind != micronotes::doc::BlockKind::Complex) continue;
      (void)micronotes::app::measureComplexBlock(text, ui, block, 600.0f);
    }
    micronotes::app::sweepComplexCache(ui, blocks, source);
  };
  layOut(tableNote("first", 100));
  MICRONOTES_REQUIRE(ui.complexCache.size() == 100);
  layOut(tableNote("second", 100));
  // The first note's hundred are gone rather than accumulated.
  MICRONOTES_REQUIRE(ui.complexCache.size() == 100);
}

// An image cache miss must not be able to move `generation()`, because every
// surface holding a laid-out note keys on it: a picture whose size is already
// known, or was never knowable, has to leave the layout alone. A null renderer
// makes every decode fail, which is the "never knowable" half -- the other half
// needs a window, and is measured in a session instead.
MICRONOTES_TEST(shell_an_image_that_cannot_decode_never_moves_the_generation) {
  micronotes::ui::ImageCache images(nullptr);
  const std::uint64_t before = images.generation();
  float w = -1.0f;
  float h = -1.0f;
  for(int attempt = 0; attempt < 8; ++attempt) {
    MICRONOTES_REQUIRE(images.load("/nonexistent/no-such-picture.png", w, h) == nullptr);
  }
  MICRONOTES_REQUIRE(images.generation() == before);
  MICRONOTES_REQUIRE(images.residentBytes() == 0);
  // And it answers with no size, so the layout reserves no box for it.
  MICRONOTES_REQUIRE(w == 0.0f);
  MICRONOTES_REQUIRE(h == 0.0f);
}

// The whole external-change path, end to end, through a real `UiRuntime`: a
// real library, a real watcher, a real editor buffer, and a write that does not
// go through micronotes.
//
// This is the behaviour the app was missing entirely. Editing a note in another
// program left the copy on screen stale and the next autosave wrote it back
// over the top; there was no watcher, and the focus-gained refresh updated the
// index and never the buffer.
MICRONOTES_TEST(shell_reloads_a_watched_note_that_changed_outside_the_app) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-watch";
  std::filesystem::remove_all(root);

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::createNote(ui);
  ui.editor.setText("mine\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  MICRONOTES_REQUIRE(ui.editor.text() == "mine\n");
  const auto path = ui.state.openNote().path;
  const auto noteId = ui.state.selection().noteId;
  MICRONOTES_REQUIRE(ui.watcher.active());

  // The app's own save is reported back by the watcher like any other write --
  // nothing filters it out -- and it must come to nothing: the file matches
  // what was written, so no reload, no re-index, and not even a bump of the
  // library revision the view memos are keyed on. Echo suppression falls out
  // of comparing the disk rather than remembering who wrote it.
  ui.editor.setText("mine, edited\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  const auto revisionAfterSave = ui.state.revision();
  for(int attempt = 0; attempt < 60; ++attempt) {
    micronotes::app::applyWatchedChanges(ui);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  MICRONOTES_REQUIRE(ui.editor.text() == "mine, edited\n");
  MICRONOTES_REQUIRE(!ui.editor.dirty());
  MICRONOTES_REQUIRE(ui.state.revision() == revisionAfterSave);

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
  MICRONOTES_REQUIRE(ui.status.find("Reloaded") != std::string::npos);
  // And the index followed, so search and the sidebar agree with the page.
  ui.state.setSearch("another program");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);

  std::filesystem::remove_all(root);
}

// The other half of the rule: unsaved work is never replaced by what is on
// disk. The save path is what resolves it, and it keeps both versions.
MICRONOTES_TEST(shell_keeps_a_dirty_buffer_when_the_file_changes_outside) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-watch-dirty";
  std::filesystem::remove_all(root);

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::createNote(ui);
  ui.editor.setText("mine\n");
  MICRONOTES_REQUIRE(micronotes::app::saveCurrent(ui));
  const auto path = ui.state.openNote().path;
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
  MICRONOTES_REQUIRE(ui.status.find("was kept as") != std::string::npos);
  MICRONOTES_REQUIRE(ui.state.allNotes().size() == 2);
  ui.state.setSearch("theirs");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);
  ui.state.setSearch("my unsaved draft");
  MICRONOTES_REQUIRE(ui.state.currentNotes().size() == 1);

  std::filesystem::remove_all(root);
}

// The outline panel rebuilds on every keystroke -- it is keyed on the editor's
// revision, and that moves with every typed character. What it must not do on
// every keystroke is derive the block partition again: that is a pass over every
// byte of the note plus a fresh vector, 194 us of the 226 us an outline rebuild
// cost on a 200 KB note, against the ~14 us that keystroke's own layout costs.
//
// The live page splices the partition during its own update, and `editorBlocks`
// lends it. But the borrow only hits if the page has already laid out *this*
// revision -- `blocksAt` refuses to hand over a partition belonging to a buffer
// that has moved, which is exactly what makes it safe.
//
// This covers the borrow and the refusal. It deliberately does NOT cover the
// order `drawApp` calls the two in, because it drives them directly and so
// cannot see it -- and drawn in the wrong order the borrow misses every frame
// with this test still green. `architecture_the_right_panel_is_drawn_after_the_
// content` is what guards that half.
MICRONOTES_TEST(shell_outline_borrows_the_partition_the_live_page_already_spliced) {
  using microcore::perf::CounterId;
  std::string source;
  for(int i = 0; i < 60; ++i) {
    source += "## Heading " + std::to_string(i) + "\n\nSome paragraph text here.\n\n";
  }

  micronotes::app::UiRuntime ui;
  micronotes::ui::TextRenderer text {nullptr};
  ui.editor.setText(source);

  const auto counter = [](CounterId id) {
    return microcore::perf::captureCounters()[static_cast<std::size_t>(id)];
  };
  // What `drawLive` does: stamp the page with the editor's revision plus one --
  // zero means "cannot say" to the layout's reuse check -- and lay it out.
  const auto layOutTheLivePage = [&] {
    ui.livePage.setRevisions(ui.editor.revision() + 1ull, 1);
    ui.livePage.setHeaderHeight(0.0f);
    ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), {0.0f, 0.0f, 800.0f, 600.0f});
  };
  const auto rebuildTheOutline = [&] {
    ui.rightPanel.outlineValid = false;
    const auto scansBefore = counter(CounterId::RightPanelOutlineScans);
    const auto borrowsBefore = counter(CounterId::RightPanelOutlineBlocksBorrowed);
    const std::size_t headings = micronotes::app::outlineFor(ui).size();
    MICRONOTES_REQUIRE(headings == 60);
    return std::pair<std::uint64_t, std::uint64_t> {
      counter(CounterId::RightPanelOutlineScans) - scansBefore,
      counter(CounterId::RightPanelOutlineBlocksBorrowed) - borrowsBefore,
    };
  };

  // Before the page has laid anything out there is nothing to borrow, and the
  // fallback scan is the right answer rather than a failure.
  {
    const auto [scans, borrows] = rebuildTheOutline();
    MICRONOTES_REQUIRE(scans == 1);
    MICRONOTES_REQUIRE(borrows == 0);
  }

  // Once it has, the borrow hits -- and keeps hitting as the buffer moves,
  // which is the steady state while somebody types.
  layOutTheLivePage();
  for(int i = 0; i < 5; ++i) {
    const auto [scans, borrows] = rebuildTheOutline();
    MICRONOTES_REQUIRE(scans == 0);
    MICRONOTES_REQUIRE(borrows == 1);
    ui.editor.moveTo(source.size(), false);
    ui.editor.insert(".");
    layOutTheLivePage();
  }

  // And a buffer the page has not caught up with is refused rather than
  // answered wrongly: this is the case the ordering bug produced every frame.
  ui.editor.insert("!");
  {
    const auto [scans, borrows] = rebuildTheOutline();
    MICRONOTES_REQUIRE(scans == 1);
    MICRONOTES_REQUIRE(borrows == 0);
  }
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
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-showfolder";
  std::filesystem::remove_all(root);
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
  ui.tree.setExpanded(folder, false);
  MICRONOTES_REQUIRE(!ui.tree.expanded(folder));

  const auto* alpha = ui.state.noteById("gi-alpha");
  MICRONOTES_REQUIRE(alpha != nullptr);
  MICRONOTES_REQUIRE(alpha->folder == folder);

  micronotes::app::showFolder(ui, alpha->folder);

  // Both halves: the context moved, and the tree opened.
  MICRONOTES_REQUIRE(ui.state.selection().folder == folder);
  MICRONOTES_REQUIRE(ui.tree.expanded(folder));
  MICRONOTES_REQUIRE(ui.tree.expanded({}));  // and every ancestor of it

  // And the note is now a row inside that folder rather than only in a flat
  // list above it -- which is the thing the user was looking for.
  const auto rows = ui.tree.rows(ui.state.folders(), ui.state.allNotes(), ui.state.libraryRoot());
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
  std::filesystem::remove_all(root);
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
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-newtab";
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
}

// The same through a sidebar row, and the distinction that matters most: a
// *click* on a row opens a tab, and the keyboard *cursor* passing over one does
// not. `moveTreeCursor` activates every row it steps onto, so without this
// holding Down in a thousand-note library would open a thousand tabs.
MICRONOTES_TEST(shell_a_sidebar_click_opens_a_tab_and_the_cursor_does_not) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-rownewtab";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "Notebook");
  for(const char* name : {"One", "Two"}) {
    std::ofstream note(root / "Notebook" / (std::string(name) + ".md"));
    note << "---\nid: rn-" << name << "\ntitle: " << name << "\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));

  const auto noteRow = [](const char* id) {
    SidebarRow row;
    row.kind = SidebarRow::Kind::Tree;
    row.tree.kind = micronotes::ui::TreeRowKind::Note;
    row.tree.noteId = id;
    row.tree.folder = "Notebook";
    return row;
  };

  using micronotes::app::RowActivation;
  micronotes::app::activateSidebarRow(ui, noteRow("rn-One"), RowActivation::Click);
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 1);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "rn-One");

  micronotes::app::activateSidebarRow(ui, noteRow("rn-Two"), RowActivation::Click);
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == 2);
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "rn-Two");
  MICRONOTES_REQUIRE(ui.state.workspace().findTab("rn-One") != std::string::npos);
  // And it still moved the context to the note's folder, which is the other
  // half of opening a note from a row.
  MICRONOTES_REQUIRE(ui.state.selection().folder == std::filesystem::path("Notebook"));

  // Arrowing across both notes leaves the strip where it was: the cursor takes
  // over the tab it is showing in rather than adding to the strip.
  const std::size_t before = ui.state.workspace().tabs.size();
  micronotes::app::activateSidebarRow(ui, noteRow("rn-One"), RowActivation::Cursor);
  micronotes::app::activateSidebarRow(ui, noteRow("rn-Two"), RowActivation::Cursor);
  MICRONOTES_REQUIRE(ui.state.workspace().tabs.size() == before);
  std::filesystem::remove_all(root);
}

// Following a `[[wikilink]]` is the other way into a note, and it took the same
// route through selectNoteById -- so following a link replaced the note the
// link was written in, losing the very context you followed it from.
MICRONOTES_TEST(shell_a_wiki_link_opens_a_tab_and_keeps_its_source) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-wikitab";
  std::filesystem::remove_all(root);
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
  std::filesystem::remove_all(root);
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

#include "TestSupport.h"

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
    page.beginFrame({});
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
    page.page.beginFrame({});
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
  MICRONOTES_REQUIRE(ui.complexParses.size() == 120);
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
  MICRONOTES_REQUIRE(ui.complexParses.size() == 100);
  layOut(tableNote("second", 100));
  // The first note's hundred are gone rather than accumulated.
  MICRONOTES_REQUIRE(ui.complexParses.size() == 100);
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

  std::filesystem::remove_all(root);
}

// The other half of the rule: unsaved work is never replaced by what is on
// disk. The save path is what resolves it, and it keeps both versions.
MICRONOTES_TEST(shell_keeps_a_dirty_buffer_when_the_file_changes_outside) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-watch-dirty";
  std::filesystem::remove_all(root);

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
  // What `drawLive` does: hand the page the frame's inputs -- the editor's own
  // revision, which the page shifts into the space where zero means "cannot
  // say" -- and lay it out.
  const auto layOutTheLivePage = [&] {
    micronotes::app::PageFrame frame;
    frame.sourceRevision = ui.editor.revision();
    ui.livePage.beginFrame(frame);
    ui.livePage.layout(text, ui.editor.text(), ui.editor.cursor(), {0.0f, 0.0f, 800.0f, 600.0f});
  };
  const auto rebuildTheOutline = [&] {
    ui.rightPanel.outline.invalidate();
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

// A tag filter is a one-way door without this.
//
// Choosing a tag replaces the entire row list with the notes carrying it, so
// while one is in force neither the tree nor the tag row that started it is on
// screen to click -- and nothing else clears `selection().tag`. The empty state
// even told the reader to "click the tag again", which was advice about a row
// the filter had just taken away. Both ways out are checked here: Esc's, and a
// second choice of the tag already in force.
MICRONOTES_TEST(shell_a_tag_filter_has_a_way_back_to_the_tree) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-tag-filter";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work");
  {
    std::ofstream out(root / "work" / "plan.md", std::ios::binary | std::ios::trunc);
    out << "---\nid: tf-plan\ntitle: Plan\ntags: work\n---\n\nBody.\n";
  }
  {
    std::ofstream out(root / "loose.md", std::ios::binary | std::ios::trunc);
    out << "---\nid: tf-loose\ntitle: Loose\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));

  micronotes::app::selectTag(ui, "work");
  MICRONOTES_REQUIRE(ui.state.selection().tag == "work");
  // The filter picked the note under it, which is what makes the folder worth
  // restoring: it is not the folder the reader started in.
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "tf-plan");
  MICRONOTES_REQUIRE(ui.state.selection().folder.empty());

  MICRONOTES_REQUIRE(micronotes::app::clearTagFilter(ui));
  MICRONOTES_REQUIRE(ui.state.selection().tag.empty());
  // The note stays open -- leaving the filter is a question about the list, not
  // about what is being read -- and the tree comes back opened onto where that
  // note actually lives rather than at the root.
  MICRONOTES_REQUIRE(ui.state.selection().noteId == "tf-plan");
  MICRONOTES_REQUIRE(ui.state.selection().folder == std::filesystem::path("work"));
  MICRONOTES_REQUIRE(ui.sidebar.tree.expanded(std::filesystem::path("work")));

  // Idempotent, so Esc with no filter running falls through to whatever else
  // Esc means rather than being swallowed.
  MICRONOTES_REQUIRE(!micronotes::app::clearTagFilter(ui));

  // And the second route: choosing the tag already in force toggles it off,
  // which is what the right panel's Tags view -- the one surface that keeps
  // listing tags while a filter runs -- offers.
  micronotes::app::selectTag(ui, "work");
  MICRONOTES_REQUIRE(ui.state.selection().tag == "work");
  micronotes::app::selectTag(ui, "work");
  MICRONOTES_REQUIRE(ui.state.selection().tag.empty());

  std::filesystem::remove_all(root);
}

// One press of Escape undoes one narrowing, innermost first.
//
// The pile of `if`s this replaced fired all of them at once, so a reader who
// had typed a query while a tag filter was running got both cleared by a single
// press and no way to see the middle state -- and the tag filter, which nothing
// else could undo, was not in the pile at all.
MICRONOTES_TEST(shell_escape_undoes_one_narrowing_at_a_time) {
  using micronotes::app::Dismissed;
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-dismiss";
  std::filesystem::remove_all(root);
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

  // A block selection is last: it is a selection inside the page rather than a
  // filter over the library, so it only comes off once nothing is narrowed.
  ui.blockSelection.active = true;
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::BlockSelection);
  MICRONOTES_REQUIRE(!ui.blockSelection.active);
  MICRONOTES_REQUIRE(micronotes::app::dismissOne(ui) == Dismissed::Nothing);

  std::filesystem::remove_all(root);
}

// `[the plan](work/project-plan.md)` is how one note links to another in plain
// Markdown -- it is what every other tool writes and what an imported file
// already contains. Following one used to hand the path to `xdg-open`, so the
// click launched whatever the desktop associates with `.md` and the reader
// watched a second application open a file out of the library they were
// already reading. `[[wikilinks]]` navigated and these did not, which is what
// made the viewer's links look inert.
MICRONOTES_TEST(shell_a_markdown_link_resolves_to_the_note_it_names) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-note-links";
  std::filesystem::remove_all(root);
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

  std::filesystem::remove_all(root);
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

// The sidebar's four groups are bands that can be shut, and a shut band emits
// no rows at all.
//
// This is the geometry half of the fix. The headings used to be four words in
// the panel's own ground on the rows' own label column, with the tree carrying
// no heading whatever -- so where TAGS stopped and RECENT began was something
// the reader inferred from the shape of the entries. A band that hit-tests, a
// count, and rows that actually disappear are what make the division real
// rather than a treatment.
MICRONOTES_TEST(shell_sidebar_sections_are_bands_that_shut) {
  using micronotes::ui::SidebarSection;
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-sections";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work");
  const auto note = [&](const std::filesystem::path& relative, const char* id, const char* title,
                        const char* tags) {
    std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << id << "\ntitle: " << title << "\ntags: " << tags << "\n---\n\nBody.\n";
  };
  note("hub.md", "sc-hub", "Hub", "work fast");
  note("work/plan.md", "sc-plan", "Plan", "work");

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::selectNoteById(ui, "sc-hub");
  ui.sidebar.tree.setExpanded({}, true);
  ui.sidebar.tree.setExpanded(std::filesystem::path("work"), true);

  const micronotes::app::SidebarMetrics metrics = micronotes::app::sidebarMetrics(16, 13);
  const micronotes::ui::Rect list {0.0f, 0.0f, 260.0f, 900.0f};
  const auto rebuild = [&] {
    // The key holds the collapsed set, so shutting a band rebuilds rather than
    // shifting a list laid out at the old heights. Invalidated here because the
    // test drives the model directly rather than through a frame.
    ui.sidebar.rowsKey.valid = false;
    micronotes::app::buildSidebarRows(ui, list, metrics);
  };
  const auto bandFor = [&](SidebarSection section) -> const micronotes::app::SidebarRow* {
    for(const auto& row : ui.sidebar.rows) {
      if(row.kind == micronotes::app::SidebarRow::Kind::SectionLabel && row.section &&
         *row.section == section) {
        return &row;
      }
    }
    return nullptr;
  };
  const auto tagRows = [&] {
    return std::count_if(ui.sidebar.rows.begin(), ui.sidebar.rows.end(), [](const auto& row) {
      return row.kind == micronotes::app::SidebarRow::Kind::Tag;
    });
  };

  rebuild();
  // The tree has a band of its own now. Naming three of four groups is worse
  // than naming none: an unlabelled group between two labelled ones reads as
  // the tail of the one above it.
  const auto* notebooks = bandFor(SidebarSection::Notebooks);
  MICRONOTES_REQUIRE(notebooks != nullptr);
  MICRONOTES_REQUIRE(notebooks->label == "Notebooks");
  // A band spans the panel where the rows it heads are inset, which is what
  // makes it a division of the list rather than a card sitting in it.
  MICRONOTES_REQUIRE(notebooks->rect.x == list.x);
  MICRONOTES_REQUIRE(notebooks->rect.w == list.w);
  // And it carries a count, which is worth most on a band that is shut -- the
  // one case where what is under it cannot be counted by looking.
  MICRONOTES_REQUIRE(notebooks->trailing == "2");
  // A collapsible band wears the same disclosure control a folder row does.
  MICRONOTES_REQUIRE(notebooks->disclosure.w > 0.0f);
  MICRONOTES_REQUIRE(!notebooks->collapsed);

  const auto* tags = bandFor(SidebarSection::Tags);
  MICRONOTES_REQUIRE(tags != nullptr);
  MICRONOTES_REQUIRE(tags->trailing == "2");   // work, fast
  MICRONOTES_REQUIRE(tagRows() == 2);

  // Shut it: the rows go, the band stays, and the count stays with it.
  ui.state.editWorkspace().setSectionCollapsed(SidebarSection::Tags, true);
  rebuild();
  MICRONOTES_REQUIRE(tagRows() == 0);
  MICRONOTES_REQUIRE(bandFor(SidebarSection::Tags) != nullptr);
  MICRONOTES_REQUIRE(bandFor(SidebarSection::Tags)->collapsed);
  MICRONOTES_REQUIRE(bandFor(SidebarSection::Tags)->trailing == "2");
  // Everything below it moves up, which is the point of shutting it.
  MICRONOTES_REQUIRE(bandFor(SidebarSection::Recent) != nullptr);
  MICRONOTES_REQUIRE(bandFor(SidebarSection::Recent)->rect.y <
                     bandFor(SidebarSection::Tags)->rect.y + 200.0f);

  // A band is findable under the pointer, because the whole band is its
  // control. `sidebarRowAt` used to skip every section label, which is why the
  // old headings could not have been made clickable.
  const auto* band = bandFor(SidebarSection::Tags);
  const auto hit = micronotes::app::sidebarRowAt(ui, band->rect.x + band->rect.w / 2.0f,
                                                 band->rect.y + band->rect.h / 2.0f);
  MICRONOTES_REQUIRE(hit.has_value());
  MICRONOTES_REQUIRE(ui.sidebar.rows[*hit].section.has_value());
  // And activating it toggles, from anywhere on the band rather than only on
  // the 12px triangle.
  micronotes::app::activateSidebarRow(ui, ui.sidebar.rows[*hit],
                                      micronotes::app::RowActivation::Click);
  MICRONOTES_REQUIRE(!ui.state.workspace().sectionCollapsed(SidebarSection::Tags));

  // A *caption* is not a control: a result count and the name of the tag being
  // filtered by head the list the same way but have nothing under them to shut,
  // so they get no chevron and the pointer finds nothing there.
  micronotes::app::selectTag(ui, "work");
  rebuild();
  bool sawCaption = false;
  for(std::size_t i = 0; i < ui.sidebar.rows.size(); ++i) {
    const auto& row = ui.sidebar.rows[i];
    if(row.kind != micronotes::app::SidebarRow::Kind::SectionLabel) continue;
    sawCaption = true;
    MICRONOTES_REQUIRE(!row.section.has_value());
    MICRONOTES_REQUIRE(row.disclosure.w == 0.0f);
    // And with no `#` in front of it: the sidebar draws a tag's colour beside
    // its name everywhere else, so a sigil as well says the same thing twice.
    MICRONOTES_REQUIRE(row.label == "work");
    const auto missed = micronotes::app::sidebarRowAt(ui, row.rect.x + 4.0f,
                                                      row.rect.y + row.rect.h / 2.0f);
    MICRONOTES_REQUIRE(!missed || *missed != i);
  }
  MICRONOTES_REQUIRE(sawCaption);

  std::filesystem::remove_all(root);
}

// The dots at a note row's trailing edge, which are what join the row to the
// TAGS band: the row named a folder and said nothing about the tags on it, so
// the one way of organising a library that cuts across the tree was invisible
// from the tree.
MICRONOTES_TEST(shell_tag_dots_are_laid_out_once_for_the_draw_and_the_hit_test) {
  const micronotes::ui::Rect row {8.0f, 100.0f, 240.0f, 20.0f};
  MICRONOTES_REQUIRE(micronotes::app::tagDotRects(row, 0, 0.0f).empty());

  const auto one = micronotes::app::tagDotRects(row, 1, 0.0f);
  MICRONOTES_REQUIRE(one.size() == 1);
  // Inside the row, and clear of its trailing edge.
  MICRONOTES_REQUIRE(one[0].x + one[0].w <= row.x + row.w);
  MICRONOTES_REQUIRE(one[0].y >= row.y && one[0].y + one[0].h <= row.y + row.h);

  // Laid out right to left, so a note with one tag puts its dot where a note
  // with four puts its last: the column reads as a column whatever is in it,
  // rather than shifting with each row's tag count.
  const auto four = micronotes::app::tagDotRects(row, 4, 0.0f);
  MICRONOTES_REQUIRE(four.size() == 4);
  MICRONOTES_REQUIRE(four.back().x == one[0].x);
  // Returned in tag order even though they are placed in reverse, so no caller
  // has to reverse an index.
  for(std::size_t i = 1; i < four.size(); ++i) {
    MICRONOTES_REQUIRE(four[i].x > four[i - 1].x);
  }
  // And they do not overlap, or two dots would be one target.
  for(std::size_t i = 1; i < four.size(); ++i) {
    MICRONOTES_REQUIRE(four[i].x >= four[i - 1].x + four[i - 1].w);
  }

  // Capped. A row is one line tall, and a note with nine tags would otherwise
  // push its own name off the panel.
  const auto many = micronotes::app::tagDotRects(row, 9, 0.0f);
  MICRONOTES_REQUIRE(many.size() == micronotes::app::kMaxTagDots);
  // The column the row reserves has to hold what the layout puts in it.
  MICRONOTES_REQUIRE(many.front().x >= row.x + row.w - micronotes::app::kTagDotColumnWidth);

  // A scrollbar's lane is the panel's, not the row's. Every dot moves clear of
  // it by exactly the reserve, which is what stops the column being painted
  // under the thumb -- the dots were the worst of three trailing placements
  // that ignored it, sitting 4px from an edge the bar covers 12px of.
  const float reserve = 14.0f;
  const auto reserved = micronotes::app::tagDotRects(row, 4, reserve);
  MICRONOTES_REQUIRE(reserved.size() == four.size());
  for(std::size_t i = 0; i < reserved.size(); ++i) {
    MICRONOTES_REQUIRE(reserved[i].x == four[i].x - reserve);
  }
  MICRONOTES_REQUIRE(reserved.back().x + reserved.back().w <= row.x + row.w - reserve);
}

// And the dot answers a click, which is the half that makes it a control rather
// than decoration.
MICRONOTES_TEST(shell_a_tag_dot_names_the_tag_under_the_pointer) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-tag-dots";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream out(root / "hub.md", std::ios::binary | std::ios::trunc);
    out << "---\nid: td-hub\ntitle: Hub\ntags: alpha beta gamma\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  micronotes::app::selectNoteById(ui, "td-hub");
  ui.sidebar.tree.setExpanded({}, true);

  const micronotes::app::SidebarMetrics metrics = micronotes::app::sidebarMetrics(16, 13);
  const micronotes::ui::Rect list {0.0f, 0.0f, 260.0f, 900.0f};
  ui.sidebar.rowsKey.valid = false;
  micronotes::app::buildSidebarRows(ui, list, metrics);

  const micronotes::app::SidebarRow* noteRow = nullptr;
  for(const auto& row : ui.sidebar.rows) {
    if(row.kind == micronotes::app::SidebarRow::Kind::Tree &&
       row.tree.kind == micronotes::ui::TreeRowKind::Note) {
      noteRow = &row;
      break;
    }
  }
  MICRONOTES_REQUIRE(noteRow != nullptr);

  // The same reserve the hit test below reads, which is what the pair is for.
  const auto dots =
    micronotes::app::tagDotRects(noteRow->rect, 3, ui.sidebar.trailingReserve);
  MICRONOTES_REQUIRE(dots.size() == 3);
  // Each dot names its own tag, in the order the note lists them.
  const char* expected[] = {"alpha", "beta", "gamma"};
  for(std::size_t i = 0; i < dots.size(); ++i) {
    const auto tag = micronotes::app::sidebarTagDotAt(ui, *noteRow,
                                                      dots[i].x + dots[i].w / 2.0f,
                                                      dots[i].y + dots[i].h / 2.0f);
    MICRONOTES_REQUIRE(tag.has_value());
    micronotes::tests::require(*tag == expected[i],
                               "dot " + std::to_string(i) + " named " + *tag + ", not " +
                                 expected[i]);
  }
  // The label is not a dot: a click on the note's name opens the note.
  MICRONOTES_REQUIRE(!micronotes::app::sidebarTagDotAt(ui, *noteRow, noteRow->rect.x + 40.0f,
                                                       noteRow->rect.y + 10.0f));

  std::filesystem::remove_all(root);
}

// The bands are reachable from the keyboard, which is the half that was missing
// when the collapsing was written: `moveTreeCursor` stepped over every section
// label, so nothing but a pointer could shut one.
MICRONOTES_TEST(shell_the_keyboard_reaches_the_sidebar_bands) {
  using micronotes::ui::SidebarSection;
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-band-keys";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream out(root / "hub.md", std::ios::binary | std::ios::trunc);
    out << "---\nid: bk-hub\ntitle: Hub\ntags: work\n---\n\nBody.\n";
  }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  ui.sidebar.tree.setExpanded({}, true);
  const micronotes::app::SidebarMetrics metrics = micronotes::app::sidebarMetrics(16, 13);
  const micronotes::ui::Rect list {0.0f, 0.0f, 260.0f, 900.0f};
  const auto rebuild = [&] {
    ui.sidebar.rowsKey.valid = false;
    micronotes::app::buildSidebarRows(ui, list, metrics);
  };
  rebuild();
  ui.sidebar.rect = list;

  const auto indexOfBand = [&](SidebarSection section) {
    for(std::size_t i = 0; i < ui.sidebar.rows.size(); ++i) {
      const auto& row = ui.sidebar.rows[i];
      if(row.section && *row.section == section) return static_cast<int>(i);
    }
    return -1;
  };

  // The cursor stops on a band...
  const int tagsBand = indexOfBand(SidebarSection::Tags);
  MICRONOTES_REQUIRE(tagsBand >= 0);
  ui.sidebar.cursor = tagsBand;
  // ...and Left shuts it, Right opens it, exactly as they do for a folder --
  // which is what it looks like, since both wear the same chevron.
  micronotes::app::expandTreeCursor(ui, /*open=*/false);
  MICRONOTES_REQUIRE(ui.state.workspace().sectionCollapsed(SidebarSection::Tags));
  rebuild();
  ui.sidebar.cursor = indexOfBand(SidebarSection::Tags);
  micronotes::app::expandTreeCursor(ui, /*open=*/true);
  MICRONOTES_REQUIRE(!ui.state.workspace().sectionCollapsed(SidebarSection::Tags));

  // But arrowing *past* a band must not shut it: the cursor lands there and
  // waits, so a walk down the list does not collapse the library on the way.
  rebuild();
  ui.sidebar.cursor = 0;
  for(int step = 0; step < static_cast<int>(ui.sidebar.rows.size()) + 2; ++step) {
    micronotes::app::moveTreeCursor(ui, 1);
  }
  std::size_t sectionCount = 0;
  const auto* sections = micronotes::ui::sidebarSections(&sectionCount);
  for(std::size_t i = 0; i < sectionCount; ++i) {
    micronotes::tests::require(
      !ui.state.workspace().sectionCollapsed(sections[i]),
      std::string("arrowing past the ") + std::string(micronotes::ui::sidebarSectionName(sections[i])) +
        " band shut it");
  }

  std::filesystem::remove_all(root);
}

// "Copy relative path" / "Copy absolute path" / "Show on disk", ported from the
// sibling microide, whose file tree and tab strip both carry them.
//
// The relative one is the one worth having and the one with a way to be wrong:
// it is the spelling that means the same thing to somebody else looking at the
// same library, so it has to be relative to the library root and it has to
// refuse rather than quietly hand back an absolute path when it cannot be.
MICRONOTES_TEST(shell_a_note_reports_both_spellings_of_its_path) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-note-paths";
  std::filesystem::remove_all(root);
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

  std::filesystem::remove_all(root);
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

namespace {

// A sidebar row list built for a fixed panel, so a test can find rows by kind.
void buildTestSidebar(micronotes::app::UiRuntime& ui) {
  micronotes::app::buildSidebarRows(ui, {0.0f, 0.0f, 320.0f, 900.0f}, sidebarMetrics(16, 12));
}

const SidebarRow* findTreeRow(const micronotes::app::UiRuntime& ui, micronotes::ui::TreeRowKind kind,
                              const std::filesystem::path& file) {
  for(const auto& row : ui.sidebar.rows) {
    if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == kind && row.tree.file == file) return &row;
  }
  return nullptr;
}

}

// A companion file opens with the desktop when it is asked for -- a click, or
// Enter -- and never when the keyboard cursor merely passes over it. Neither
// touches the selection: a file has no page, so the note on screen stays.
MICRONOTES_TEST(shell_a_companion_opens_on_a_click_and_never_on_the_cursor) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-companion-open";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work" / "files");
  { std::ofstream note(root / "work" / "Alpha.md"); note << "---\nid: c-alpha\ntitle: Alpha\n---\n\nBody.\n"; }
  { std::ofstream pdf(root / "work" / "files" / "diagram.png"); pdf << "png"; }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  int launched = 0;
  std::filesystem::path last;
  ui.launcher = [&](const std::filesystem::path& path) {
    ++launched;
    last = path;
    return true;
  };
  ui.sidebar.tree.reveal("work/files");
  buildTestSidebar(ui);

  const auto* file = findTreeRow(ui, micronotes::ui::TreeRowKind::File, "work/files/diagram.png");
  MICRONOTES_REQUIRE(file != nullptr);
  const auto* filesDir = findTreeRow(ui, micronotes::ui::TreeRowKind::FilesFolder, "work/files");
  MICRONOTES_REQUIRE(filesDir != nullptr);
  const SidebarRow fileRow = *file;
  const SidebarRow dirRow = *filesDir;

  micronotes::app::activateSidebarRow(ui, fileRow, micronotes::app::RowActivation::Cursor);
  MICRONOTES_REQUIRE(launched == 0);
  MICRONOTES_REQUIRE(ui.state.selection().noteId.empty());

  micronotes::app::activateSidebarRow(ui, fileRow, micronotes::app::RowActivation::Click);
  MICRONOTES_REQUIRE(launched == 1);
  MICRONOTES_REQUIRE(last == root / "work" / "files" / "diagram.png");
  MICRONOTES_REQUIRE(ui.status.text == "Opened diagram.png");
  MICRONOTES_REQUIRE(ui.state.selection().noteId.empty());
  MICRONOTES_REQUIRE(ui.state.selection().folder.empty());

  // The files directory unfolds and folds, and becomes nothing.
  MICRONOTES_REQUIRE(ui.sidebar.tree.expanded("work/files"));
  micronotes::app::activateSidebarRow(ui, dirRow, micronotes::app::RowActivation::Click);
  MICRONOTES_REQUIRE(!ui.sidebar.tree.expanded("work/files"));
  MICRONOTES_REQUIRE(ui.state.selection().folder.empty());
  MICRONOTES_REQUIRE(launched == 1);

  // A drop on either kind names the directory it stands for, and a drop on a
  // note names its notebook with no files directory at all.
  const auto centre = [](const SidebarRow& row) {
    return std::pair {row.rect.x + row.rect.w / 2.0f, row.rect.y + row.rect.h / 2.0f};
  };
  const auto [fx, fy] = centre(fileRow);
  const auto onFile = micronotes::app::sidebarDropTargetAt(ui, fx, fy);
  MICRONOTES_REQUIRE(onFile.valid && onFile.filesDir == std::filesystem::path("work/files"));
  const auto [dx, dy] = centre(dirRow);
  const auto onDir = micronotes::app::sidebarDropTargetAt(ui, dx, dy);
  MICRONOTES_REQUIRE(onDir.valid && onDir.filesDir == std::filesystem::path("work/files"));
  const SidebarRow* note = nullptr;
  for(const auto& row : ui.sidebar.rows) {
    if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == micronotes::ui::TreeRowKind::Note) note = &row;
  }
  MICRONOTES_REQUIRE(note != nullptr);
  const auto [nx, ny] = centre(*note);
  const auto onNote = micronotes::app::sidebarDropTargetAt(ui, nx, ny);
  MICRONOTES_REQUIRE(onNote.valid && onNote.folder == std::filesystem::path("work") && onNote.filesDir.empty());
  std::filesystem::remove_all(root);
}

// A query lists the files whose *name* matched under a caption of their own,
// and only when there are any.
MICRONOTES_TEST(shell_search_lists_matching_file_names_under_their_own_caption) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-companion-search";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work" / "files");
  { std::ofstream note(root / "work" / "Alpha.md"); note << "---\nid: s-alpha\ntitle: Alpha\n---\n\ndiagram in the body\n"; }
  { std::ofstream pdf(root / "work" / "files" / "diagram.png"); pdf << "png"; }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  ui.fields.search.beginWith("diagram", false);
  ui.state.setSearch("diagram");
  buildTestSidebar(ui);

  bool notesCaption = false;
  bool filesCaption = false;
  bool fileRow = false;
  for(const auto& row : ui.sidebar.rows) {
    if(row.kind == SidebarRow::Kind::SectionLabel && row.label == "1 result") notesCaption = true;
    if(row.kind == SidebarRow::Kind::SectionLabel && row.label == "1 file") filesCaption = true;
    if(row.kind == SidebarRow::Kind::Tree && row.tree.kind == micronotes::ui::TreeRowKind::File &&
       row.tree.file == std::filesystem::path("work/files/diagram.png")) {
      fileRow = true;
    }
  }
  MICRONOTES_REQUIRE(notesCaption && filesCaption && fileRow);

  // A query only a note answers has no files caption at all.
  ui.fields.search.beginWith("body", false);
  ui.state.setSearch("body");
  buildTestSidebar(ui);
  for(const auto& row : ui.sidebar.rows) {
    MICRONOTES_REQUIRE(!(row.kind == SidebarRow::Kind::SectionLabel && row.label.ends_with(" file")));
    MICRONOTES_REQUIRE(!(row.kind == SidebarRow::Kind::Tree && row.tree.kind == micronotes::ui::TreeRowKind::File));
  }
  std::filesystem::remove_all(root);
}

// A file copied into a `files/` directory from outside appears in the tree
// through one walk of that directory -- not through the library refresh a
// folder operation costs.
MICRONOTES_TEST(shell_a_file_dropped_into_files_refreshes_only_that_directory) {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-shell-companion-watch";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work" / "files");
  { std::ofstream note(root / "work" / "Alpha.md"); note << "---\nid: w-alpha\ntitle: Alpha\n---\n\nBody.\n"; }

  micronotes::app::UiRuntime ui;
  MICRONOTES_REQUIRE(micronotes::app::openLibraryRoot(ui, root));
  MICRONOTES_REQUIRE(ui.watcher.active());
  MICRONOTES_REQUIRE(ui.state.catalog().companions().size() == 1);

  microcore::perf::resetCounters();
  { std::ofstream pdf(root / "work" / "files" / "new.pdf"); pdf << "pdf"; }
  bool applied = false;
  for(int attempt = 0; attempt < 400 && !applied; ++attempt) {
    applied = micronotes::app::applyWatchedChanges(ui);
    if(!applied) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  MICRONOTES_REQUIRE(applied);
  bool listed = false;
  for(const auto& entry : ui.state.catalog().companions()) {
    if(entry.path == std::filesystem::path("work/files/new.pdf")) listed = true;
  }
  MICRONOTES_REQUIRE(listed);
  using microcore::perf::CounterId;
  MICRONOTES_REQUIRE(microcore::perf::readCounter(CounterId::LibraryFilesDirRefreshes) >= 1);
  MICRONOTES_REQUIRE(microcore::perf::readCounter(CounterId::LibraryIndexRefreshCalls) == 0);
  std::filesystem::remove_all(root);
}

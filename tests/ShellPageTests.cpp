#include "TestSupport.h"

#include "app/MarkdownBlocks.h"
#include "app/RightPanel.h"
#include "app/Shell.h"
#include "ui/ImageCache.h"

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


// The page surface: the anchors it records, the markers it reveals, the md4c
// parses it caches, and the block partition the outline panel borrows from it.

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
    page.layout(text, source, {0.0f, 0.0f, 800.0f, 600.0f});
  }
};

}

// The anchors an in-note `[#heading]` link lands on, which the reading pane used
// to keep in a private map -- so the two panes disagreed about what a note
// contained.
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

// The page never shows a block's markers as text. They keep their offsets --
// which is what lets a selection cover them and a click land between them and
// the word -- and take no width at all.
MICRONOTES_TEST(shell_a_page_never_reveals_a_blocks_markers) {
  LaidOutPage page;
  if(!page.ready()) return;
  page.layout("Body **bold** text\n");
  float markerInk = 0.0f;
  std::size_t markers = 0;
  const auto& block = page.page.document().layout(0);
  for(const auto& line : block.lines) {
    for(const auto& run : block.runsOf(line)) {
      if(!run.isMarker) continue;
      ++markers;
      markerInk += run.rect.w;
    }
  }
  MICRONOTES_REQUIRE(markers > 0);
  MICRONOTES_REQUIRE(markerInk == 0.0f);
}

// The md4c parse of a block the block scanner does not model used to be cached
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
  MICRONOTES_REQUIRE(ui.complexRenders.size() == 120);
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
  MICRONOTES_REQUIRE(ui.complexRenders.size() == 100);
  layOut(tableNote("second", 100));
  // The first note's hundred are gone rather than accumulated.
  MICRONOTES_REQUIRE(ui.complexRenders.size() == 100);
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

// The outline panel rebuilds on every keystroke -- it is keyed on the editor's
// revision, and that moves with every typed character. What it must not do on
// every keystroke is derive the block partition again: that is a pass over every
// byte of the note plus a fresh vector, 194 us of the 226 us an outline rebuild
// cost on a 200 KB note, against the ~14 us that keystroke's own layout costs.
//
// The reading page splices the partition during its own update, and `editorBlocks`
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
  // What `drawReading` does: hand the page the frame's inputs -- the editor's
  // own revision, which the page shifts into the space where zero means
  // "cannot say" -- and lay it out.
  const auto layOutThePage = [&] {
    micronotes::app::PageFrame frame;
    frame.sourceRevision = ui.editor.revision();
    ui.readingPage.beginFrame(frame);
    ui.readingPage.layout(text, ui.editor.text(), {0.0f, 0.0f, 800.0f, 600.0f});
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
  layOutThePage();
  for(int i = 0; i < 5; ++i) {
    const auto [scans, borrows] = rebuildTheOutline();
    MICRONOTES_REQUIRE(scans == 0);
    MICRONOTES_REQUIRE(borrows == 1);
    ui.editor.moveTo(source.size(), false);
    ui.editor.insert(".");
    layOutThePage();
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

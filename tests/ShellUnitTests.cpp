#include "TestSupport.h"

#include "app/InlineText.h"
#include "app/MarkdownBlocks.h"
#include "app/PageView.h"
#include "app/Shell.h"
#include "core/perf/PerformanceCounters.h"
#include "doc/BlockScan.h"
#include "ui/TextUtil.h"
#include "app/SidebarModel.h"
#include "ui/Draw.h"

#include <string>
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
                                 micronotes::ui::theme().text,
                                 [exists](std::string_view) { return exists; });
    for(const auto& run : runs) {
      if(run.wiki) return run.color;
    }
    return micronotes::ui::theme().text;
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

// Chrome stays put while text grows with the reader's size setting, so a row
// nailed to 26 pixels drew its label into the row beneath it at the large size.
MICRONOTES_TEST(shell_sidebar_metrics_never_fall_below_the_medium_size) {
  const auto small = sidebarMetrics(10, 8);
  MICRONOTES_REQUIRE(small.row >= 26.0f);
  MICRONOTES_REQUIRE(small.tag >= 24.0f);
  MICRONOTES_REQUIRE(small.label >= 34.0f);
  const auto large = sidebarMetrics(30, 22);
  MICRONOTES_REQUIRE(large.row > small.row);
  MICRONOTES_REQUIRE(large.snippet > small.snippet);
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

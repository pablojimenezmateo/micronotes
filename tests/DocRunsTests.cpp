#include "TestSupport.h"
#include "LayoutFixture.h"

#include "doc/Layout.h"
#include "ui/DocRuns.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using micronotes::doc::DocumentLayout;
using micronotes::doc::LayoutOptions;
using micronotes::tests::stubMetrics;

// The tinted ground behind an inline code span, as the two run painters ask
// for it.

namespace {

struct Band {
  float x = 0.0f;
  float width = 0.0f;
};

// Every band on the first line of the first block, which is all these
// fixtures have.
std::vector<Band> bandsOf(const std::string& source) {
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 800.0f;
  layout.update(source, options);
  std::vector<Band> bands;
  for(std::size_t i = 0; i < layout.blockCount(); ++i) {
    const auto& block = layout.layout(i);
    for(const auto& line : block.lines) {
      micronotes::ui::forEachCodeSpan(block.runsOf(line), [&bands](float x, float width) {
        bands.push_back({x, width});
      });
    }
  }
  return bands;
}

}

// The bug this walk exists for. `` `202 Accepted` `` is three runs -- the two
// words and the space between them -- and each ground used to be filled from
// inside the run loop, inflated two pixels sideways so the pieces met. That
// inflation landed on top of the run before it, and a glyph whose ink reaches
// past its advance lost the overhang: the last digit of `202` came out two
// columns short, on screen and in an exported PDF alike.
MICRONOTES_TEST(doc_runs_gives_a_multi_word_code_span_one_band) {
  const auto bands = bandsOf("`202 Accepted` and more\n");
  MICRONOTES_REQUIRE(bands.size() == 1);
  MICRONOTES_REQUIRE(bands.front().width > 0.0f);
}

// The band covers the whole span and no more: it starts where the span's first
// run starts and ends where its last one ends, so the inflation each painter
// adds is the only thing outside it.
MICRONOTES_TEST(doc_runs_band_spans_the_whole_code_run_and_stops_there) {
  DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  LayoutOptions options;
  options.width = 800.0f;
  layout.update("word `202 Accepted` tail\n", options);

  const auto& block = layout.layout(0);
  float first = -1.0f;
  float last = -1.0f;
  for(const auto& line : block.lines) {
    for(const auto& run : block.runsOf(line)) {
      if(run.role != micronotes::doc::TextRole::Code || run.isMarker || run.text.empty()) continue;
      if(first < 0.0f) first = run.rect.x;
      last = run.rect.x + run.rect.w;
    }
  }
  MICRONOTES_REQUIRE(first >= 0.0f);

  const auto bands = bandsOf("word `202 Accepted` tail\n");
  MICRONOTES_REQUIRE(bands.size() == 1);
  MICRONOTES_REQUIRE(std::abs(bands.front().x - first) < 0.01f);
  MICRONOTES_REQUIRE(std::abs(bands.front().x + bands.front().width - last) < 0.01f);
}

// Two spans on one line stay two bands. Merging everything between the first
// and the last code run would tint the prose between them.
MICRONOTES_TEST(doc_runs_keeps_two_code_spans_apart) {
  const auto bands = bandsOf("`one` between `two`\n");
  MICRONOTES_REQUIRE(bands.size() == 2);
  MICRONOTES_REQUIRE(bands[0].x + bands[0].width < bands[1].x);
}

// A line with no code span asks for no fill at all.
MICRONOTES_TEST(doc_runs_asks_for_no_band_without_a_code_span) {
  MICRONOTES_REQUIRE(bandsOf("plain words only\n").empty());
}

#include "perf/Harness.h"
#include "perf/Lanes.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "app/PageView.h"
#include "doc/Layout.h"
#include "ui/TextRenderer.h"

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>


namespace micronotes::perfharness {

// ---------------------------------------------------------------------------
// The font lane.
//
// Every budget above measures against `stubMetrics()`, a fixed-advance stand-in,
// because the core library has no fonts in it. That is the right call for a
// core-level benchmark and it is also why the largest cost in the app -- glyph
// shaping to open a note -- was invisible here for four passes and only showed
// up in a real session.
//
// It is measurable now because the shell is a library: `documentMetrics` is the
// same measure and line height `PageView` lays a note out with, over the same
// `ui::TextRenderer`, reading the same vendored faces. What is still not here
// is a *draw* -- no window, no textures, no present -- so this lane says what
// shaping and measuring cost and says nothing about rasterizing. That is the
// half that was missing: `render.text_measure_calls` and its hit rate are the
// numbers a layout change moves, and they were unobservable outside a session.
//
// Deliberately loose budgets, and more loosely still than the stubbed ones: a
// shaping pass is the scenario here whose cost moves most with the machine --
// 65 ms on an idle box and 200 ms on one running a parallel build, measured
// with the same binary and the same CPU clock. The point of the lane is to
// catch a change that makes shaping several times worse; the counters beside it
// are the tight numbers, and they are deterministic.
static constexpr std::uint64_t kFontColdBudgetMicros = 60000;
static constexpr std::uint64_t kFontShapingBudgetMicros = 400000;
static constexpr std::uint64_t kFontKeystrokeBudgetMicros = 20000;

// A note whose every word is different.
//
// The 200 KB fixture the other lanes share is written by a loop, so it repeats
// a few thousand distinct words several thousand times -- and a cold open of it
// is 99.2% measure-cache hits, which means it says almost nothing about
// shaping. Real prose is not that repetitive and neither is this: each word is
// a base-36 counter padded to a varied length, so every measurement is a miss
// and the lane measures the face rather than the hash table in front of it.
static std::string uniqueWordMarkdown(std::size_t bytes, int seed) {
  static const char kDigits[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string out;
  out.reserve(bytes + 256);
  std::size_t counter = static_cast<std::size_t>(seed) * 4'000'000ull;
  int section = 0;
  while(out.size() < bytes) {
    out += "## Section ";
    out += std::to_string(section++);
    out += "\n\n";
    for(int line = 0; line < 12; ++line) {
      for(int word = 0; word < 11; ++word) {
        if(word != 0) out.push_back(' ');
        std::size_t value = ++counter;
        // Three to eight characters, so the word lengths vary the way prose
        // does and one cached width cannot stand in for another.
        const int length = 3 + static_cast<int>(counter % 6);
        for(int c = 0; c < length; ++c) {
          out.push_back(kDigits[value % 36]);
          value /= 36;
        }
      }
      out.push_back('\n');
    }
    out += "\n";
  }
  return out;
}

bool fontBudgets(const std::string& base) {
  micronotes::ui::TextRenderer text(nullptr);
  if(!text.fonts().ready()) {
    std::cout << "\n=== the real font path === (skipped: no usable face on this machine)\n";
    return true;
  }
  std::cout << "\n=== the real font path (" << text.fonts().sourceDescription() << ") ===\n";

  const auto freshMetrics = [&text] {
    auto metrics = micronotes::app::documentMetrics(text);
    // A `Complex` block is drawn by md4c, which this lane does not build. Zero
    // is what an unwired page already answers, so the note lays out with those
    // blocks empty rather than not at all.
    metrics.measureComplex = [](const micronotes::doc::SourceBlock&, float) { return 0.0f; };
    return metrics;
  };

  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  options.type = micronotes::app::documentTypeMetrics();

  bool ok = true;
  const auto gate = [&ok](const char* name, const Cost& cost, std::uint64_t budget) {
    if(cost.medianMicros <= budget) return;
    std::cerr << "BUDGET FAILED: " << name << " " << cost.medianMicros << "us exceeds " << budget
              << "us\n";
    ok = false;
  };

  // Opening the note with a cold measure cache each time -- which is what
  // starting the app does, and the one place shaping is the whole cost.
  const auto before = microcore::perf::captureCounters();
  gate("font.open_cold_layout",
       measureIterations("font.open_cold_layout", 4,
                         [&](int i) {
                           micronotes::doc::DocumentLayout fresh;
                           fresh.setMetrics(freshMetrics());
                           micronotes::doc::LayoutOptions cold = options;
                           cold.width = 700.0f + static_cast<float>(i);
                           fresh.update(base, cold);
                         }),
       kFontColdBudgetMicros);
  const auto afterCold = microcore::perf::captureCounters();

  // The same open over prose the cache cannot absorb. This is the shaping
  // number: every word is measured once and no second word has its width.
  //
  // A different note per pass, because the measure cache belongs to the
  // renderer and outlives the layout: run one note four times and only the
  // first pass shapes anything, which is a 75% hit rate and three quarters of
  // the answer missing. Counted rather than indexed by `i`, because
  // `measureIterations` makes an untimed warm-up call that would otherwise
  // hand the first timed pass a note already in the cache.
  std::vector<std::string> uniqueNotes;
  for(int note = 0; note < 5; ++note) uniqueNotes.push_back(uniqueWordMarkdown(200 * 1024, note));
  std::size_t pass = 0;
  gate("font.open_unique_words",
       measureIterations("font.open_unique_words", 4,
                         [&](int i) {
                           micronotes::doc::DocumentLayout fresh;
                           fresh.setMetrics(freshMetrics());
                           micronotes::doc::LayoutOptions cold = options;
                           cold.width = 700.0f + static_cast<float>(i);
                           fresh.update(uniqueNotes[pass++ % uniqueNotes.size()], cold);
                         }),
       kFontShapingBudgetMicros);
  const auto afterUnique = microcore::perf::captureCounters();

  // And a keystroke into it, where the cache is warm and the measure calls are
  // a hash and a probe. This is the number that says whether a layout change
  // moved the *shaping* or only the arithmetic around it.
  std::string source = base;
  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(freshMetrics());
  layout.update(source, options);
  std::size_t caret = source.find("paragraph", source.size() / 2);
  if(caret == std::string::npos) caret = source.size() / 2;
  gate("font.type_middle", measureIterations("font.type_middle", 24,
                                             [&](int i) {
                                               source.insert(caret + static_cast<std::size_t>(i), 1, 'x');
                                               layout.update(source, options);
                                             }),
       kFontKeystrokeBudgetMicros);
  const auto afterType = microcore::perf::captureCounters();

  // The hit rate is the diagnosis behind both numbers: a cold open is misses,
  // a keystroke should be almost entirely hits, and a change that turns the
  // second into the first is a regression no timing on a loaded machine would
  // have shown reliably.
  const auto rate = [](std::uint64_t calls, std::uint64_t hits) {
    return calls == 0 ? 0.0 : 100.0 * static_cast<double>(hits) / static_cast<double>(calls);
  };
  const auto delta = [](const auto& from, const auto& to, microcore::perf::CounterId id) {
    return to[static_cast<std::size_t>(id)] - from[static_cast<std::size_t>(id)];
  };
  using microcore::perf::CounterId;
  const std::uint64_t coldCalls = delta(before, afterCold, CounterId::RenderTextMeasureCalls);
  const std::uint64_t coldHits = delta(before, afterCold, CounterId::RenderTextMeasureCacheHits);
  const std::uint64_t uniqueCalls = delta(afterCold, afterUnique, CounterId::RenderTextMeasureCalls);
  const std::uint64_t uniqueHits = delta(afterCold, afterUnique, CounterId::RenderTextMeasureCacheHits);
  const std::uint64_t typeCalls = delta(afterUnique, afterType, CounterId::RenderTextMeasureCalls);
  const std::uint64_t typeHits = delta(afterUnique, afterType, CounterId::RenderTextMeasureCacheHits);
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.open_cold_layout.measures",
              static_cast<unsigned long long>(coldCalls), static_cast<unsigned long long>(coldHits),
              rate(coldCalls, coldHits));
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.open_unique_words.measures",
              static_cast<unsigned long long>(uniqueCalls), static_cast<unsigned long long>(uniqueHits),
              rate(uniqueCalls, uniqueHits));
  std::printf("%-40s %12llu calls %12llu hits %6.1f%%\n", "font.type_middle.measures",
              static_cast<unsigned long long>(typeCalls), static_cast<unsigned long long>(typeHits),
              rate(typeCalls, typeHits));
  return ok;
}

}

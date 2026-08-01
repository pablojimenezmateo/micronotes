#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"

#include "doc/Edits.h"
#include "doc/Fold.h"
#include "doc/Layout.h"
#include "core/markdown/MarkdownParser.h"
#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "ui/AppState.h"
#include "ui/FoldState.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

static std::string heavyMarkdown(int seed, int sections) {
  std::ostringstream out;
  out << "# Perf Note " << seed << "\n\n";
  for(int section = 0; section < sections; ++section) {
    out << "## Section " << section << "\n\n";
    out << "This paragraph has searchable text, [a local link](note-" << section << ".md), ";
    out << "[a remote link](https://example.com/" << seed << "/" << section << "), ";
    out << "inline `code`, **strong text**, and raw https://example.com/raw/" << seed << "/" << section << ".\n\n";
    out << "![image " << section << "](.micronotes/attachments/perf-" << seed << "/image-" << section << ".png)\n\n";
    out << "- [x] completed item " << section << "\n";
    out << "- [ ] pending item " << section << "\n";
    out << "  - nested item with more searchable text\n\n";
    out << "| Left | Center | Right |\n|:-----|:------:|------:|\n";
    out << "| alpha " << section << " | beta " << section << " | gamma " << section << " |\n\n";
    out << "> [!NOTE]\n> A callout with enough text to exercise wrapping and inline spans.\n\n";
  }
  return out.str();
}

// The live surface has no fonts in the core library, so the budget runs against
// a fixed-advance stand-in. It exercises the same scan, cache and flow work.
static micronotes::doc::Metrics stubMetrics() {
  micronotes::doc::Metrics metrics;
  metrics.measure = [](std::string_view value, const micronotes::doc::RunStyle& style) {
    std::size_t glyphs = 0;
    for(const char c : value) {
      if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
    }
    return static_cast<float>(glyphs) * style.size * 0.6f;
  };
  metrics.lineHeight = [](const micronotes::doc::RunStyle& style) { return std::round(style.size * 1.5f); };
  return metrics;
}

// Budget from the design: one keystroke in a 200 KB note re-lays out in ~2 ms.
static constexpr std::uint64_t kKeystrokeBudgetMicros = 2000;

static bool layoutBudgets(std::string* out) {
  std::string source;
  int section = 0;
  while(source.size() < 200 * 1024) {
    source += "## Section " + std::to_string(section) + "\n\n";
    source += "A paragraph with **strong text**, *emphasis*, `code`, a [link](note-" +
              std::to_string(section) + ".md) and enough words to wrap more than once on screen.\n\n";
    source += "- bullet " + std::to_string(section) + "\n- [ ] task " + std::to_string(section) + "\n\n";
    source += "> quoted line " + std::to_string(section) + "\n\n";
    ++section;
  }

  micronotes::doc::DocumentLayout layout;
  layout.setMetrics(stubMetrics());
  micronotes::doc::LayoutOptions options;
  options.width = 700.0f;
  {
    micronotes::perf::ScopeTimer timer("layout.initial_200kb");
    layout.update(source, options);
  }
  std::cout << "layout.blocks: " << layout.blockCount() << "\n";

  // Type into a paragraph halfway down and re-lay out, the way a keystroke does.
  std::size_t caret = source.find("paragraph", source.size() / 2);
  if(caret == std::string::npos) caret = source.size() / 2;
  bool ok = true;
  std::vector<std::uint64_t> samples;
  for(int i = 0; i < 24; ++i) {
    source.insert(caret + static_cast<std::size_t>(i), 1, 'x');
    options.caretOffset = caret;
    const auto start = std::chrono::steady_clock::now();
    layout.update(source, options);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    samples.push_back(static_cast<std::uint64_t>(micros));
  }
  std::sort(samples.begin(), samples.end());
  const std::uint64_t median = samples[samples.size() / 2];
  const std::uint64_t worst = samples.back();
  micronotes::perf::Recorder::instance().add("layout.keystroke_relayout_median", median);
  micronotes::perf::Recorder::instance().add("layout.keystroke_relayout_worst", worst);
  std::cout << "layout.keystroke_relaid_blocks: " << layout.lastRelaidBlocks() << "\n";

  // Folding puts a predicate on every block of every relayout, so it belongs
  // under the same keystroke budget as the layout it runs inside.
  {
    micronotes::ui::FoldState folds;
    folds.toggle("perf", micronotes::doc::foldKey(source, micronotes::doc::scanBlocks(source).front()));
    options.folded = [&folds, &source](const micronotes::doc::SourceBlock& block) {
      return folds.folded("perf", micronotes::doc::foldKey(source, block));
    };
    std::vector<std::uint64_t> folded;
    for(int i = 0; i < 12; ++i) {
      source.insert(caret + static_cast<std::size_t>(i), 1, 'y');
      options.caretOffset = caret;
      const auto start = std::chrono::steady_clock::now();
      layout.update(source, options);
      folded.push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()));
    }
    std::sort(folded.begin(), folded.end());
    const std::uint64_t foldedMedian = folded[folded.size() / 2];
    micronotes::perf::Recorder::instance().add("layout.keystroke_relayout_folded_median", foldedMedian);
    options.folded = nullptr;
    if(foldedMedian > kKeystrokeBudgetMicros) {
      std::cerr << "BUDGET FAILED: layout.keystroke_relayout_folded_median " << foldedMedian
                << "us exceeds " << kKeystrokeBudgetMicros << "us\n";
      ok = false;
    }
  }
  if(out) *out = source;
  if(median > kKeystrokeBudgetMicros) {
    std::cerr << "BUDGET FAILED: layout.keystroke_relayout_median " << median
              << "us exceeds " << kKeystrokeBudgetMicros << "us\n";
    ok = false;
  }
  return ok;
}

// Enter, Tab and Backspace each rescan the note to find the block they act on.
// That is once per structural key, not once per character, so the budget is
// looser than the keystroke one - but it still has to stay off the critical path.
static constexpr std::uint64_t kTransformBudgetMicros = 4000;

static bool editBudgets(const std::string& source) {
  const std::size_t caret = std::min(source.find("bullet", source.size() / 2) + 6, source.size());
  const auto time = [&](const char* name, auto&& transform) {
    std::vector<std::uint64_t> samples;
    for(int i = 0; i < 16; ++i) {
      const auto start = std::chrono::steady_clock::now();
      const auto edit = transform();
      const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      samples.push_back(static_cast<std::uint64_t>(micros));
      if(!edit.valid && i == 0) std::cout << name << ": (declined)\n";
    }
    std::sort(samples.begin(), samples.end());
    const std::uint64_t median = samples[samples.size() / 2];
    micronotes::perf::Recorder::instance().add(name, median);
    return median;
  };

  std::uint64_t worst = 0;
  worst = std::max(worst, time("edits.continue_list_200kb",
                               [&] { return micronotes::doc::continueList(source, caret); }));
  worst = std::max(worst, time("edits.outdent_or_unwrap_200kb",
                               [&] { return micronotes::doc::outdentOrUnwrap(source, caret); }));
  worst = std::max(worst, time("edits.toggle_todo_200kb",
                               [&] { return micronotes::doc::toggleTodo(source, caret); }));
  // The typing shortcut runs on every space, so it must reject without scanning.
  worst = std::max(worst, time("edits.typing_shortcut_reject_200kb",
                               [&] { return micronotes::doc::applyMarkdownShortcut(source, caret); }));
  // The block affordances: a drag drop, a multi-block turn-into, and the insert
  // button. Each runs once per gesture, never per keystroke.
  worst = std::max(worst, time("edits.move_blocks_to_200kb",
                               [&] { return micronotes::doc::moveBlocksTo(source, caret, caret, 0); }));
  worst = std::max(worst, time("edits.turn_blocks_into_200kb", [&] {
    return micronotes::doc::turnBlocksInto(source, caret, caret + 400, micronotes::doc::BlockKind::Bullet);
  }));
  worst = std::max(worst, time("edits.insert_block_after_200kb", [&] {
    return micronotes::doc::insertBlockAfter(source, caret, micronotes::doc::BlockKind::Todo);
  }));
  if(worst > kTransformBudgetMicros) {
    std::cerr << "BUDGET FAILED: a block transform took " << worst
              << "us, over " << kTransformBudgetMicros << "us\n";
    return false;
  }
  return true;
}

// Ranked by total time, because that is the only ordering that answers "what
// should I look at first". The per-call and max columns separate "slow once"
// from "fast but called far too often" -- two problems with different fixes
// that a single total conflates.
static void printSamples() {
  const auto samples = microcore::perf::Recorder::instance().snapshot();
  std::cout << "\n=== scope timings (ranked by total) ===\n";
  std::printf("%-48s %10s %12s %10s %10s\n", "scope", "calls", "total_us", "avg_us", "max_us");
  for(const auto& sample : samples) {
    const double avg = sample.calls ? static_cast<double>(sample.totalMicros) / static_cast<double>(sample.calls) : 0.0;
    std::printf("%-48s %10llu %12llu %10.1f %10llu\n",
                sample.name.c_str(),
                static_cast<unsigned long long>(sample.calls),
                static_cast<unsigned long long>(sample.totalMicros),
                avg,
                static_cast<unsigned long long>(sample.maxMicros));
  }
}

// The counters are the other half of the picture: timings say where the time
// went, counters say how many times the work happened at all. A scope that
// looks cheap per call but ran 40,000 times is invisible in the table above.
static void printCounters() {
  std::cout << "\n";
  microcore::perf::writeCounters(stdout);
}

}

int main() {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-perf-fixture";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  {
    microcore::perf::ScopeTimer timer("fixture.large_library.create_1000_notes");
    for(int i = 0; i < 1000; ++i) {
      micronotes::library::NoteMetadata metadata;
      metadata.id = "perf-" + std::to_string(i);
      metadata.title = "Perf Note " + std::to_string(i);
      metadata.tags = {"perf", i % 2 == 0 ? "even" : "odd"};
      auto path = library.createNote(metadata, heavyMarkdown(i, 3));
      if(i % 4 == 0) library.moveNote(path, "work");
      else if(i % 4 == 1) library.moveNote(path, "ideas");
    }
  }

  micronotes::library::LibraryIndex index;
  index.open(root);
  index.refreshChangedFiles();
  index.refreshChangedFiles();
  {
    micronotes::library::NoteMetadata metadata;
    metadata.id = "perf-updated";
    metadata.title = "Perf Updated";
    library.createNote(metadata, heavyMarkdown(2000, 8));
    index.refreshChangedFiles();
  }
  (void)index.search("searchable");

  {
    micronotes::ui::AppState state;
    microcore::perf::ScopeTimer timer("fixture.app_state.open_select_and_list");
    state.openOrCreateLibrary(root);
    state.selectFolder("work");
    (void)state.folders();
    (void)state.tags();
    auto notes = state.currentNotes();
    if(!notes.empty()) {
      state.selectNote(notes.front().id);
      (void)state.selectedNote();
    }
  }

  {
    microcore::markdown::MarkdownParser parser;
    microcore::perf::ScopeTimer timer("fixture.markdown.parse_heavy_document");
    const auto doc = parser.parse(heavyMarkdown(9999, 200));
    std::cout << "heavy_document.blocks: " << doc.blocks.size() << "\n";
  }

  std::string liveNote;
  bool withinBudget = layoutBudgets(&liveNote);
  withinBudget = editBudgets(liveNote) && withinBudget;

  printSamples();
  printCounters();
  std::filesystem::remove_all(root);
  return withinBudget ? 0 : 1;
}

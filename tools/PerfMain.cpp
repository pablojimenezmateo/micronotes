#include "perf/Perf.h"

#include "markdown/MarkdownParser.h"
#include "library/Library.h"
#include "library/LibraryIndex.h"
#include "ui/AppState.h"

#include <filesystem>
#include <iostream>
#include <sstream>

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

static void printSamples() {
  const auto samples = micronotes::perf::Recorder::instance().snapshot();
  for(const auto& sample : samples) {
    std::cout << sample.name << ": " << sample.micros << "us\n";
  }
}

}

int main() {
  const auto root = std::filesystem::temp_directory_path() / "micronotes-perf-fixture";
  std::filesystem::remove_all(root);
  micronotes::library::Library library(root);
  {
    micronotes::perf::ScopeTimer timer("fixture.large_library.create_1000_notes");
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
    micronotes::perf::ScopeTimer timer("fixture.app_state.open_select_and_list");
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
    micronotes::markdown::MarkdownParser parser;
    micronotes::perf::ScopeTimer timer("fixture.markdown.parse_heavy_document");
    const auto doc = parser.parse(heavyMarkdown(9999, 200));
    std::cout << "heavy_document.blocks: " << doc.blocks.size() << "\n";
  }

  printSamples();
  std::filesystem::remove_all(root);
  return 0;
}

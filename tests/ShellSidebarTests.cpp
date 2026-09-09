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

// The sidebar: its rows and their rhythm, the bands that shut, the tag dots,
// the companion files, and what a click and the keyboard cursor each do.

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

// The same through a sidebar row, and the distinction that matters most: a
// *click* on a row opens a tab, and the keyboard *cursor* passing over one does
// not. `moveTreeCursor` activates every row it steps onto, so without this
// holding Down in a thousand-note library would open a thousand tabs.
MICRONOTES_TEST(shell_a_sidebar_click_opens_a_tab_and_the_cursor_does_not) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-rownewtab");
  const auto& root = rootDir.path();
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
  const micronotes::tests::TempDir rootDir("micronotes-shell-tag-filter");
  const auto& root = rootDir.path();
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
  const micronotes::tests::TempDir rootDir("micronotes-shell-sections");
  const auto& root = rootDir.path();
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
  const micronotes::tests::TempDir rootDir("micronotes-shell-tag-dots");
  const auto& root = rootDir.path();
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

}

// The bands are reachable from the keyboard, which is the half that was missing
// when the collapsing was written: `moveTreeCursor` stepped over every section
// label, so nothing but a pointer could shut one.
MICRONOTES_TEST(shell_the_keyboard_reaches_the_sidebar_bands) {
  using micronotes::ui::SidebarSection;
  const micronotes::tests::TempDir rootDir("micronotes-shell-band-keys");
  const auto& root = rootDir.path();
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

}

// A companion file opens with the desktop when it is asked for -- a click, or
// Enter -- and never when the keyboard cursor merely passes over it. Neither
// touches the selection: a file has no page, so the note on screen stays.
MICRONOTES_TEST(shell_a_companion_opens_on_a_click_and_never_on_the_cursor) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-companion-open");
  const auto& root = rootDir.path();
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
}

// A query lists the files whose *name* matched under a caption of their own,
// and only when there are any.
MICRONOTES_TEST(shell_search_lists_matching_file_names_under_their_own_caption) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-companion-search");
  const auto& root = rootDir.path();
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
}

// A file copied into a `files/` directory from outside appears in the tree
// through one walk of that directory -- not through the library refresh a
// folder operation costs.
MICRONOTES_TEST(shell_a_file_dropped_into_files_refreshes_only_that_directory) {
  const micronotes::tests::TempDir rootDir("micronotes-shell-companion-watch");
  const auto& root = rootDir.path();
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
}

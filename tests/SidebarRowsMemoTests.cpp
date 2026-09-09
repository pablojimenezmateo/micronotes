#include "TestSupport.h"

#include "AppPerfCounters.h"
#include "app/Notes.h"
#include "app/Shell.h"
#include "app/SessionState.h"
#include "app/SidebarModel.h"
#include "ui/WorkspaceModel.h"
#include "core/perf/PerformanceCounters.h"

#include <filesystem>
#include <fstream>
#include <string>

using micronotes::app::SidebarRowsShape;
using micronotes::app::UiRuntime;
using micronotes::app::buildSidebarRows;
using micronotes::app::sidebarMetrics;
using micronotes::ui::Rect;
using microcore::perf::CounterId;
using microcore::perf::readCounter;

// The sidebar's row memo: when it reuses the list, and when it must not.
//
// This is the one memo in the shell that is deliberately not a `ui::Memo`,
// because its reuse is partial -- `shape` decides whether the rows exist and
// `placement` only decides where they sit. That design is worth keeping and it
// is also the reason the memo needs tests of its own: it used to compare its
// thirteen rebuild inputs field by field in one function and store them field
// by field in another, and nothing checked that the two lists agreed. A memo
// like that is wrong only when the field nobody stored is the field that
// changed, which is exactly the failure no test written after the fact would
// have thought to try.

namespace {

const Rect kPanel {0.0f, 0.0f, 320.0f, 900.0f};

// A library with rows in it. The counters below tally `rows.size()`, so an
// empty library reports zero whether it rebuilt or not -- which is how the
// first version of these tests passed against a memo that was reusing wrongly.
void openFixture(UiRuntime& ui, const char* name) {
  const auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "work");
  const auto note = [&](const std::filesystem::path& relative, const char* id, const char* title) {
    std::ofstream out(root / relative, std::ios::binary | std::ios::trunc);
    out << "---\nid: " << id << "\ntitle: " << title << "\ntags: work\n---\n\nSearchable body.\n";
  };
  note("hub.md", "memo-hub", "Hub");
  note("work/plan.md", "memo-plan", "Plan");
  micronotes::tests::require(micronotes::app::openLibraryRoot(ui, root),
                             "the fixture library did not open");
  ui.sidebar.tree.setExpanded({}, true);
  ui.sidebar.tree.setExpanded(std::filesystem::path("work"), true);
}

// A row list as comparable text, so two lists can be diffed in a message.
//
// Compared rather than the counters, because the counters tally `rows.size()`
// and say nothing about whether the rows are the *right* ones.
std::string describe(const UiRuntime& ui) {
  std::string out;
  for(const auto& row : ui.sidebar.rows) {
    out += std::to_string(static_cast<int>(row.kind)) + " " + row.label + " " + row.trailing + " " +
           row.tree.file.string() + " @" + std::to_string(row.rect.x) + "," +
           std::to_string(row.rect.y) + " " + std::to_string(row.rect.w) + "x" +
           std::to_string(row.rect.h) + (row.collapsed ? " shut" : "") + "\n";
  }
  return out;
}

// The same panel built with the memo bypassed. This is what the rows should be.
std::string builtFresh(UiRuntime& ui, Rect rect) {
  ui.sidebar.rowsKey.valid = false;
  buildSidebarRows(ui, rect, sidebarMetrics(16, 12));
  return describe(ui);
}

void build(UiRuntime& ui, Rect rect = kPanel) {
  buildSidebarRows(ui, rect, sidebarMetrics(16, 12));
}

// Rows built since the last call. Non-zero means the list was rebuilt rather
// than shifted.
std::uint64_t builtDelta(std::uint64_t& mark) {
  const std::uint64_t now = readCounter(CounterId::SidebarRowsBuilt);
  const std::uint64_t delta = now - mark;
  mark = now;
  return delta;
}

}

// The whole point of the memo: an unchanged panel does not rebuild.
MICRONOTES_TEST(sidebar_rows_are_reused_when_nothing_changed) {
  UiRuntime ui;
  openFixture(ui, "micronotes-rows-memo-reuse");
  build(ui);
  MICRONOTES_REQUIRE(!ui.sidebar.rows.empty());
  std::uint64_t mark = readCounter(CounterId::SidebarRowsBuilt);
  build(ui);
  MICRONOTES_REQUIRE(builtDelta(mark) == 0);
}

// And the invariant that makes the memo correct: after a build, the shape it
// stored describes the world the rows were built for. If reuse is wrong the
// stored shape is stale, and that is observable whatever the rebuild produced
// -- which matters, because the counters tally `rows.size()` and a rebuild
// that yields no rows adds nothing. The first version of this test counted, and
// passed against a search change it should have caught for exactly that reason.
MICRONOTES_TEST(sidebar_rows_key_never_describes_a_world_it_was_not_built_for) {
  struct Change {
    const char* what;
    void (*apply)(UiRuntime&, Rect&);
  };
  const Change changes[] = {
    {"search text", [](UiRuntime& ui, Rect&) { ui.fields.search.beginWith("Searchable"); }},
    {"a search that matches nothing", [](UiRuntime& ui, Rect&) {
       ui.fields.search.beginWith("zzzznothing");
     }},
    {"search scope", [](UiRuntime& ui, Rect&) {
       ui.fields.searchScope = micronotes::library::SearchScope::Title;
     }},
    {"panel width", [](UiRuntime&, Rect& rect) { rect.w += 40.0f; }},
    {"panel height", [](UiRuntime&, Rect& rect) { rect.h += 40.0f; }},
    {"a shut band", [](UiRuntime& ui, Rect&) {
       ui.state.editWorkspace().toggleSection(micronotes::ui::SidebarSection::Notebooks);
     }},
    {"an expanded folder", [](UiRuntime& ui, Rect&) {
       ui.sidebar.tree.setExpanded(std::filesystem::path("work"), false);
     }},
  };

  for(const auto& change : changes) {
    UiRuntime ui;
    openFixture(ui, "micronotes-rows-memo-shape");
    Rect rect = kPanel;
    build(ui, rect);
    change.apply(ui, rect);
    build(ui, rect);
    micronotes::tests::require(
      ui.sidebar.rowsKey.shape == micronotes::app::sidebarRowsShape(ui, rect, sidebarMetrics(16, 12)),
      std::string("after ") + change.what +
        " changed, the row list's stored shape is not the world it now describes -- the memo "
        "reused a list built for different inputs");
  }
}

// Reuse still has to happen, and be visible, or the memo is just a slow
// rebuild. Counted here, where the fixture does produce rows.
MICRONOTES_TEST(sidebar_rows_rebuild_when_a_shape_input_changes) {
  UiRuntime ui;
  openFixture(ui, "micronotes-rows-memo-rebuild");
  Rect rect = kPanel;
  build(ui, rect);
  MICRONOTES_REQUIRE(!ui.sidebar.rows.empty());
  std::uint64_t mark = readCounter(CounterId::SidebarRowsBuilt);
  build(ui, rect);
  MICRONOTES_REQUIRE(builtDelta(mark) == 0);
  // A wider panel is a rebuild rather than a shift: the rows are laid out to it.
  rect.w += 40.0f;
  build(ui, rect);
  MICRONOTES_REQUIRE(builtDelta(mark) > 0);
}

// The text rhythm is a shape input rather than a placement one: every row's
// height derives from it, so a change has to rebuild rather than shift a list
// laid out at the old rhythm.
MICRONOTES_TEST(sidebar_rows_rebuild_when_the_text_rhythm_changes) {
  UiRuntime ui;
  openFixture(ui, "micronotes-rows-memo-rhythm");
  buildSidebarRows(ui, kPanel, sidebarMetrics(16, 12));
  MICRONOTES_REQUIRE(!ui.sidebar.rows.empty());
  std::uint64_t mark = readCounter(CounterId::SidebarRowsBuilt);
  buildSidebarRows(ui, kPanel, sidebarMetrics(16, 12));
  MICRONOTES_REQUIRE(builtDelta(mark) == 0);
  buildSidebarRows(ui, kPanel, sidebarMetrics(30, 22));
  MICRONOTES_REQUIRE(builtDelta(mark) > 0);
}

// Moving the panel is placement, so the rows are shifted rather than rebuilt --
// and the shift has to actually move them, or the panel hit-tests rows where
// they used to be.
MICRONOTES_TEST(sidebar_rows_shift_rather_than_rebuild_when_the_panel_moves) {
  UiRuntime ui;
  openFixture(ui, "micronotes-rows-memo-shift");
  Rect rect = kPanel;
  build(ui, rect);
  MICRONOTES_REQUIRE(!ui.sidebar.rows.empty());
  const float before = ui.sidebar.rows.front().rect.x;

  std::uint64_t mark = readCounter(CounterId::SidebarRowsBuilt);
  rect.x += 25.0f;
  build(ui, rect);
  MICRONOTES_REQUIRE(builtDelta(mark) == 0);
  MICRONOTES_REQUIRE(ui.sidebar.rows.front().rect.x == before + 25.0f);
}

// The shape is compared with the compiler's `operator==`, which is what makes
// the compare and the store one list. If someone gives it a hand-written one
// that forgets a field, this fails.
MICRONOTES_TEST(sidebar_rows_shape_compares_every_field_it_holds) {
  SidebarRowsShape a;
  SidebarRowsShape b;
  MICRONOTES_REQUIRE(a == b);

  const auto differs = [](auto&& mutate) {
    SidebarRowsShape left;
    SidebarRowsShape right;
    mutate(right);
    return !(left == right);
  };
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.stateRevision = 1; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.treeRevision = 1; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.search = "x"; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) {
    s.searchScope = micronotes::library::SearchScope::Title;
  }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.tag = "x"; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.favorites = {"x"}; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.recents = {"x"}; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.collapsedSections[2] = true; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.width = 1.0f; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.height = 1.0f; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.rowHeight = 1.0f; }));
  MICRONOTES_REQUIRE(differs([](SidebarRowsShape& s) { s.snippetHeight = 1.0f; }));
}

// The invariant that actually pins the memo: **the list it reuses is the list a
// fresh build would produce.**
//
// The two tests above cannot catch a field missing from `SidebarRowsShape`.
// Now that the compare and the store read one list, dropping a field drops it
// from both -- so the stored shape still matches the world, the memo still
// reuses, and the rows are simply wrong. Only a differential check sees that,
// because only a fresh build knows what the rows should have been.
//
// Every input here is one a reader can change without touching the panel's
// geometry, which is the case the memo exists to make fast and therefore the
// case it can get wrong.
MICRONOTES_TEST(sidebar_rows_reused_list_matches_a_freshly_built_one) {
  struct Change {
    const char* what;
    void (*apply)(UiRuntime&, Rect&);
  };
  const Change changes[] = {
    {"nothing at all", [](UiRuntime&, Rect&) {}},
    {"a search that matches", [](UiRuntime& ui, Rect&) { ui.fields.search.beginWith("Searchable"); }},
    {"a search that matches nothing", [](UiRuntime& ui, Rect&) {
       ui.fields.search.beginWith("zzzznothing");
     }},
    {"the search scope", [](UiRuntime& ui, Rect&) {
       ui.fields.searchScope = micronotes::library::SearchScope::Title;
     }},
    {"a shut band", [](UiRuntime& ui, Rect&) {
       ui.state.editWorkspace().toggleSection(micronotes::ui::SidebarSection::Notebooks);
     }},
    {"a collapsed folder", [](UiRuntime& ui, Rect&) {
       ui.sidebar.tree.setExpanded(std::filesystem::path("work"), false);
     }},
    {"the selected note", [](UiRuntime& ui, Rect&) {
       micronotes::app::selectNoteById(ui, "memo-plan");
     }},
    {"the panel width", [](UiRuntime&, Rect& rect) { rect.w += 40.0f; }},
    {"the panel height", [](UiRuntime&, Rect& rect) { rect.h -= 200.0f; }},
    {"the panel origin", [](UiRuntime&, Rect& rect) { rect.x += 17.0f; rect.y += 9.0f; }},
    {"the scroll", [](UiRuntime& ui, Rect&) { ui.sidebar.list.scrollBy(40.0f); }},
  };

  for(const auto& change : changes) {
    UiRuntime memoized;
    openFixture(memoized, "micronotes-rows-memo-diff-a");
    Rect rect = kPanel;
    build(memoized, rect);
    change.apply(memoized, rect);
    build(memoized, rect);
    const std::string viaMemo = describe(memoized);

    UiRuntime fresh;
    openFixture(fresh, "micronotes-rows-memo-diff-b");
    Rect freshRect = kPanel;
    build(fresh, freshRect);
    change.apply(fresh, freshRect);
    const std::string viaRebuild = builtFresh(fresh, freshRect);

    micronotes::tests::require(
      viaMemo == viaRebuild,
      std::string("after changing ") + change.what +
        ", the reused row list differs from a freshly built one.\n--- reused ---\n" + viaMemo +
        "--- fresh ---\n" + viaRebuild);
  }
}

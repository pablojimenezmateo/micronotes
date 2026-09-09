#include "TestSupport.h"

#include "core/perf/PerformanceCounters.h"
#include "core/render/FontResolver.h"
#include "core/render/TextTextureCache.h"
#include "ui/Actions.h"
#include "ui/Menus.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Invariants about the shape of the source tree rather than the behaviour of
// any one function. They exist because the failure modes below are silent: the
// code compiles, the tests pass, and the damage only shows up as a number that
// quietly lies to whoever reads it next.

namespace {

std::filesystem::path repoRoot() {
  return std::filesystem::path(MICRONOTES_SOURCE_DIR);
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::vector<std::filesystem::path> sourceFiles(const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  if(!std::filesystem::exists(root)) return files;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if(!entry.is_regular_file()) continue;
    const auto extension = entry.path().extension();
    if(extension == ".cpp" || extension == ".h") files.push_back(entry.path());
  }
  return files;
}

// Every id declared through an X(...) row in the given header.
std::vector<std::string> declaredCounterIds(const std::filesystem::path& header) {
  const std::string text = readText(header);
  std::vector<std::string> ids;
  const std::regex declaration(R"(\bX\(\s*(\w+)\s*,)");
  for(std::sregex_iterator it(text.begin(), text.end(), declaration), last; it != last; ++it) {
    ids.push_back((*it)[1].str());
  }
  return ids;
}

}

// A counter with no producer reads zero forever, which is strictly worse than
// no counter at all: a reader sees the row missing from a dump and concludes
// the code path did not run, when in fact nobody ever incremented it. Declaring
// an id costs one line and is easy to do while sketching; wiring it up is the
// part that gets forgotten.
MICRONOTES_TEST(architecture_every_perf_counter_has_a_producer) {
  const auto coreHeader = repoRoot() / "src/core/perf/PerformanceCounters.h";
  const auto appHeader = repoRoot() / "src/AppPerfCounters.h";
  MICRONOTES_REQUIRE(std::filesystem::exists(coreHeader));
  MICRONOTES_REQUIRE(std::filesystem::exists(appHeader));

  // If the X-macro spelling changes, this test would silently scan nothing and
  // pass forever. Anchor on it explicitly.
  MICRONOTES_REQUIRE(readText(coreHeader).find("#define MICROCORE_PERF_COUNTERS(X)") != std::string::npos);
  MICRONOTES_REQUIRE(readText(appHeader).find("#define MICROCORE_APP_PERF_COUNTERS(X)") != std::string::npos);

  std::vector<std::string> declared = declaredCounterIds(coreHeader);
  const auto appIds = declaredCounterIds(appHeader);
  declared.insert(declared.end(), appIds.begin(), appIds.end());
  MICRONOTES_REQUIRE(!declared.empty());

  std::set<std::string> produced;
  const std::regex use(R"(\bCounterId::(\w+))");
  for(const auto& path : sourceFiles(repoRoot() / "src")) {
    // The declaration and the name table are not producers.
    const auto name = path.filename().string();
    if(name == "PerformanceCounters.h" || name == "PerformanceCounters.cpp" || name == "AppPerfCounters.h") continue;
    const std::string text = readText(path);
    for(std::sregex_iterator it(text.begin(), text.end(), use), last; it != last; ++it) {
      produced.insert((*it)[1].str());
    }
  }

  std::string orphans;
  for(const auto& id : declared) {
    if(produced.count(id) == 0) {
      if(!orphans.empty()) orphans += ", ";
      orphans += id;
    }
  }
  micronotes::tests::require(
    orphans.empty(),
    "perf counters are declared but never incremented anywhere in src/: " + orphans +
    " -- either wire them up or delete them; a counter that reads zero forever is "
    "read as 'this code path did not run'");
}

// The counter table is indexed positionally by CounterId, so the enum and the
// name array must have the same length. The X-macro makes that true by
// construction; this test fails loudly if someone reintroduces a hand-written
// parallel list.
MICRONOTES_TEST(architecture_perf_counter_names_cover_every_id) {
  using microcore::perf::CounterId;
  using microcore::perf::counterName;
  for(std::size_t i = 0; i < microcore::perf::kCounterCount; ++i) {
    const auto name = counterName(static_cast<CounterId>(i));
    MICRONOTES_REQUIRE(!name.empty());
    // "<subsystem>.<event>" -- a name without a dot means a row was added
    // without following the convention the dumps are grouped by.
    MICRONOTES_REQUIRE(name.find('.') != std::string_view::npos);
  }
}

// src/core is the app-agnostic layer, and "agnostic" has to be checked or it
// decays: one `.micronotes` spelled out in a path helper and the boundary is
// gone. AppIdentity.h is the single seam, and it carries the name as a macro
// the host CMakeLists supplies so the derived paths stay compile-time
// constants.
MICRONOTES_TEST(architecture_core_does_not_hardcode_the_app_name) {
  std::string offenders;
  for(const auto& path : sourceFiles(repoRoot() / "src/core")) {
    if(path.filename() == "AppIdentity.h") continue;
    const std::string text = readText(path);
    if(text.find("micronotes") != std::string::npos) {
      if(!offenders.empty()) offenders += ", ";
      offenders += path.filename().string();
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "src/core names a specific app: " + offenders +
    " -- use kAppName / kAppDotDir from core/AppIdentity.h");
}

// --- core/render ------------------------------------------------------------

// Hardcoded /usr/share/fonts/truetype/dejavu/ paths meant a machine without
// DejaVu installed exactly there rendered no text at all: TTF_OpenFont returned
// null and every draw call silently did nothing.
MICRONOTES_TEST(font_resolver_finds_a_real_file_for_every_face) {
  const microcore::render::FontRequest requests[] = {
    {false, false, false}, {false, true, false}, {false, false, true},
    {false, true, true},   {true, false, false},
  };
  for(const auto& request : requests) {
    const auto path = microcore::render::resolveFontFile(request);
    micronotes::tests::require(!path.empty(), "no font resolved for a requested face");
    micronotes::tests::require(std::filesystem::exists(path), "resolved font does not exist: " + path);
  }
}

// The old cache destroyed every texture on reaching 4096 entries. Besides the
// one-frame cost of thousands of destructions, a full flush discards exactly
// the entries about to be reused, because it cannot tell which those are.
MICRONOTES_TEST(text_texture_cache_evicts_least_recently_used_not_everything) {
  microcore::render::TextTextureCache cache(3);
  const SDL_Color color {255, 255, 255, 255};
  const microcore::render::TextTextureCache::Style style {};
  // Null textures: the cache only ever destroys a non-null one, so this
  // exercises the eviction policy without needing a live renderer.
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};
  const auto key = [&](std::string_view text) {
    return microcore::render::TextTextureCache::makeKey(text, color, style);
  };

  cache.insert(key("a"), entry);
  cache.insert(key("b"), entry);
  cache.insert(key("c"), entry);
  MICRONOTES_REQUIRE(cache.size() == 3);

  // Touch "a" so "b" becomes the coldest entry.
  MICRONOTES_REQUIRE(cache.find(key("a")) != nullptr);
  cache.insert(key("d"), entry);

  MICRONOTES_REQUIRE(cache.size() == 3);
  MICRONOTES_REQUIRE(cache.find(key("b")) == nullptr);  // evicted
  MICRONOTES_REQUIRE(cache.find(key("a")) != nullptr);  // kept: recently used
  MICRONOTES_REQUIRE(cache.find(key("c")) != nullptr);
  MICRONOTES_REQUIRE(cache.find(key("d")) != nullptr);
}

MICRONOTES_TEST(text_texture_cache_distinguishes_colour_and_style) {
  microcore::render::TextTextureCache cache(16);
  const microcore::render::TextTextureCache::Entry entry {nullptr, 10, 10};
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color red {255, 0, 0, 255};

  using Cache = microcore::render::TextTextureCache;
  cache.insert(Cache::makeKey("x", white, {}), entry);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", red, {})) == nullptr);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", white, {true, false, false, false})) == nullptr);
  MICRONOTES_REQUIRE(cache.find(Cache::makeKey("x", white, {})) != nullptr);
}

// Opening a connection outside SqliteDb bypasses two things at once: the
// per-connection pragmas (so `synchronous` falls back to FULL and
// `foreign_keys` to OFF, silently disabling ON DELETE CASCADE) and the
// instrumentation. The second failure is the nastier one: sqlite.connection_opens
// then reads *zero*, which is indistinguishable from "this code path never ran".
//
// architecture_every_perf_counter_has_a_producer cannot catch that, because the
// counter does have a producer -- just not on the path that matters. This rule
// closes the gap by requiring every connection to go through the wrapper.
MICRONOTES_TEST(architecture_connections_go_through_the_sqlite_wrapper) {
  std::string offenders;
  for(const auto& path : sourceFiles(repoRoot() / "src")) {
    if(path.parent_path().filename() == "persistence") continue;
    const std::string text = readText(path);
    if(text.find("sqlite3_open") != std::string::npos) {
      if(!offenders.empty()) offenders += ", ";
      offenders += path.filename().string();
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "sqlite3_open is called outside core/persistence: " + offenders +
    " -- use microcore::persistence::SqliteDb, which applies the per-connection "
    "pragmas and counts the open; a direct handle silently loses both");
}

// AGENTS.md says of src/app/Application.cpp: "a catch-all doing layout, input,
// rendering, and persistence. Do not grow it." It said 3,000 lines when that
// was written and the file had reached 4,820 by the time anyone checked, which
// is what a rule with no test attached is worth.
//
// This is that test, and it is now two rules rather than one. The first is
// Application.cpp's own ratchet, which only ever goes down. The second is the
// lesson of the first: a budget on one file by name does not stop a catch-all,
// it only stops *that* catch-all -- the input routing and the frame were free to
// pile up in whichever neighbouring file they were moved to. So every source
// the shell draws through carries a ceiling, and Application.cpp's is simply
// the tightest one.
//
// Both numbers are ratchets, not targets: a change that moves behaviour into a
// named unit lowers them in the same commit, and nothing raises them. If either
// fails, the fix is a named unit under src/ -- not a bigger budget.
constexpr int kApplicationLineBudget = 232;
constexpr int kShellFileLineBudget = 1000;
// And the same rule again, over every source in the tree rather than the shell
// alone -- because the shell ceiling did not stop a catch-all either, it only
// stopped the ones in `src/app/`. While it was the only one, `doc/Layout.cpp`
// reached 1,732 lines and `tools/PerfMain.cpp` 1,553, which are the two largest
// files this project has ever had, and both grew to that size with the ceiling
// green the whole way. Sizes rather than one number because these are ratchets
// at what the tree measures today, not targets: a change that moves behaviour
// into a named unit lowers them in the same commit, and nothing raises them.
constexpr int kTreeFileLineBudget = 1012;

namespace {

int lineCount(const std::filesystem::path& path) {
  const std::string text = readText(path);
  return static_cast<int>(std::count(text.begin(), text.end(), '\n'));
}

}

MICRONOTES_TEST(architecture_application_cpp_stays_under_its_budget) {
  const auto path = repoRoot() / "src" / "app" / "Application.cpp";
  const int lines = lineCount(path);
  micronotes::tests::require(
    lines <= kApplicationLineBudget,
    "src/app/Application.cpp is " + std::to_string(lines) + " lines, over its budget of " +
      std::to_string(kApplicationLineBudget) +
      ". This file is the window, the renderer and the event loop, and nothing else: put the new "
      "behaviour in a named unit under src/ and lower the budget instead of raising it.");
}

// The same rule, over the whole shell. Application.cpp got a budget because it
// had grown to 4,820 lines; nothing stopped the next file from doing the same,
// and a decomposition that moves 1,900 lines from one file into one other file
// passes the test above while changing nothing.
MICRONOTES_TEST(architecture_no_shell_source_is_a_catch_all) {
  std::string offenders;
  for(const auto& entry : std::filesystem::directory_iterator(repoRoot() / "src" / "app")) {
    if(entry.path().extension() != ".cpp") continue;
    const int lines = lineCount(entry.path());
    if(lines <= kShellFileLineBudget) continue;
    if(!offenders.empty()) offenders += ", ";
    offenders += entry.path().filename().string() + " (" + std::to_string(lines) + ")";
  }
  micronotes::tests::require(
    offenders.empty(),
    "these src/app/ sources are over the " + std::to_string(kShellFileLineBudget) +
      "-line ceiling: " + offenders +
      " -- a file this size is doing more than one thing; split the thing that has the fewest "
      "callers into a named unit of its own");
}

// The ceiling, over the whole tree.
//
// `architecture_no_shell_source_is_a_catch_all` above draws exactly this lesson
// about `Application.cpp` -- "a budget on one file by name does not stop a
// catch-all, it only stops that one" -- and then stops at `src/app/`. So the
// next two catch-alls formed in the two places it does not reach.
// `doc/Layout.cpp` held the line breaker, the incremental pass and the
// per-block work in 1,732 lines; `tools/PerfMain.cpp` held ten independent
// lanes and the instrument they measure through in 1,553. Neither was ever
// flagged, by this file or by anything else.
//
// `tools/` counts. It is not shipped code, which is exactly the reasoning that
// let the harness grow -- and the harness is the instrument every performance
// claim in `docs/performance.md` rests on, so a reader who cannot find a lane
// in it is the reader who does not add one.
MICRONOTES_TEST(architecture_no_source_in_the_tree_is_a_catch_all) {
  std::vector<std::pair<std::string, int>> offenders;
  for(const auto& root : {"src", "tools"}) {
    for(const auto& path : sourceFiles(repoRoot() / root)) {
      const int lines = lineCount(path);
      if(lines <= kTreeFileLineBudget) continue;
      offenders.push_back({std::string(root) + "/" + path.filename().string(), lines});
    }
  }
  std::sort(offenders.begin(), offenders.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string message = "these sources are over the " + std::to_string(kTreeFileLineBudget) +
                        "-line tree ceiling:\n";
  for(const auto& [name, lines] : offenders) {
    message += "  " + name + " (" + std::to_string(lines) + ")\n";
  }
  message +=
    "This is a ratchet at what the tree measured when it was written, not a target: it goes down "
    "when behaviour moves into a named unit, and nothing raises it. Split the thing that has the "
    "fewest callers into a unit of its own and lower the number in the same commit.";
  micronotes::tests::require(offenders.empty(), message);
}

// The right panel is drawn after the content, and that is a performance
// contract rather than a matter of taste.
//
// The panel's outline borrows the block partition the live page splices during
// its own layout. `blocksAt` hands it over only for the revision it was built
// from -- which is what makes the borrow safe -- so if the panel is drawn ahead
// of the content it asks one revision early, is correctly refused, and rescans
// the whole note on every keystroke instead: 194 us of a 226 us rebuild on a
// 200 KB note, against the ~14 us that keystroke's own layout costs.
//
// Nothing else fails when that happens. The pixels are identical, every unit
// test still passes -- `shell_outline_borrows_the_partition_the_live_page_
// already_spliced` drives the two directly and so cannot see the order they are
// called in -- and the only evidence is a counter in a session nobody is
// running. So the order is asserted here, where it is cheap, rather than left
// to be rediscovered.
MICRONOTES_TEST(architecture_the_right_panel_is_drawn_after_the_content) {
  const std::string text = readText(repoRoot() / "src" / "app" / "Frame.cpp");
  const auto content = text.find("\"shell.content\"");
  const auto rightPanel = text.find("\"shell.right_panel\"");
  micronotes::tests::require(content != std::string::npos && rightPanel != std::string::npos,
                             "drawApp no longer has both a content and a right-panel scope");
  micronotes::tests::require(
    rightPanel > content,
    "drawApp draws the right panel before the content. Its outline then asks the live page for a "
    "block partition the page has not laid out yet, is refused, and rescans the whole note on "
    "every keystroke -- with identical pixels and a green suite. Draw it after the content.");
}

// Actions are named in one table so the palette, the shortcut list and the key
// handler cannot drift. That only holds while nobody writes a key name out by
// hand somewhere else: a hint line that says "Ctrl+P" is a fourth copy, and the
// first one to go stale.
//
// The registry is the exception, because it is where the spelling lives.
MICRONOTES_TEST(architecture_key_names_are_formatted_not_typed) {
  // A line-by-line scan rather than a regex: std::regex backtracks its way
  // through a 200 KB translation unit and overflows the stack. Only string
  // literals count -- a comment naming a key is documentation, not a fourth
  // copy of the binding.
  const std::vector<std::string> chords {"Ctrl+", "Alt+"};
  std::vector<std::string> offenders;
  for(const auto& file : sourceFiles(repoRoot() / "src")) {
    if(file.filename() == "Actions.cpp") continue;
    std::istringstream lines(readText(file));
    std::string line;
    while(std::getline(lines, line)) {
      const auto firstCode = line.find_first_not_of(" \t");
      const auto quote = line.find('"');
      if(quote == std::string::npos) continue;
      // Anything after a // is prose. A chord in a comment is documentation,
      // not a second copy of the binding.
      const auto comment = line.find("//");
      if(comment != std::string::npos && comment < quote) continue;
      if(comment != std::string::npos) line = line.substr(0, comment);
      for(const auto& chord : chords) {
        const auto at = line.find(chord, quote);
        if(at == std::string::npos) continue;
        offenders.push_back(file.filename().string() + ":" + line.substr(firstCode));
        break;
      }
    }
  }
  std::string message = "a key chord is spelled out by hand here instead of coming from "
                        "ui::acceleratorText(), so rebinding the key would leave it stale:\n";
  for(const auto& hit : offenders) message += "  " + hit + "\n";
  micronotes::tests::require(offenders.empty(), message);
}

// Every action a surface can offer has to be dispatched by something.
//
// The palette, the menu bar and the keyboard all funnel into
// performCommand(), which is a chain of `id == "name"` branches, and a name
// with no branch is a control that does nothing at all when clicked -- silently,
// because the chain simply falls off the end. That is how the rail's Commands
// button spent its life inert: `command-palette` is the one action deliberately
// kept out of the palette, so the rail was the only surface offering it and
// nothing else exercised the missing branch.
//
// A source scan rather than a call, because the dispatch needs a whole running
// shell to call. It is enough: what is being checked is that *something*
// compares the name.
//
// Every `src/app/` source, not just `Application.cpp`. It used to read that one
// file and look for `id == "name"`, on the reasoning that the chain is spelt one
// way -- which was true until the file's shrinking budget started pushing
// branches out of it. A group of related commands now goes to a named unit and
// `performCommand` delegates (`handleNotePathCommand`), so the chain is spelt
// three ways in three files: `id ==`, `command ==`, `result.itemId ==`. What
// they have in common is the comparison, so that is what this looks for -- and
// a menu table's `{"name", "Label", ...}` entry deliberately does not match it,
// because being *offered* is the thing whose dispatch is in question.
MICRONOTES_TEST(architecture_every_offered_action_is_dispatched) {
  std::string dispatch;
  for(const auto& entry : std::filesystem::directory_iterator(repoRoot() / "src" / "app")) {
    if(entry.path().extension() != ".cpp") continue;
    dispatch += readText(entry.path());
  }
  // Anchor on the chain itself. Were performCommand rewritten into a table,
  // this test would otherwise scan for a spelling nothing uses any more and
  // pass forever.
  MICRONOTES_REQUIRE(dispatch.find("void performCommand(UiRuntime& ui, const std::string& id) {") !=
                     std::string::npos);

  std::set<std::string> offered;
  for(const auto& spec : micronotes::ui::actionSpecs()) {
    // Everything the command palette lists.
    if(spec.inPalette) offered.insert(std::string(spec.name));
    // And everything a key alone runs, which the palette does not have to
    // list: the editing verbs act on a selection the palette has just taken
    // the focus away from, so they carry `inPalette = false` and were checked
    // by nothing. `keyRunsIt` means `handleKey` hands the name straight to
    // performCommand, so a name with no branch there is a dead shortcut --
    // which is exactly what `F2` and `Ctrl+Q` were.
    if(spec.keyRunsIt && !spec.chord.empty()) offered.insert(std::string(spec.name));
  }
  // Everything the menu bar can be clicked on. Read from the menus' own tables
  // rather than a copy of them, so an item added there is covered by this test
  // the moment it is added -- and the bar offers far more than the seven-button
  // icon rail it replaced, which is most of the value of this check.
  for(const auto& menu : micronotes::ui::menuSpecs()) {
    for(const auto& item : menu.items) {
      if(item.separator) continue;
      const auto* spec = micronotes::ui::findAction(item.action);
      MICRONOTES_REQUIRE(spec != nullptr);
      offered.insert(std::string(spec->name));
    }
  }
  MICRONOTES_REQUIRE(!offered.empty());

  std::string missing;
  for(const auto& name : offered) {
    if(dispatch.find("== \"" + name + "\"") != std::string::npos) continue;
    if(!missing.empty()) missing += ", ";
    missing += name;
  }
  micronotes::tests::require(
    missing.empty(),
    "actions the palette or the menu bar offer but nothing under src/app/ dispatches: " + missing +
    " -- clicking one of these does nothing at all, and nothing else notices");
}

// The tab strip lays itself out exactly once, in the draw.
//
// Asserted here because nothing else can see it. `ui::layoutTabs` narrows every tab to the
// widest title when that is less than an even share of the strip, so its
// result depends on the text measurer it is handed -- and a second caller that
// hands it a different one, or none, gets a *different strip* with no error and
// identical-looking pixels. That is exactly what happened: the hit test passed
// `nullptr` on a comment claiming the geometry was a pure function of the
// titles, and a click on the second tab opened the first.
//
// `tabs_need_the_measurer_the_draw_used` pins the disagreement. This pins the
// fix: with one caller there is no second answer to disagree with, and the
// hit tests read `ui.tabStrip` -- what was drawn.
MICRONOTES_TEST(architecture_the_tab_strip_is_laid_out_once) {
  std::vector<std::string> callers;
  for(const auto& entry : std::filesystem::directory_iterator(repoRoot() / "src" / "app")) {
    if(entry.path().extension() != ".cpp") continue;
    const std::string text = readText(entry.path());
    std::size_t at = text.find("layoutTabs(");
    while(at != std::string::npos) {
      callers.push_back(entry.path().filename().string());
      at = text.find("layoutTabs(", at + 1);
    }
  }
  std::string where;
  for(const auto& caller : callers) {
    if(!where.empty()) where += ", ";
    where += caller;
  }
  micronotes::tests::require(
    callers.size() == 1 && callers.front() == "TabStrip.cpp",
    "ui::layoutTabs is called " + std::to_string(callers.size()) + " times under src/app/ (" + where +
      "). It must be called once, by drawTabStrip: its result depends on the text measurer it is "
      "handed, so a second caller is a second strip -- and the click lands on whichever one it "
      "built rather than on the one that was painted. Read ui.tabStrip instead.");
}

// The ASCII case fold lives in exactly one place, and `<cctype>` is not it.
//
// `core/util/StringUtil.h` exists because the fold had been written out at
// eight sites and the copies had drifted; its own comment says so. That held
// for a while and then stopped: `ui/Menus.cpp` grew a fourth `lowerAscii`, and
// three sites reached for `std::toupper` because the header had a lower fold
// and no upper one -- so the drift came back through the half that was missing.
//
// Both halves of that are worth failing on, and the `std::toupper` half is a
// correctness bug rather than a tidiness one. `std::toupper` is
// locale-dependent, and every caller here folds a *machine* string: a callout
// kind matched against "TIP", a key chord printed on a shortcut row, a theme
// name. Under a Turkish locale `toupper('i')` is not 'I', so `> [!tip]` stops
// finding its colour -- on the user's machine, in a build that passed every
// test on ours.
//
// Comments are exempt: this header's own preamble discusses the dance it
// replaced, and so does this test.
MICRONOTES_TEST(architecture_the_ascii_case_fold_lives_in_one_place) {
  // A hand-rolled fold, and the locale-dependent library one. Not `<cctype>`
  // itself: a dozen files include it for `isalnum`, `ispunct` and `isdigit`,
  // which is a different question from case and one they all guard correctly by
  // casting to `unsigned char` first.
  const std::vector<std::string> banned {
    "- 'A' + 'a'", "- 'a' + 'A'", "std::tolower", "std::toupper", "tolower(", "toupper(",
  };
  std::vector<std::string> offenders;
  for(const auto& file : sourceFiles(repoRoot() / "src")) {
    // Where the fold is defined, and where its history is written down.
    if(file.filename() == "StringUtil.h" || file.filename() == "StringUtil.cpp") continue;
    std::istringstream lines(readText(file));
    std::string line;
    int number = 0;
    while(std::getline(lines, line)) {
      ++number;
      const auto code = line.substr(0, line.find("//"));
      for(const auto& needle : banned) {
        if(code.find(needle) == std::string::npos) continue;
        offenders.push_back(file.filename().string() + ":" + std::to_string(number) + ": " + needle);
        break;
      }
    }
  }
  std::string message =
    "the ASCII case fold is spelled out by hand, or taken from the locale, instead of coming "
    "from core/util/StringUtil.h:\n";
  for(const auto& hit : offenders) message += "  " + hit + "\n";
  message +=
    "use util::toLowerAscii / util::toUpperAscii. std::tolower and std::toupper are "
    "locale-dependent, and every fold in this tree is over a machine string.";
  micronotes::tests::require(offenders.empty(), message);
}

// The tree is layered, and the layers only point one way.
//
// `core` knows about nothing; `doc` is the Markdown document; `library` is the
// folder of notes; `ui` draws and models; `app` is the shell and the loop. Each
// may include itself and anything below it, and nothing above.
//
// This is checked rather than described because it had already broken, in the
// way layering always does -- by one include, for one function.
// `library/LibraryIndex.cpp` needs to know what a note's `[[wikilinks]]` are so
// it can build the backlink table, and the answer lived in `ui/WikiLink.h`. So
// the library included the UI, the UI includes the library, and the two
// directories were a cycle: a change to a paint could not be reasoned about
// without the index, and the index could not be built without the paint.
//
// Nothing failed. It compiled, because the include graph over *files* was still
// acyclic -- which is exactly why a rule about directories needs a test about
// directories. The fix was to notice that "what links does this text contain"
// is a question about a document (`doc/WikiLink.h`) and "which note does this
// target name" is a question about a library (`library/WikiResolve.h`), and
// that they had been one header only because they were both about links.
MICRONOTES_TEST(architecture_the_layers_only_point_one_way) {
  // Lowest first. A layer may include itself and anything before it.
  const std::vector<std::string> layers {"core", "doc", "library", "ui", "app"};

  std::string offenders;
  for(std::size_t i = 0; i < layers.size(); ++i) {
    const auto dir = repoRoot() / "src" / layers[i];
    for(const auto& path : sourceFiles(dir)) {
      const std::string text = readText(path);
      for(std::size_t j = i + 1; j < layers.size(); ++j) {
        const std::string include = "#include \"" + layers[j] + "/";
        if(text.find(include) == std::string::npos) continue;
        if(!offenders.empty()) offenders += "; ";
        offenders += "src/" + layers[i] + "/" + path.filename().string() + " includes " + layers[j] + "/";
      }
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "a layer includes one above it: " + offenders +
    " -- the order is core < doc < library < ui < app. Whatever is wanted from the upper layer is "
    "either in the wrong place or is two things: move the half the lower layer needs down, and "
    "leave the half that needs the upper layer where it is");
}

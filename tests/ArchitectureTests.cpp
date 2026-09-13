#include "SourceTree.h"
#include "TestSupport.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Where code is allowed to live: the layer directions, the line ratchets, and
// the concepts this tree insists are spelled out in exactly one file.
//
// They exist because the failure modes below are silent -- the code compiles,
// the tests pass, and the damage shows up later as a cycle nothing failed on or
// a catch-all nobody noticed growing. The rules about *two lists that must stay
// one* are `RegistryTests.cpp`; this file is about placement.

using micronotes::tests::readText;
using micronotes::tests::repoRoot;
using micronotes::tests::sourceFiles;
using micronotes::tests::lineCount;

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
constexpr int kApplicationLineBudget = 170;
constexpr int kShellFileLineBudget = 1000;
// And the same rule again, over every source in the tree rather than the shell
// alone -- because the shell ceiling did not stop a catch-all either, it only
// stopped the ones in `src/app/`. While it was the only one, `doc/Layout.cpp`
// reached 1,732 lines and `tools/PerfMain.cpp` 1,553, which are the two largest
// files this project has ever had, and both grew to that size with the ceiling
// green the whole way. Sizes rather than one number because these are ratchets
// at what the tree measures today, not targets: a change that moves behaviour
// into a named unit lowers them in the same commit, and nothing raises them.
constexpr int kTreeFileLineBudget = 826;

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
// `tools/` and `tests/` count. Neither is shipped code, which is exactly the
// reasoning that let both grow: the harness reached 1,553 lines and
// `LayoutTests.cpp` 1,878, which made it the largest file in the repository
// while every ceiling stayed green. The harness is the instrument every
// performance claim in `docs/performance.md` rests on, and the suite is where
// somebody goes to add a test -- a reader who cannot find the lane, or the file
// a subject's tests live in, is the reader who adds neither.
MICRONOTES_TEST(architecture_no_source_in_the_tree_is_a_catch_all) {
  std::vector<std::pair<std::string, int>> offenders;
  for(const auto& root : {"src", "tools", "tests"}) {
    for(const auto& path : sourceFiles(repoRoot() / root)) {
      const int lines = lineCount(path);
      if(lines <= kTreeFileLineBudget) continue;
      // The path relative to the repo, not just the filename: two directories
      // hold a `Layout.cpp` and the message has to say which one.
      offenders.push_back({std::filesystem::relative(path, repoRoot()).string(), lines});
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

// The per-library state directory is named once.
//
// A library root holds the person's notes and one directory of ours --
// `microcore::kAppDotDir`, `.micronotes` here -- with the attachments, the
// trash and the recovery queue under it. `kAppDotDir` exists so that name is a
// compile-time constant off `MICROCORE_APP_NAME`, and it was used by exactly
// one of the seven sites that needed it: the tree walk that prunes the
// directory. Every site that *wrote* into it concatenated the literal.
//
// So the walk skipped a directory named after the app while the writers named a
// hard-coded one, and the two agreed only because nobody had renamed the app.
// Renaming it would not have failed a test or a build; it would have produced a
// library whose notes list a trash the trash does not write to.
//
// The derived paths are `library/StatePaths.h`. Tests are exempt: a test that
// asserts a file landed in `.micronotes/trash/files` is checking the layout
// from outside, which is the only place that check means anything.
MICRONOTES_TEST(architecture_the_state_directory_is_named_once) {
  std::vector<std::string> offenders;
  for(const auto& file : sourceFiles(repoRoot() / "src")) {
    // Where the name is defined, and where its history is written down.
    if(file.filename() == "AppIdentity.h" || file.filename() == "StatePaths.h") continue;
    std::istringstream lines(readText(file));
    std::string line;
    int number = 0;
    while(std::getline(lines, line)) {
      ++number;
      const auto code = line.substr(0, line.find("//"));
      if(code.find("\".micronotes\"") == std::string::npos) continue;
      offenders.push_back(file.filename().string() + ":" + std::to_string(number));
    }
  }
  std::string message =
    "the per-library state directory is spelled out as a literal instead of coming from "
    "library/StatePaths.h (or microcore::kAppDotDir):\n";
  for(const auto& hit : offenders) message += "  " + hit + "\n";
  message +=
    "use stateDir / attachmentsDir / trashFilesDir / trashIndexPath / recoveryDir. A literal "
    "here is a path that stops matching the one the tree walk prunes the moment the app is "
    "renamed, and nothing fails when it does.";
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
// md4c's render model is laid out in exactly one place.
//
// A `markdown::Block`'s `inlines` and `tableRows` are the model, and turning
// them into lines is a *decision* -- where the text breaks, what a cell is set
// in, which bytes are a `[[wikilink]]`. It was made twice: once in `src/app/`
// with a line breaker of its own beside the tested one, and once in
// `src/export/` with none at all, which is why a table cell exported as plain
// text and a paragraph beside a table did not export at all.
//
// Reading these fields anywhere above `doc/` is that split starting again. The
// answer is `doc/RenderLayout.h`, which hands back the same `doc::BlockLayout`
// the note's own blocks produce, and painting one of those is what a surface
// is allowed to do.
MICRONOTES_TEST(architecture_the_md4c_render_model_is_laid_out_in_one_place) {
  const std::vector<std::string> above {"library", "ui", "export", "app"};
  std::string offenders;
  for(const auto& layer : above) {
    for(const auto& path : sourceFiles(repoRoot() / "src" / layer)) {
      const std::string text = readText(path);
      for(const char* field : {".tableRows", "->tableRows"}) {
        if(text.find(field) == std::string::npos) continue;
        if(!offenders.empty()) offenders += "; ";
        offenders += "src/" + layer + "/" + path.filename().string() + " reads " + field;
      }
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "md4c's render model is read above doc/: " + offenders +
      " -- laying it out is a decision, and a decision made in two layers is "
      "one that drifts. Lay it out with doc/RenderLayout.h and paint the "
      "doc::BlockLayout it hands back");
}

MICRONOTES_TEST(architecture_the_layers_only_point_one_way) {
  // Lowest first. A layer may include itself and anything before it.
  const std::vector<std::string> layers {"core", "doc", "library", "ui", "export", "app"};

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
    " -- the order is core < doc < library < ui < export < app. Whatever is wanted from the upper "
    "layer is "
    "either in the wrong place or is two things: move the half the lower layer needs down, and "
    "leave the half that needs the upper layer where it is");
}

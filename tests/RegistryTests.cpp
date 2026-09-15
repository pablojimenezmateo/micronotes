#include "SourceTree.h"
#include "TestSupport.h"

#include "app/Commands.h"

#include "core/perf/PerformanceCounters.h"
#include "ui/Actions.h"
#include "ui/Menus.h"

#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <vector>

// One list rather than two.
//
// Every rule here is the same rule: some fact about this program is written
// down in two places, and the two are free to drift with nothing failing to
// compile. A counter declared and never incremented. A command in the palette
// and in no dispatch. A menu row's meaning carried by which trailing `false`
// became a `true`. A run inked by a third loop neither painter knows about. In
// every case the symptom is a feature that quietly does nothing, so the test
// compares the two lists directly rather than grepping for one of them.
//
// The rules about *where code lives* -- the layers, the line ratchets -- are
// `ArchitectureTests.cpp`.

using micronotes::tests::readText;
using micronotes::tests::repoRoot;
using micronotes::tests::sourceFiles;
using micronotes::tests::withoutLineComments;

namespace {

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

// The right panel is drawn after the content, and that is a performance
// contract rather than a matter of taste.
//
// The panel's outline borrows the block partition the reading page splices during
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
    "drawApp draws the right panel before the content. Its outline then asks the reading page for a "
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
// Every action a surface offers reaches a command, and every command is an
// action a surface could offer.
//
// This used to read `src/app/*.cpp` as text and look for the literal
// `== "name"`, because the dispatch was a ninety-branch `if`/`else if` chain
// and a source grep was the only way to see into it. That check could not tell
// whether the branch it found was *reachable* -- one nested inside another
// id's `if`, or one after an arm that already matched, passed it -- and it had
// to assert `performCommand`'s own signature line still existed so it would
// not silently scan for a spelling nothing used any more. See TD-46, which
// this closes.
//
// `commandSpecs()` is a table now, so both directions are a set comparison.
// They are different failures and both are silent:
//
//   * an action with no command is a menu row or a shortcut that does nothing
//     when used -- which `F2`, `Ctrl+Q` and `Ctrl+O` all once were;
//   * a command with no action is a command no surface can reach, which is
//     dead code that looks live.
MICRONOTES_TEST(architecture_every_offered_action_is_dispatched) {
  std::set<std::string> offered;
  for(const auto& spec : micronotes::ui::actionSpecs()) {
    // Everything the command palette lists.
    if(spec.inPalette) offered.insert(std::string(spec.name));
    // And everything a key alone runs, which the palette does not have to
    // list: the editing verbs act on a selection the palette has just taken
    // the focus away from, so they carry `inPalette = false` and were checked
    // by nothing. `keyRunsIt` means `handleKey` hands the name straight to
    // performCommand, so a name with no command is a dead shortcut.
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

  std::set<std::string> dispatched;
  for(const auto& command : micronotes::app::commandSpecs()) {
    dispatched.insert(std::string(command.name));
  }

  std::string missing;
  for(const auto& name : offered) {
    if(dispatched.count(name) != 0) continue;
    if(!missing.empty()) missing += ", ";
    missing += name;
  }
  micronotes::tests::require(
    missing.empty(),
    "actions the palette or the menu bar offer but `commandSpecs()` does not carry: " + missing +
    " -- clicking one of these does nothing at all, and nothing else notices");
}

// The other direction, and the row's own shape.
//
// A command's name is an action's name: that is what makes the two tables
// comparable at all, and it is what catches the three path rows -- which pass
// their own id on to `handleNotePathCommand` -- coming to disagree with the
// key they are filed under. And a duplicate name is a row that can never run,
// which the chain hid just as well as a table does: the second `== "save"`
// was as unreachable as the second `{"save", ...}` is.
MICRONOTES_TEST(architecture_every_command_is_an_action_and_is_named_once) {
  std::set<std::string> seen;
  for(const auto& command : micronotes::app::commandSpecs()) {
    const std::string name(command.name);
    micronotes::tests::require(!name.empty(), "a command row has no name");
    micronotes::tests::require(command.run != nullptr, "command '" + name + "' does nothing");
    micronotes::tests::require(micronotes::ui::findAction(command.name) != nullptr,
                               "command '" + name +
                                 "' is not an action, so no surface can reach it");
    micronotes::tests::require(seen.insert(name).second,
                               "two command rows are named '" + name +
                                 "', and the second can never run");
  }
  MICRONOTES_REQUIRE(seen.size() == micronotes::ui::actionSpecs().size());
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

// A run's appearance is decided once per output surface, not once per loop.
//
// This was TD-43 and it had already cost something. `app/PageViewPaint.cpp`
// walked a `doc::BlockLayout`'s lines and runs for the note's own blocks and
// `app/MarkdownBlocks.cpp` walked the same fields of the same type for the
// blocks md4c lays out, and the two made the same decisions from the same
// fields: the tinted ground behind a code span, the strikethrough, the link
// rule, the rect handed back to a click. They drifted -- the strikethrough at
// 0.55 of the line box in one and 0.45 in the other, the link rule two pixels
// above the bottom in one and four in the other -- so a struck word inside a
// table wore its line lower than the same word in the paragraph above it.
//
// There are exactly two outputs, so there are exactly two of these loops:
// `ui/DocRuns.cpp` draws one onto a window and `export/PdfBlocks.cpp` paints
// one onto a page. A third file reading these fields is a third loop, and the
// fields are the tell -- `style.strike` and `linkIndex` are only interesting to
// something deciding how a run *looks*.
MICRONOTES_TEST(architecture_a_run_is_inked_once_per_surface) {
  // `DocRuns.h` is the third only in file count: it holds `forEachCodeSpan`,
  // which is the one decision the two painters make *identically* -- where the
  // tinted ground behind a code span begins and ends -- and it is here rather
  // than in each loop precisely so there is one answer instead of two.
  const std::set<std::string> painters {"DocRuns.h", "DocRuns.cpp", "PdfBlocks.cpp"};
  // Where the fields are *set* rather than read: the tokenizer fills them in.
  const std::set<std::string> producers {"Flow.h"};
  std::string offenders;
  for(const auto& path : sourceFiles(repoRoot() / "src")) {
    const std::string name = path.filename().string();
    if(painters.count(name) != 0 || producers.count(name) != 0) continue;
    const std::string text = readText(path);
    for(const char* field : {"run.style.strike", "run.linkIndex", "run.isMarker"}) {
      if(text.find(field) == std::string::npos) continue;
      if(!offenders.empty()) offenders += "; ";
      offenders += name + " reads " + field;
    }
  }
  micronotes::tests::require(
    offenders.empty(),
    "a run's appearance is decided outside the two run painters: " + offenders +
      " -- there are two output surfaces, a window and a page, so there are two "
      "loops: ui::paintRuns and exporting::paintRuns. A third copy of those "
      "decisions is a third set of them to drift, which is what TD-43 was. Pass "
      "what your surface knows through ui::RunPaint instead");
}

// A menu row says which field it sets.
//
// `ui::OverlayItem` has eight fields, four of them bools, and every menu the
// overlay backs used to be written as a braced list of all eight -- so a row's
// meaning was carried by which trailing `false` had become a `true`, and the
// rule between two groups was `{"", "", "", "", false, false, false, true}`
// fifteen times over. `ui::menuItem` and `ui::menuSeparator` name them.
//
// Asserted on the separator, because it is the one shape that is identical at
// every site: a reappearance of it means someone has gone back to writing the
// aggregate out, and the four bools go with it.
MICRONOTES_TEST(architecture_a_menu_row_names_the_field_it_sets) {
  std::string offenders;
  for(const auto& path : sourceFiles(repoRoot() / "src")) {
    const std::string text = readText(path);
    // The definition of `menuSeparator` itself sets the two fields by name and
    // is where this shape is allowed to be spelled at all.
    if(path.filename() == "Overlay.h") continue;
    if(text.find("\"\", \"\", \"\", \"\", false") == std::string::npos) continue;
    if(!offenders.empty()) offenders += ", ";
    offenders += path.filename().string();
  }
  micronotes::tests::require(
    offenders.empty(),
    "an overlay row written as its eight fields in order: " + offenders +
      " -- use ui::menuItem and ui::menuSeparator, so a row differs from its "
      "neighbours in a field with a name rather than in which of four trailing "
      "bools is set");
}

// A read of the note index cannot skip the flush a save left for it.
//
// `LibraryIndex` defers the half of a save that scales with the note -- the
// body stored in `notes`, the same bytes tokenised into `notes_fts`, the links
// -- and runs it before the next read (TD-48). That contract has one failure
// mode and it is silent: a *new* public read, added past the flush, answers
// from tables that are one save behind. Nothing crashes; a search stops finding
// a word that is on screen, once, until something else reads the index.
//
// So the public read surface is enumerated here, and every name on it is either
// one this file says flushes -- checked against the source, not just listed --
// or one it says reaches no table at all. Adding a public `const` method to
// `LibraryIndex` fails this test until it has been put in one of the two.
// `library_index_every_public_read_sees_a_deferred_save` is the other end: it
// asks each of the flushing reads the question with a save standing.
MICRONOTES_TEST(architecture_a_library_index_read_cannot_skip_the_flush) {
  const auto header = repoRoot() / "src/library/LibraryIndex.h";
  const std::string text = readText(header);
  const auto classAt = text.find("class LibraryIndex {");
  micronotes::tests::require(classAt != std::string::npos,
                             "src/library/LibraryIndex.h no longer declares class LibraryIndex, so "
                             "this test would scan nothing and pass forever");
  const auto privateAt = text.find("\nprivate:", classAt);
  micronotes::tests::require(privateAt != std::string::npos,
                             "class LibraryIndex has no private section; this test reads the "
                             "public one as everything before it");
  // Comments stripped first: this class documents itself heavily and prose
  // about "one `count(*)` over the primary key" parses as a declaration.
  const std::string publicSection = withoutLineComments(text.substr(classAt, privateAt - classAt));

  // Every public declaration that ends in `const;` -- which for this class is
  // exactly its reads, because nothing const about a library index is anything
  // else.
  std::set<std::string> reads;
  const std::regex declaration(R"(\b(\w+)\s*\([^;{]*\)\s*const\s*;)");
  for(std::sregex_iterator it(publicSection.begin(), publicSection.end(), declaration), last;
      it != last; ++it) {
    reads.insert((*it)[1].str());
  }

  // Reads that go to the tables, and so must run what a save deferred.
  const std::set<std::string> flushes {"search", "backlinks", "size", "notes", "ftsStoresBodies"};
  // Reads that touch no table: `isOpen` asks the connection, and the other two
  // hand back vectors the last tree walk filled.
  const std::set<std::string> tableless {"isOpen", "directories", "companions"};

  std::string unaccounted;
  for(const auto& name : reads) {
    if(flushes.count(name) || tableless.count(name)) continue;
    unaccounted += (unaccounted.empty() ? "" : ", ") + name;
  }
  micronotes::tests::require(
    unaccounted.empty(),
    "these public reads of LibraryIndex are not accounted for: " + unaccounted +
      " -- a save defers the note-sized half of its index write and a read runs it "
      "first, so a read added past flushDeferred() answers from tables one save "
      "behind. Call flushDeferred() and list it in this test's `flushes`, or, if it "
      "genuinely reaches no table, in `tableless`");

  // Listed is not wired. Each flushing read is checked against its own
  // definition, because the way this contract actually breaks is a name that
  // was added to the list above and to nothing else.
  const std::filesystem::path sources[] = {repoRoot() / "src/library/LibraryIndex.cpp",
                                           repoRoot() / "src/library/LibraryIndexSearch.cpp"};
  std::string bodies;
  for(const auto& source : sources) bodies += readText(source);
  std::string missing;
  for(const auto& name : flushes) {
    micronotes::tests::require(reads.count(name) == 1,
                               "this test lists `" + name +
                                 "` as a flushing read of LibraryIndex, but the header declares no "
                                 "such public const method -- the list has outlived the method");
    const auto at = bodies.find("LibraryIndex::" + name + "(");
    micronotes::tests::require(at != std::string::npos,
                               "no definition of LibraryIndex::" + name + " under src/library/");
    // As far as the function's closing brace, which for this file's style is a
    // `}` alone at the start of a line.
    const auto end = bodies.find("\n}\n", at);
    const std::string body = bodies.substr(at, end == std::string::npos ? end : end - at);
    if(body.find("flushDeferred()") == std::string::npos) {
      missing += (missing.empty() ? "" : ", ") + name;
    }
  }
  micronotes::tests::require(missing.empty(),
                             "these LibraryIndex reads are listed as flushing but do not call "
                             "flushDeferred(): " +
                               missing);
}

// Every write of a note's file, enumerated, for the reason
// `architecture_a_library_index_read_cannot_skip_the_flush` enumerates the
// index's reads: the rule is one a *caller* can forget, and forgetting it here
// destroys somebody's work rather than showing a stale list.
//
// AGENTS.md states it: never write a note's file without checking what is
// there. `ui::OpenNoteRecord` carries a `platform::FileSignature` per open note
// and `AppState::writeOpenNote` compares it before writing, so a path that
// reaches `catalog_.writeNote` without going through it can overwrite an edit
// made in another program -- or, for the open note, the buffer on screen.
//
// Three call sites are accounted for and each is a different answer:
//
//   * `writeOpenNote` is the guarded path itself.
//   * `removeTagEverywhere` routes the *open* note through `writeOpenNote` with
//     the buffer's body, and every other note through a read-modify-write of
//     its own file, which cannot be behind anything.
//   * `appendToNote` is a read-modify-write too, and refuses the open note.
//
// A fourth site is one of those three or it is a new answer that has to be
// written down here. This is deliberately over the file rather than over the
// class: a note write added anywhere in `AppState.cpp` is what it has to catch.
MICRONOTES_TEST(architecture_a_note_write_cannot_skip_the_signature_check) {
  const auto source = repoRoot() / "src/ui/AppState.cpp";
  const std::string text = withoutLineComments(readText(source));
  micronotes::tests::require(text.find("AppState::writeOpenNote") != std::string::npos,
                             "src/ui/AppState.cpp no longer defines writeOpenNote, so this test "
                             "would scan for a rule that has moved and pass forever");

  // Which `AppState::` method each `catalog_.writeNote...` call sits inside:
  // the nearest definition above it.
  const std::regex marker(R"(AppState::(\w+)\s*\(|catalog_\.(writeNoteAs|writeNote)\s*\()");
  std::string enclosing;
  std::set<std::string> writers;
  for(std::sregex_iterator it(text.begin(), text.end(), marker), last; it != last; ++it) {
    if((*it)[1].matched) {
      enclosing = (*it)[1].str();
      continue;
    }
    writers.insert(enclosing.empty() ? "<file scope>" : enclosing);
  }
  micronotes::tests::require(!writers.empty(),
                             "no catalog_.writeNote call sites found in AppState.cpp -- the "
                             "spelling changed and this test now guards nothing");

  const std::set<std::string> accounted {"writeOpenNote", "removeTagEverywhere", "appendToNote"};
  std::string unaccounted;
  for(const auto& name : writers) {
    if(accounted.count(name)) continue;
    unaccounted += (unaccounted.empty() ? "" : ", ") + name;
  }
  micronotes::tests::require(
    unaccounted.empty(),
    "these methods write a note's file without being accounted for: " + unaccounted +
      " -- every write must either go through AppState::writeOpenNote, which compares the "
      "file's signature before writing, or be a read-modify-write of a note that is not the "
      "open one. Say which it is in the test above, or route it through writeOpenNote.");
}

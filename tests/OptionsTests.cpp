#include "TestSupport.h"

#include "app/Application.h"

#include <string>
#include <vector>

// What the command line asked for.
//
// `parseArgs` is described in its own file as "the one part of startup that
// could be tested without a window", and nothing tested it -- which is how
// `--pane live` came to be silently ignored for a release after the live view
// was removed, taking a third of `session-compare.sh`'s rounds with it.

namespace {

micronotes::app::ApplicationOptions parse(std::vector<std::string> args) {
  std::vector<char*> argv;
  std::string program = "micronotes";
  argv.push_back(program.data());
  for(auto& arg : args) argv.push_back(arg.data());
  return micronotes::app::parseArgs(static_cast<int>(argv.size()), argv.data());
}

}

MICRONOTES_TEST(options_reads_the_three_pane_arrangements_by_either_name) {
  MICRONOTES_REQUIRE(parse({"--pane", "raw"}).paneMode == 0);
  MICRONOTES_REQUIRE(parse({"--pane", "editor"}).paneMode == 0);
  MICRONOTES_REQUIRE(parse({"--pane", "reading"}).paneMode == 1);
  MICRONOTES_REQUIRE(parse({"--pane", "viewer"}).paneMode == 1);
  MICRONOTES_REQUIRE(parse({"--pane", "split"}).paneMode == 2);
  for(const char* name : {"raw", "editor", "reading", "viewer", "split"}) {
    MICRONOTES_REQUIRE(parse({"--pane", name}).problems.empty());
  }
}

// The arrangement that used to exist. Left unset *and* reported: a capture that
// silently falls back to whatever the state file was in is a capture of the
// wrong surface that looks exactly like a capture of the right one.
MICRONOTES_TEST(options_refuses_a_pane_this_build_does_not_have) {
  const auto options = parse({"--pane", "live"});
  MICRONOTES_REQUIRE(!options.paneMode.has_value());
  MICRONOTES_REQUIRE(options.problems.size() == 1);
  MICRONOTES_REQUIRE(options.problems.front().find("live") != std::string::npos);
  MICRONOTES_REQUIRE(options.problems.front().find("split") != std::string::npos);
}

MICRONOTES_TEST(options_leaves_the_pane_alone_when_nothing_asks) {
  const auto options = parse({"--size", "800x600"});
  MICRONOTES_REQUIRE(!options.paneMode.has_value());
  MICRONOTES_REQUIRE(options.problems.empty());
  MICRONOTES_REQUIRE(options.windowWidth == 800);
  MICRONOTES_REQUIRE(options.windowHeight == 600);
}

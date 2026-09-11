#pragma once

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace micronotes::tests {

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* name, TestFn fn);
};

void require(bool condition, const std::string& message);

}

#define MICRONOTES_TEST(name) \
  static void name(); \
  static micronotes::tests::Registrar name##_registrar(#name, &name); \
  static void name()

#define MICRONOTES_REQUIRE(condition) \
  micronotes::tests::require((condition), #condition)

namespace micronotes::tests {

// What the suite would have handed to the desktop, had it been allowed to.
//
// `main` points `app::setSpawnForTesting` at a recorder before the first test
// runs, so no test can fork `xdg-open`. That is not tidiness: `MenuActionTests`
// runs every row of every context menu, five of which carry "Show on disk", so
// the suite opened five or six file manager windows on the developer's desktop
// every single `ctest`. Nothing failed, which is why it went unnoticed for as
// long as it did.
//
// A recorder rather than a no-op, because "what would you have launched" is a
// better assertion than "did something happen" -- see
// `show_on_disk_asks_the_desktop_for_the_containing_directory`.
std::vector<std::vector<std::string>>& desktopLaunches();

}

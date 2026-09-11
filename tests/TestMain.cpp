#include "TestSupport.h"

#include "app/Desktop.h"

namespace micronotes::tests {

std::vector<std::vector<std::string>>& desktopLaunches() {
  static std::vector<std::vector<std::string>> launches;
  return launches;
}

namespace {

bool recordLaunch(const std::vector<std::string>& command) {
  desktopLaunches().push_back(command);
  // True, as a successful launch would answer: a test asserting on what the
  // shell *said* should see the message a working desktop produces, not the
  // "could not open the file manager" fallback.
  return true;
}

}

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

Registrar::Registrar(const char* name, TestFn fn) {
  registry().push_back({name, fn});
}

void require(bool condition, const std::string& message) {
  if(!condition) throw std::runtime_error(message);
}

}

int main() {
  // Before the first test, and with no way for one to forget: a test must not
  // reach the desktop. See `micronotes::tests::desktopLaunches`.
  micronotes::app::setSpawnForTesting(&micronotes::tests::recordLaunch);

  int failed = 0;
  for(const auto& test : micronotes::tests::registry()) {
    try {
      test.fn();
      std::cout << "[PASS] " << test.name << "\n";
    } catch(const std::exception& ex) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << "\n";
    }
  }
  return failed == 0 ? 0 : 1;
}

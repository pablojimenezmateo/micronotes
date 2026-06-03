#include "TestSupport.h"

namespace micronotes::tests {

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

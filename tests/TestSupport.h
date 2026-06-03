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

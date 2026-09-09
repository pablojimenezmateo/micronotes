#pragma once

#include <cstdlib>
#include <string_view>

// Whether MICRONOTES_DEBUG_INPUT is set, read once.
//
// A switch four files consult -- the loop, the clipboard, the key router and
// the desktop integration -- and none of them wants the shell's state to ask
// it. Read once into a function-local static: the environment does not change
// under a running process, and this sits on the keystroke path.
namespace micronotes::app {

inline bool inputDebugEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("MICRONOTES_DEBUG_INPUT");
    return value && *value && std::string_view(value) != "0";
  }();
  return enabled;
}

}

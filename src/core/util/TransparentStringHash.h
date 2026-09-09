#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace microcore::util {

// Heterogeneous hash for a string-keyed unordered container: paired with
// `std::equal_to<>` it lets `find`, `count` and `erase` take a
// `std::string_view` without building a `std::string` for the probe.
//
// Usage: `std::unordered_map<std::string, V, TransparentStringHash, std::equal_to<>>`.
//
// Stored keys and view lookups both hash through `std::hash<std::string_view>`,
// which is what keeps the two consistent -- a container that hashed its owned
// keys one way and its probes another would simply never find anything.
//
// Written out twice before this existed, as `SqlHash` in the sqlite wrapper and
// `TransparentStringHash` in the trace channel, with a comment each explaining
// the same allocation. The sibling `../microide` has had it as one util for
// long enough that ten of its containers use it.
struct TransparentStringHash {
  using is_transparent = void;
  std::size_t operator()(std::string_view value) const noexcept {
    return std::hash<std::string_view> {}(value);
  }
};

}

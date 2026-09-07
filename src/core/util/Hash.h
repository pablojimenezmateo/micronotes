#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace microcore::util {

// FNV-1a, widened to a machine word.
//
// Four copies of this used to live in the tree -- the layout's block
// fingerprint, the measure cache's key, the texture cache's key and the
// library's fallback note id -- and they had drifted into two different
// algorithms. Two of them consumed a word at a time; the texture cache, which
// is the hottest of the four (once per drawn run per frame, and twice on a
// miss), still walked bytes. Nothing chose that: it was simply the copy nobody
// went back to.
//
// So the fast form lives here once and everything keys off it. Values are
// process-local -- they key in-memory caches and are never written to disk --
// so changing the algorithm is free, which is the whole reason it is safe to
// have one.

inline constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

// The word-at-a-time loop. The extra shift-xor is what keeps a whole word's
// worth of entropy from being lost to FNV's single multiply: without it, byte
// order inside a word barely moves the result.
inline std::uint64_t hashBytes(std::uint64_t seed, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const unsigned char*>(data);
  std::size_t i = 0;
  for(; i + sizeof(std::uint64_t) <= size; i += sizeof(std::uint64_t)) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes + i, sizeof(word));
    seed = (seed ^ word) * kFnvPrime;
    seed ^= seed >> 29;
  }
  for(; i < size; ++i) {
    seed = (seed ^ bytes[i]) * kFnvPrime;
  }
  return seed;
}

inline std::uint64_t hashBytes(std::uint64_t seed, std::string_view text) {
  return hashBytes(seed, text.data(), text.size());
}

// One trivially-copyable value. The caller is responsible for the type having
// no padding when it is a struct: padding bytes are indeterminate, so hashing
// them mixes whatever the stack held into the key, and the failure mode is a
// cache that quietly stops hitting rather than anything that looks like a bug.
template <typename T>
inline std::uint64_t hashValue(std::uint64_t seed, const T& value) {
  return hashBytes(seed, &value, sizeof(T));
}

inline std::uint64_t hashByte(std::uint64_t seed, unsigned char value) {
  return (seed ^ value) * kFnvPrime;
}

}

#include "TestSupport.h"

#include "CoreAliases.h"

#include "core/util/Utf8.h"

using micronotes::util::CodePoint;
using micronotes::util::decodeAt;

// Decoding is the one thing this header does that is not boundary arithmetic,
// and it is the half that has to be right about *malformed* input: the bytes
// come out of a note somebody typed, and a note may hold anything.

MICRONOTES_TEST(utf8_decodes_each_sequence_length) {
  // ASCII, two bytes, three bytes, four bytes.
  const std::string text = "Aé€\U0001F600";
  CodePoint point = decodeAt(text, 0);
  MICRONOTES_REQUIRE(point.value == U'A');
  MICRONOTES_REQUIRE(point.next == 1);

  point = decodeAt(text, point.next);
  MICRONOTES_REQUIRE(point.value == 0x00E9);
  MICRONOTES_REQUIRE(point.next == 3);

  point = decodeAt(text, point.next);
  MICRONOTES_REQUIRE(point.value == 0x20AC);
  MICRONOTES_REQUIRE(point.next == 6);

  point = decodeAt(text, point.next);
  MICRONOTES_REQUIRE(point.value == 0x1F600);
  MICRONOTES_REQUIRE(point.next == 10);
  MICRONOTES_REQUIRE(point.next == text.size());
}

// The contract that matters: a walk over broken bytes terminates, and it stays
// in step with the offsets around it rather than resynchronising somewhere
// else. A decoder that returns `next == offset` on bad input hangs whatever is
// walking it.
MICRONOTES_TEST(utf8_decoding_broken_bytes_always_advances) {
  const std::string broken("\xFF\x80\xC3\xE2\x82", 5);
  std::size_t at = 0;
  int steps = 0;
  while(at < broken.size()) {
    const CodePoint point = decodeAt(broken, at);
    MICRONOTES_REQUIRE(point.next > at);
    MICRONOTES_REQUIRE(point.value == 0xFFFD);
    at = point.next;
    ++steps;
  }
  MICRONOTES_REQUIRE(steps > 0);
}

// A surrogate half is ill-formed in UTF-8 however it was encoded, and a font
// asked for one would answer with whatever glyph that number happens to name.
MICRONOTES_TEST(utf8_refuses_a_surrogate_encoded_as_three_bytes) {
  const std::string surrogate("\xED\xA0\x80", 3);
  const CodePoint point = decodeAt(surrogate, 0);
  MICRONOTES_REQUIRE(point.value == 0xFFFD);
  MICRONOTES_REQUIRE(point.next == 3);
}

MICRONOTES_TEST(utf8_decoding_past_the_end_stays_at_the_end) {
  const std::string text = "ab";
  const CodePoint point = decodeAt(text, 9);
  MICRONOTES_REQUIRE(point.next == text.size());
  MICRONOTES_REQUIRE(point.value == 0);
}

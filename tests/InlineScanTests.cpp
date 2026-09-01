#include "TestSupport.h"

#include "doc/InlineScan.h"

#include <string>

using micronotes::doc::SourceSpan;
using micronotes::doc::SpanKind;
using micronotes::doc::scanInlines;

namespace {

const SourceSpan* find(const std::vector<SourceSpan>& spans, SpanKind kind, std::size_t nth = 0) {
  std::size_t seen = 0;
  for(const auto& span : spans) {
    if(span.kind != kind) continue;
    if(seen++ == nth) return &span;
  }
  return nullptr;
}

std::string slice(const std::string& text, std::size_t from, std::size_t to) {
  return text.substr(from, to - from);
}

}

MICRONOTES_TEST(inline_scan_separates_markers_from_content) {
  const std::string text = "a **bold** and *soft* and ~~gone~~";
  const auto spans = scanInlines(text);
  const auto* strong = find(spans, SpanKind::Strong);
  MICRONOTES_REQUIRE(strong != nullptr);
  MICRONOTES_REQUIRE(slice(text, strong->openStart, strong->openEnd) == "**");
  MICRONOTES_REQUIRE(slice(text, strong->contentStart, strong->contentEnd) == "bold");
  MICRONOTES_REQUIRE(slice(text, strong->closeStart, strong->closeEnd) == "**");
  const auto* emphasis = find(spans, SpanKind::Emphasis);
  MICRONOTES_REQUIRE(emphasis != nullptr);
  MICRONOTES_REQUIRE(slice(text, emphasis->contentStart, emphasis->contentEnd) == "soft");
  const auto* strike = find(spans, SpanKind::Strike);
  MICRONOTES_REQUIRE(strike != nullptr);
  MICRONOTES_REQUIRE(slice(text, strike->contentStart, strike->contentEnd) == "gone");
}

MICRONOTES_TEST(inline_scan_applies_a_base_offset) {
  const std::string text = "**x**";
  const auto spans = scanInlines(text, 100);
  MICRONOTES_REQUIRE(spans.size() == 1);
  MICRONOTES_REQUIRE(spans[0].start == 100);
  MICRONOTES_REQUIRE(spans[0].contentStart == 102);
  MICRONOTES_REQUIRE(spans[0].end == 105);
}

MICRONOTES_TEST(inline_scan_nests_emphasis_inside_strong) {
  const std::string text = "**bold *and italic* here**";
  const auto spans = scanInlines(text);
  const auto* strong = find(spans, SpanKind::Strong);
  const auto* emphasis = find(spans, SpanKind::Emphasis);
  MICRONOTES_REQUIRE(strong != nullptr && emphasis != nullptr);
  MICRONOTES_REQUIRE(strong->contentStart <= emphasis->start && emphasis->end <= strong->contentEnd);
  MICRONOTES_REQUIRE(emphasis->depth > strong->depth);
}

MICRONOTES_TEST(inline_scan_leaves_unmatched_delimiters_alone) {
  const std::string text = "a * b _ c ** d";
  const auto spans = scanInlines(text);
  MICRONOTES_REQUIRE(spans.empty());
}

MICRONOTES_TEST(inline_scan_keeps_intraword_underscores_literal) {
  const std::string text = "snake_case_name and _real_ emphasis";
  const auto spans = scanInlines(text);
  MICRONOTES_REQUIRE(spans.size() == 1);
  MICRONOTES_REQUIRE(slice(text, spans[0].contentStart, spans[0].contentEnd) == "real");
}

MICRONOTES_TEST(inline_scan_gives_code_spans_priority) {
  const std::string text = "use `a **b** c` verbatim";
  const auto spans = scanInlines(text);
  MICRONOTES_REQUIRE(spans.size() == 1);
  MICRONOTES_REQUIRE(spans[0].kind == SpanKind::Code);
  MICRONOTES_REQUIRE(slice(text, spans[0].contentStart, spans[0].contentEnd) == "a **b** c");
}

MICRONOTES_TEST(inline_scan_records_links_images_and_autolinks) {
  const std::string text = "see [the **doc**](docs/a.md \"title\") and ![alt](img.png) and <https://example.com>";
  const auto spans = scanInlines(text);
  const auto* link = find(spans, SpanKind::Link);
  MICRONOTES_REQUIRE(link != nullptr);
  MICRONOTES_REQUIRE(link->target == "docs/a.md");
  MICRONOTES_REQUIRE(slice(text, link->contentStart, link->contentEnd) == "the **doc**");
  MICRONOTES_REQUIRE(slice(text, link->openStart, link->openEnd) == "[");
  MICRONOTES_REQUIRE(text.compare(link->closeStart, 2, "](") == 0);
  MICRONOTES_REQUIRE(find(spans, SpanKind::Strong) != nullptr);
  const auto* image = find(spans, SpanKind::Image);
  MICRONOTES_REQUIRE(image != nullptr && image->target == "img.png");
  MICRONOTES_REQUIRE(slice(text, image->openStart, image->openEnd) == "![");
  const auto* autolink = find(spans, SpanKind::Autolink);
  MICRONOTES_REQUIRE(autolink != nullptr && autolink->target == "https://example.com");
}

MICRONOTES_TEST(inline_scan_honors_backslash_escapes) {
  const std::string text = "\\*not emphasis\\* at all";
  const auto spans = scanInlines(text);
  MICRONOTES_REQUIRE(find(spans, SpanKind::Emphasis) == nullptr);
  const auto* escape = find(spans, SpanKind::Escape);
  MICRONOTES_REQUIRE(escape != nullptr);
  MICRONOTES_REQUIRE(slice(text, escape->openStart, escape->openEnd) == "\\");
  MICRONOTES_REQUIRE(slice(text, escape->contentStart, escape->contentEnd) == "*");
}

MICRONOTES_TEST(inline_scan_keeps_utf8_boundaries) {
  const std::string text = "**caf\xc3\xa9 \xe2\x9c\x93**";
  const auto spans = scanInlines(text);
  MICRONOTES_REQUIRE(spans.size() == 1);
  MICRONOTES_REQUIRE(slice(text, spans[0].contentStart, spans[0].contentEnd) == "caf\xc3\xa9 \xe2\x9c\x93");
  for(const auto& span : spans) {
    MICRONOTES_REQUIRE((static_cast<unsigned char>(text[span.contentStart]) & 0xC0) != 0x80);
  }
}

MICRONOTES_TEST(inline_scan_never_reports_ranges_outside_the_text) {
  const std::string text = "***triple*** `unclosed ![x](y) _z_";
  const auto spans = scanInlines(text);
  for(const auto& span : spans) {
    MICRONOTES_REQUIRE(span.start <= span.openEnd && span.openEnd <= span.contentStart);
    MICRONOTES_REQUIRE(span.contentStart <= span.contentEnd && span.contentEnd <= span.closeStart);
    MICRONOTES_REQUIRE(span.closeEnd == span.end && span.end <= text.size());
  }
}

namespace {

const micronotes::doc::SourceSpan* firstWikiLink(const std::vector<micronotes::doc::SourceSpan>& spans) {
  for(const auto& span : spans) {
    if(span.kind == micronotes::doc::SpanKind::WikiLink) return &span;
  }
  return nullptr;
}

std::size_t countWikiLinks(const std::vector<micronotes::doc::SourceSpan>& spans) {
  std::size_t count = 0;
  for(const auto& span : spans) {
    if(span.kind == micronotes::doc::SpanKind::WikiLink) ++count;
  }
  return count;
}

}

MICRONOTES_TEST(inline_scan_reads_a_plain_wikilink) {
  const std::string text = "see [[Another note]] here";
  const auto spans = micronotes::doc::scanInlines(text);
  const auto* link = firstWikiLink(spans);
  MICRONOTES_REQUIRE(link != nullptr);
  MICRONOTES_REQUIRE(link->target == "Another note");
  MICRONOTES_REQUIRE(link->start == text.find("[["));
  MICRONOTES_REQUIRE(link->end == text.find("]]") + 2);
  // The markers are the brackets, so hiding them leaves the title.
  MICRONOTES_REQUIRE(text.substr(link->openStart, link->openEnd - link->openStart) == "[[");
  MICRONOTES_REQUIRE(text.substr(link->contentStart, link->contentEnd - link->contentStart) == "Another note");
  MICRONOTES_REQUIRE(text.substr(link->closeStart, link->closeEnd - link->closeStart) == "]]");
}

// With an alias, the target and the bar are marker: hiding the markers has to
// leave exactly the words the writer chose to show.
MICRONOTES_TEST(inline_scan_reads_an_aliased_wikilink) {
  const std::string text = "[[Some Note|that one]]";
  const auto spans = micronotes::doc::scanInlines(text);
  const auto* link = firstWikiLink(spans);
  MICRONOTES_REQUIRE(link != nullptr);
  MICRONOTES_REQUIRE(link->target == "Some Note");
  MICRONOTES_REQUIRE(text.substr(link->openStart, link->openEnd - link->openStart) == "[[Some Note|");
  MICRONOTES_REQUIRE(text.substr(link->contentStart, link->contentEnd - link->contentStart) == "that one");
  MICRONOTES_REQUIRE(text.substr(link->closeStart, link->closeEnd - link->closeStart) == "]]");
}

MICRONOTES_TEST(inline_scan_reads_a_heading_target) {
  const auto spans = micronotes::doc::scanInlines("[[Note#Section]]");
  const auto* link = firstWikiLink(spans);
  MICRONOTES_REQUIRE(link != nullptr);
  // The fragment stays on the target; splitting it is the resolver's job.
  MICRONOTES_REQUIRE(link->target == "Note#Section");
}

MICRONOTES_TEST(inline_scan_reads_two_wikilinks_on_one_line) {
  const auto spans = micronotes::doc::scanInlines("[[one]] and [[two]]");
  MICRONOTES_REQUIRE(countWikiLinks(spans) == 2);
}

// A wikilink is claimed before an ordinary link, or `[[a]]` reads as the label
// `[a]` and `[[a](b)]` becomes a link with a bracket in its name.
MICRONOTES_TEST(inline_scan_prefers_a_wikilink_to_a_link) {
  const auto spans = micronotes::doc::scanInlines("[[a]](b)");
  MICRONOTES_REQUIRE(countWikiLinks(spans) == 1);
  for(const auto& span : spans) MICRONOTES_REQUIRE(span.kind != micronotes::doc::SpanKind::Link);
}

// Code spans win over everything, as they already did for links.
MICRONOTES_TEST(inline_scan_leaves_a_wikilink_inside_a_code_span_alone) {
  const auto spans = micronotes::doc::scanInlines("`[[not a link]]`");
  MICRONOTES_REQUIRE(countWikiLinks(spans) == 0);
}

// The shapes that are not links, and must stay literal text rather than
// swallowing the rest of the line.
MICRONOTES_TEST(inline_scan_ignores_wikilinks_that_are_not_one) {
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("[[unterminated")) == 0);
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("[[]]")) == 0);
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("[[|alias]]")) == 0);
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("[single]")) == 0);
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("[[")) == 0);
  MICRONOTES_REQUIRE(countWikiLinks(micronotes::doc::scanInlines("]]")) == 0);
}

// An ordinary link on the same line still works, and still has its own target.
MICRONOTES_TEST(inline_scan_reads_a_wikilink_beside_a_link) {
  const auto spans = micronotes::doc::scanInlines("[[wiki]] and [text](http://example.com)");
  MICRONOTES_REQUIRE(countWikiLinks(spans) == 1);
  bool sawLink = false;
  for(const auto& span : spans) {
    if(span.kind != micronotes::doc::SpanKind::Link) continue;
    sawLink = true;
    MICRONOTES_REQUIRE(span.target == "http://example.com");
  }
  MICRONOTES_REQUIRE(sawLink);
}

// A title is a name, so punctuation inside it is part of the name.
MICRONOTES_TEST(inline_scan_does_not_emphasise_inside_a_wikilink) {
  const auto spans = micronotes::doc::scanInlines("[[a *b* c]]");
  MICRONOTES_REQUIRE(countWikiLinks(spans) == 1);
  for(const auto& span : spans) {
    MICRONOTES_REQUIRE(span.kind != micronotes::doc::SpanKind::Emphasis);
  }
}

// Offsets are absolute, so a span found in a block addresses the note buffer.
MICRONOTES_TEST(inline_scan_wikilink_offsets_honour_the_base) {
  const auto spans = micronotes::doc::scanInlines("[[x]]", 100);
  const auto* link = firstWikiLink(spans);
  MICRONOTES_REQUIRE(link != nullptr);
  MICRONOTES_REQUIRE(link->start == 100 && link->end == 105);
  MICRONOTES_REQUIRE(link->contentStart == 102 && link->contentEnd == 103);
}

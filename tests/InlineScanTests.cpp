#include "TestSupport.h"

#include "core/markdown/MarkdownParser.h"
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

// The scan rejects a block before doing any work when it holds none of the
// bytes an inline construct can begin with. That is a *claim about the
// scanner*, not just about the reject: every byte the four passes below key on
// has to be in the table, or a real span goes missing and nothing else in the
// suite would notice, because the reject and the full scan agree on the empty
// answer everywhere except exactly there.
//
// So this asserts both directions. Each construct's own opening byte, alone,
// must survive the reject and still be found; and prose carrying every *other*
// ASCII punctuation mark must produce nothing, which is the case the fast path
// exists for.
MICRONOTES_TEST(inline_scan_rejects_only_text_that_can_hold_no_span) {
  const std::string plain =
    "A heading, a list item and a quoted line: no markup here! "
    "Prices are $5 + 10% = #6 @ 50/50; \"quoted\", 'single', (parens), [], {braces}, "
    "em-dash - and a trailing question mark?";
  MICRONOTES_REQUIRE(scanInlines(plain).empty());

  // One sample per construct, each reachable only through its own opening byte.
  MICRONOTES_REQUIRE(find(scanInlines("a \\* b"), SpanKind::Escape) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a `c` b"), SpanKind::Code) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a <https://x.test> b"), SpanKind::Autolink) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a [l](t) b"), SpanKind::Link) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a ![l](t) b"), SpanKind::Image) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a [[n]] b"), SpanKind::WikiLink) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a *e* b"), SpanKind::Emphasis) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a _e_ b"), SpanKind::Emphasis) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a **s** b"), SpanKind::Strong) != nullptr);
  MICRONOTES_REQUIRE(find(scanInlines("a ~~g~~ b"), SpanKind::Strike) != nullptr);
}

// A footnote reference is a link to the definition further down the note. The
// live scanner had no notion of one, so `[^first]` was drawn as the four
// literal characters it is spelled with -- while the reading pane, which parsed
// the whole note through md4c, drew it as `[1]` and made it clickable. One of
// the two renderers had to learn it, and it is this one.
MICRONOTES_TEST(inline_scan_reads_a_footnote_reference_as_a_link) {
  const auto spans = scanInlines("see the note[^first] here");
  int refs = 0;
  for(const auto& span : spans) {
    if(span.kind != SpanKind::FootnoteRef) continue;
    ++refs;
    MICRONOTES_REQUIRE(span.start == 12);
    MICRONOTES_REQUIRE(span.end == 20);
    MICRONOTES_REQUIRE(span.contentStart == 14);
    MICRONOTES_REQUIRE(span.contentEnd == 19);
    MICRONOTES_REQUIRE(span.target == "#fn-first");
  }
  MICRONOTES_REQUIRE(refs == 1);
}

// The three shapes that are not one. A definition never reaches this scan -- it
// is a block of its own -- but `[^a]:` is checked anyway, because the cost of
// being wrong is a link nobody can follow drawn over the author's own text.
MICRONOTES_TEST(inline_scan_leaves_a_bracket_that_is_not_a_footnote_alone) {
  for(const std::string_view text : {"a [^](b) c", "a [^first](target.md) c", "a [^] c",
                                     "a [^unterminated c", "a [^a]: definition"}) {
    for(const auto& span : scanInlines(text)) {
      micronotes::tests::require(span.kind != SpanKind::FootnoteRef,
                                 "read a footnote reference out of: " + std::string(text));
    }
  }
}

MICRONOTES_TEST(inline_scan_autolinks_a_bare_url) {
  const std::string text = "Model calibration https://ptvgroup-my.sharepoint.com/:w:/p/a_b/IQCq?e=4x&isSPOFile=1";
  const auto spans = scanInlines(text);
  const auto* url = find(spans, SpanKind::Autolink);
  MICRONOTES_REQUIRE(url != nullptr);
  MICRONOTES_REQUIRE(url->target == text.substr(18));
  // No brackets to hide, so the span is all content: a bare URL reads the same
  // laid out as it does with its markers revealed.
  MICRONOTES_REQUIRE(url->openStart == url->openEnd);
  MICRONOTES_REQUIRE(slice(text, url->contentStart, url->contentEnd) == url->target);
  // The underscore inside the URL is the scanner's, not emphasis's.
  MICRONOTES_REQUIRE(find(spans, SpanKind::Emphasis) == nullptr);
}

MICRONOTES_TEST(inline_scan_gives_back_the_sentence_punctuation) {
  const std::string text = "See https://example.com/report.pdf.";
  const auto spans = scanInlines(text);
  const auto* url = find(spans, SpanKind::Autolink);
  MICRONOTES_REQUIRE(url != nullptr);
  MICRONOTES_REQUIRE(url->target == "https://example.com/report.pdf");
  MICRONOTES_REQUIRE(url->end == text.size() - 1);
}

MICRONOTES_TEST(inline_scan_leaves_a_link_target_to_its_link) {
  const std::string text = "[6 pilars](https://example.com/deck) and nothing else";
  const auto spans = scanInlines(text);
  // One link, and it is the bracketed one: the URL inside `(...)` must not be
  // claimed a second time as a bare URL of its own.
  MICRONOTES_REQUIRE(find(spans, SpanKind::Autolink) == nullptr);
  const auto* link = find(spans, SpanKind::Link);
  MICRONOTES_REQUIRE(link != nullptr && link->target == "https://example.com/deck");
}

MICRONOTES_TEST(inline_scan_leaves_a_url_in_a_code_span_alone) {
  const auto spans = scanInlines("try `https://example.com` first");
  MICRONOTES_REQUIRE(find(spans, SpanKind::Autolink) == nullptr);
  MICRONOTES_REQUIRE(find(spans, SpanKind::Code) != nullptr);
}

MICRONOTES_TEST(inline_scan_agrees_with_the_reading_model_on_a_bare_url) {
  // The two pipelines lay out the same note -- md4c behind the reading model,
  // this scanner behind the page -- and a URL that links in one and not the
  // other is the app appearing to forget. They share `bareUrlAt`; this is the
  // check that they still share it.
  const std::string url =
    "https://ptvgroup-my.sharepoint.com/:w:/p/eduardo_a/IQCq?e=4x&ovuser=a%2Cb%40c.com&x=eyJBIjoiQiJ9%3D%3D";
  const std::string text = "Model calibration " + url;
  const auto spans = scanInlines(text);
  const auto* scanned = find(spans, SpanKind::Autolink);
  MICRONOTES_REQUIRE(scanned != nullptr && scanned->target == url);

  const auto doc = microcore::markdown::MarkdownParser().parse(text + "\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  bool parsed = false;
  for(const auto& item : doc.blocks[0].inlines) {
    parsed = parsed || (item.type == microcore::markdown::InlineType::Link && item.target == url);
  }
  MICRONOTES_REQUIRE(parsed);
}

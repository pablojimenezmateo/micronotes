#include "LayoutFixture.h"
#include "TestSupport.h"

#include "core/markdown/MarkdownParser.h"
#include "doc/RenderLayout.h"

#include <algorithm>
#include <string>
#include <vector>

// Laying out md4c's render model -- the blocks the live scanner does not model,
// and the inline constructs inside one.
//
// This is the layer both surfaces shape through. It exists because there were
// two renderers for that model: the screen's carried a line breaker of its own
// beside the tested one, and the exporter's could not reach it from a lower
// layer and set every table cell as plain text (TD-39). So what these check is
// that one shaping answers for both -- a cell keeps its runs, the offsets a
// wikilink is split at are the note page's own, and the slices a table breaks
// into tile the height the block reported.
namespace {

using micronotes::doc::RenderContext;
using micronotes::doc::RenderedItem;
using micronotes::doc::RenderedSlice;
using micronotes::doc::TextRole;

RenderContext stubContext() {
  RenderContext context;
  context.metrics = micronotes::tests::stubMetrics();
  return context;
}

std::vector<micronotes::markdown::Inline> textInlines(std::string value) {
  micronotes::markdown::Inline item;
  item.type = micronotes::markdown::InlineType::Text;
  item.text = std::move(value);
  return {item};
}

micronotes::doc::InlineLayout laidOut(const std::vector<micronotes::markdown::Inline>& inlines,
                                     float width = 1000.0f,
                                     const RenderContext& context = stubContext()) {
  micronotes::doc::RunStyle base;
  base.size = context.type.body;
  return micronotes::doc::layoutInlines(context, inlines, base, width);
}

// Every run's text, in order, which is the visible characters of the block.
std::string joined(const micronotes::doc::InlineLayout& content) {
  std::string out;
  for(const auto& run : content.layout.runs) out += run.text;
  return out;
}

const micronotes::doc::TextRun* runWithRole(const micronotes::doc::InlineLayout& content,
                                            TextRole role) {
  for(const auto& run : content.layout.runs) {
    if(run.role == role && !run.text.empty()) return &run;
  }
  return nullptr;
}

micronotes::markdown::Document parsed(std::string_view source) {
  micronotes::markdown::MarkdownParser parser;
  return micronotes::doc::parseRenderBlock(parser, source);
}

const micronotes::markdown::Block* firstTable(const micronotes::markdown::Document& document) {
  for(const auto& block : document.blocks) {
    if(block.type == micronotes::markdown::BlockType::Table) return &block;
  }
  return nullptr;
}

}

// md4c hands `[[Some Note]]` back as literal text, so the runs are split here
// by the same rule the note page applies. Without it the surfaces that render
// through md4c showed the raw brackets of every link between notes.
MICRONOTES_TEST(render_layout_splits_a_wikilink_out_of_plain_text) {
  const auto content = laidOut(textInlines("before [[Some Note]] after"));
  MICRONOTES_REQUIRE(joined(content) == "before Some Note after");
  const auto* link = runWithRole(content, TextRole::WikiLink);
  MICRONOTES_REQUIRE(link != nullptr);
  MICRONOTES_REQUIRE(link->text == "Some");
  MICRONOTES_REQUIRE(link->linkIndex >= 0);
  MICRONOTES_REQUIRE(content.layout.links[static_cast<std::size_t>(link->linkIndex)] ==
                     "Some Note");
}

// A link to a note that does not exist yet is an offer, not an error, and it
// has to look different from one that resolves -- otherwise the surface says
// every name in the note resolves.
MICRONOTES_TEST(render_layout_asks_whether_a_wikilink_resolves) {
  for(const bool exists : {true, false}) {
    RenderContext context = stubContext();
    context.wikiLinkResolves = [exists](std::string_view) { return exists; };
    const auto content = laidOut(textInlines("see [[Here]] now"), 1000.0f, context);
    const auto role = exists ? TextRole::WikiLink : TextRole::WikiLinkUnresolved;
    MICRONOTES_REQUIRE(runWithRole(content, role) != nullptr);
    const auto other = exists ? TextRole::WikiLinkUnresolved : TextRole::WikiLink;
    MICRONOTES_REQUIRE(runWithRole(content, other) == nullptr);
  }
}

// md4c breaks its text at every `[`: it tries to read `[[Deep Note]]` as a
// link, finds no `(`, and hands back `"A ["`, `"["`, `"Deep Note]] ..."`. No
// one of those contains a `[[`, so the pieces are rejoined before the scan.
MICRONOTES_TEST(render_layout_rejoins_the_pieces_md4c_split_a_wikilink_into) {
  std::vector<micronotes::markdown::Inline> inlines;
  for(const char* piece : {"A ", "[", "[", "Deep Note]] tail"}) {
    micronotes::markdown::Inline item;
    item.type = micronotes::markdown::InlineType::Text;
    item.text = piece;
    inlines.push_back(item);
  }
  const auto content = laidOut(inlines);
  MICRONOTES_REQUIRE(joined(content) == "A Deep Note tail");
  MICRONOTES_REQUIRE(runWithRole(content, TextRole::WikiLink) != nullptr);
}

// The regression guard for TD-39. A cell used to be set with
// `markdown::plainText` on the way to a PDF, which kept every word and lost
// every one of these.
MICRONOTES_TEST(render_layout_keeps_a_table_cells_own_runs) {
  const auto document = parsed("| Plain | Marked |\n| --- | --- |\n"
                               "| ordinary | **bold**, *italic*, `code`, [a](b) |\n");
  const auto* table = firstTable(document);
  MICRONOTES_REQUIRE(table != nullptr);
  const auto laid = micronotes::doc::layoutTable(stubContext(), *table, 600.0f);
  MICRONOTES_REQUIRE(laid.rows.size() == 2);
  MICRONOTES_REQUIRE(laid.headerRow == 0);

  const auto& marked = laid.rows[1].cells[1].content;
  bool strong = false;
  bool italic = false;
  bool mono = false;
  bool link = false;
  for(const auto& run : marked.layout.runs) {
    if(run.text.empty()) continue;
    strong = strong || run.style.strong;
    italic = italic || run.style.italic;
    mono = mono || run.style.mono;
    link = link || run.linkIndex >= 0;
  }
  MICRONOTES_REQUIRE(strong);
  MICRONOTES_REQUIRE(italic);
  MICRONOTES_REQUIRE(mono);
  MICRONOTES_REQUIRE(link);
  // The header row reads as the surface's own text and the body a step back
  // from it, which is a role rather than a colour so both surfaces ink it.
  MICRONOTES_REQUIRE(runWithRole(laid.rows[0].cells[0].content, TextRole::Body) != nullptr);
  MICRONOTES_REQUIRE(runWithRole(laid.rows[1].cells[0].content, TextRole::Muted) != nullptr);
}

// The pagination is built on the slices tiling the block, so a slice list that
// does not add up to the height the block reported puts every block after it
// on the page at the wrong y.
MICRONOTES_TEST(render_layout_slices_tile_the_block_they_came_from) {
  const RenderContext context = stubContext();
  const std::string source = "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n";
  micronotes::doc::RenderedBlock block;
  block.parsed = parsed(source);
  block.haveParse = true;
  micronotes::doc::layoutRenderedBlock(context, source, 400.0f, block);

  MICRONOTES_REQUIRE(!block.slices.empty());
  MICRONOTES_REQUIRE(block.slices.front().top == context.spacing.padTop);
  for(std::size_t i = 1; i < block.slices.size(); ++i) {
    MICRONOTES_REQUIRE(block.slices[i].top == block.slices[i - 1].bottom);
  }
  const float tiled = block.slices.back().bottom + context.spacing.padBottom;
  MICRONOTES_REQUIRE(std::abs(tiled - block.height) < 0.01f);
}

// TD-40's fix. A table continued onto a second page repeats the row its
// columns are named in, and that row is drawn twice -- so the cost is a
// question about where the page starts rather than a number on the block.
MICRONOTES_TEST(render_layout_charges_a_repeated_header_only_past_the_header) {
  const std::string source = "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n| 5 | 6 |\n";
  micronotes::doc::RenderedBlock block;
  block.parsed = parsed(source);
  block.haveParse = true;
  micronotes::doc::layoutRenderedBlock(stubContext(), source, 400.0f, block);

  // The header itself, and the first body row that would follow it anyway, are
  // drawn where they are.
  MICRONOTES_REQUIRE(micronotes::doc::repeatedHeaderHeight(block, 0) == 0.0f);
  const float header = block.items[0].table.rows[0].height;
  for(std::size_t slice = 1; slice < block.slices.size(); ++slice) {
    MICRONOTES_REQUIRE(micronotes::doc::repeatedHeaderHeight(block, slice) == header);
  }
}

// md4c drops a footnote definition that nothing refers to, and a complex block
// is parsed on its own -- so one parsed by itself came back empty and fell
// through to the raw-source fallback. The exporter had no such trick at all,
// which is why the same note showed a footnote on screen and grey monospace
// source on the page.
MICRONOTES_TEST(render_layout_keeps_a_lone_footnote_definition) {
  const auto document = parsed("[^label]: the body of the note\n");
  bool footnote = false;
  for(const auto& block : document.blocks) {
    if(block.type == micronotes::markdown::BlockType::Footnote) footnote = true;
  }
  MICRONOTES_REQUIRE(footnote);
}

// Every block of a complex parse is laid out, not only the first table. The
// exporter used to find the first table and drop everything else in the block,
// so a paragraph above a table simply was not in the PDF.
MICRONOTES_TEST(render_layout_lays_out_every_block_of_a_complex_parse) {
  const std::string source = "Text above.\n\n| A |\n| --- |\n| 1 |\n\nText below.\n";
  micronotes::doc::RenderedBlock block;
  block.parsed = parsed(source);
  block.haveParse = true;
  micronotes::doc::layoutRenderedBlock(stubContext(), source, 400.0f, block);

  int tables = 0;
  int inlines = 0;
  for(const auto& item : block.items) {
    if(item.kind == RenderedItem::Kind::Table) ++tables;
    if(item.kind == RenderedItem::Kind::Inlines) ++inlines;
  }
  MICRONOTES_REQUIRE(tables == 1);
  MICRONOTES_REQUIRE(inlines == 2);
}

// Laying the same block out again at the width it is already laid out at is a
// comparison. It is what lets the note page measure a table and then draw it
// without shaping it twice per frame.
MICRONOTES_TEST(render_layout_reuses_a_layout_at_the_same_width) {
  const std::string source = "| A | B |\n| --- | --- |\n| 1 | 2 |\n";
  micronotes::doc::RenderedBlock block;
  block.parsed = parsed(source);
  block.haveParse = true;
  micronotes::doc::layoutRenderedBlock(stubContext(), source, 400.0f, block);
  const void* runs = block.items[0].table.rows[0].cells[0].content.layout.runs.data();

  micronotes::doc::layoutRenderedBlock(stubContext(), source, 400.0f, block);
  MICRONOTES_REQUIRE(block.items[0].table.rows[0].cells[0].content.layout.runs.data() == runs);

  micronotes::doc::layoutRenderedBlock(stubContext(), source, 380.0f, block);
  MICRONOTES_REQUIRE(block.width == 380.0f);
}

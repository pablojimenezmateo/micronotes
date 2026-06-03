#include "TestSupport.h"

#include "markdown/MarkdownParser.h"
#include "markdown/RenderModel.h"

#include <md4c-html.h>

#include <string>

using micronotes::markdown::BlockType;
using micronotes::markdown::InlineType;
using micronotes::markdown::MarkdownParser;

namespace {

static void appendHtml(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  auto& out = *static_cast<std::string*>(userdata);
  out.append(text, size);
}

static std::string md4cHtml(const std::string& markdown) {
  std::string out;
  md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), appendHtml, &out, MD_DIALECT_GITHUB, 0);
  return out;
}

}

MICRONOTES_TEST(markdown_parser_parses_basic_blocks) {
  const auto doc = MarkdownParser().parse("# Title\n\n- item\n\n> quote\n\n```mermaid\ngraph TD\n```\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 4);
  MICRONOTES_REQUIRE(doc.blocks[0].type == BlockType::Heading);
  MICRONOTES_REQUIRE(doc.blocks[0].level == 1);
  MICRONOTES_REQUIRE(doc.blocks[1].type == BlockType::UnorderedItem);
  MICRONOTES_REQUIRE(doc.blocks[2].type == BlockType::Quote);
  MICRONOTES_REQUIRE(doc.blocks[3].type == BlockType::Code);
}

MICRONOTES_TEST(markdown_parser_keeps_links_and_images) {
  const auto doc = MarkdownParser().parse("Read [doc](file.pdf) and see ![alt](image.png)\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  bool sawImage = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.target == "file.pdf");
    sawImage = sawImage || (item.type == InlineType::Image && item.target == "image.png");
  }
  MICRONOTES_REQUIRE(sawLink);
  MICRONOTES_REQUIRE(sawImage);
}

MICRONOTES_TEST(markdown_parser_keeps_empty_links_and_images) {
  const auto doc = MarkdownParser().parse("![](.micronotes/attachments/note/clipboard.png)\n[](.micronotes/attachments/note/doc.pdf)\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  bool sawImage = false;
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawImage = sawImage || (item.type == InlineType::Image && item.target == ".micronotes/attachments/note/clipboard.png");
    sawLink = sawLink || (item.type == InlineType::Link && item.text == "doc.pdf" && item.target == ".micronotes/attachments/note/doc.pdf");
  }
  MICRONOTES_REQUIRE(sawImage);
  MICRONOTES_REQUIRE(sawLink);
}

MICRONOTES_TEST(markdown_parser_tolerates_attachment_links_with_spaces) {
  const auto target = std::string(".micronotes/attachments/n25083524798053-0/From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf");
  const auto label = std::string("From-Modelling-and-Analysis-Tools-to-Enabling-Decision-Workflows .pdf");
  const auto doc = MarkdownParser().parse("[" + label + "](" + target + ")\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.text == label && item.target == target);
  }
  MICRONOTES_REQUIRE(sawLink);
}

MICRONOTES_TEST(markdown_parser_autolinks_raw_urls) {
  const auto url = std::string("https://example.com/files/report.pdf");
  const auto doc = MarkdownParser().parse("Open " + url + ".\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  bool sawLink = false;
  bool sawTrailingPeriod = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawLink = sawLink || (item.type == InlineType::Link && item.text == url && item.target == url);
    sawTrailingPeriod = sawTrailingPeriod || (item.type == InlineType::Text && item.text == ".");
  }
  MICRONOTES_REQUIRE(sawLink);
  MICRONOTES_REQUIRE(sawTrailingPeriod);
}

MICRONOTES_TEST(markdown_parser_keeps_ordered_numbers_and_rules) {
  const auto doc = MarkdownParser().parse("3. Bird\n1. McHale\n8. Parish\n\n---\n\nUse the `printf()` function and *emphasis*.\n");
  MICRONOTES_REQUIRE(doc.blocks.size() == 5);
  MICRONOTES_REQUIRE(doc.blocks[0].type == BlockType::OrderedItem);
  MICRONOTES_REQUIRE(doc.blocks[0].orderedNumber == 3);
  MICRONOTES_REQUIRE(doc.blocks[1].orderedNumber == 4);
  MICRONOTES_REQUIRE(doc.blocks[2].orderedNumber == 5);
  MICRONOTES_REQUIRE(doc.blocks[3].type == BlockType::HorizontalRule);
  bool sawCode = false;
  bool sawEmphasis = false;
  for(const auto& item : doc.blocks[4].inlines) {
    sawCode = sawCode || item.type == InlineType::Code;
    sawEmphasis = sawEmphasis || item.type == InlineType::Emphasis;
  }
  MICRONOTES_REQUIRE(sawCode);
  MICRONOTES_REQUIRE(sawEmphasis);
}

MICRONOTES_TEST(markdown_parser_keeps_gfm_tables) {
  const std::string source = "| Left | Center | Right |\n|:-----|:------:|------:|\n| a | b | c |\n";
  const auto doc = MarkdownParser().parse(source);
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  MICRONOTES_REQUIRE(doc.blocks[0].type == BlockType::Table);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows.size() == 2);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows[0].header);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows[0].cells.size() == 3);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows[0].cells[0].align == micronotes::markdown::Align::Left);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows[0].cells[1].align == micronotes::markdown::Align::Center);
  MICRONOTES_REQUIRE(doc.blocks[0].tableRows[0].cells[2].align == micronotes::markdown::Align::Right);
  const auto html = md4cHtml(source);
  MICRONOTES_REQUIRE(html.find("<table>") != std::string::npos);
  MICRONOTES_REQUIRE(html.find("<th align=\"center\">") != std::string::npos);
  MICRONOTES_REQUIRE(micronotes::markdown::plainText(doc).find("Center") != std::string::npos);
}

MICRONOTES_TEST(markdown_parser_keeps_gfm_task_lists_and_strike_links) {
  const std::string source = "- [x] done with ~~strike~~ and [link](https://example.com)\n- [ ] todo\n";
  const auto doc = MarkdownParser().parse(source);
  MICRONOTES_REQUIRE(doc.blocks.size() == 2);
  MICRONOTES_REQUIRE(doc.blocks[0].type == BlockType::UnorderedItem);
  MICRONOTES_REQUIRE(doc.blocks[0].task);
  MICRONOTES_REQUIRE(doc.blocks[0].taskChecked);
  MICRONOTES_REQUIRE(doc.blocks[1].task);
  MICRONOTES_REQUIRE(!doc.blocks[1].taskChecked);
  bool sawStrike = false;
  bool sawLink = false;
  for(const auto& item : doc.blocks[0].inlines) {
    sawStrike = sawStrike || item.strikethrough || item.type == InlineType::Strikethrough;
    sawLink = sawLink || (item.type == InlineType::Link && item.target == "https://example.com");
  }
  MICRONOTES_REQUIRE(sawStrike);
  MICRONOTES_REQUIRE(sawLink);
  const auto html = md4cHtml(source);
  MICRONOTES_REQUIRE(html.find("type=\"checkbox\"") != std::string::npos);
  MICRONOTES_REQUIRE(html.find("<del>strike</del>") != std::string::npos);
}

MICRONOTES_TEST(markdown_parser_keeps_nested_list_parent_items) {
  const std::string source = "- Parent item\n  - Child item\n    - Grandchild item\n- Second parent item\n\n1. Plan\n   - Research\n   - Implement\n2. Verify\n   1. Build\n   2. Test\n";
  const auto doc = MarkdownParser().parse(source);
  const auto plain = micronotes::markdown::plainText(doc);
  MICRONOTES_REQUIRE(plain.find("Parent item") != std::string::npos);
  MICRONOTES_REQUIRE(plain.find("Child item") != std::string::npos);
  MICRONOTES_REQUIRE(plain.find("Grandchild item") != std::string::npos);
  MICRONOTES_REQUIRE(plain.find("Plan") != std::string::npos);
  MICRONOTES_REQUIRE(plain.find("Research") != std::string::npos);
  MICRONOTES_REQUIRE(plain.find("Build") != std::string::npos);
}

MICRONOTES_TEST(markdown_parser_keeps_footnotes_and_admonitions) {
  const std::string source = "Read this[^a].\n\n[^a]: Footnote body.\n\n> [!NOTE]\n> Pay attention.\n";
  const auto doc = MarkdownParser().parse(source);
  bool sawRef = false;
  bool sawFootnote = false;
  bool sawAdmonition = false;
  for(const auto& block : doc.blocks) {
    sawFootnote = sawFootnote || (block.type == BlockType::Footnote && block.footnoteLabel == "a");
    sawAdmonition = sawAdmonition || (block.type == BlockType::Admonition && block.admonitionType == "note");
    for(const auto& item : block.inlines) sawRef = sawRef || item.type == InlineType::FootnoteRef;
  }
  MICRONOTES_REQUIRE(sawRef);
  MICRONOTES_REQUIRE(sawFootnote);
  MICRONOTES_REQUIRE(sawAdmonition);
  const auto html = md4cHtml(source);
  MICRONOTES_REQUIRE(html.find("footnote") != std::string::npos);
  MICRONOTES_REQUIRE(html.find("admonition") != std::string::npos);
}

MICRONOTES_TEST(markdown_parser_decodes_entities_like_md4c_html) {
  const std::string source = "Fish &amp; chips &#169;\n";
  const auto doc = MarkdownParser().parse(source);
  MICRONOTES_REQUIRE(doc.blocks.size() == 1);
  MICRONOTES_REQUIRE(micronotes::markdown::plainText(doc).find("Fish & chips") != std::string::npos);
  MICRONOTES_REQUIRE(micronotes::markdown::plainText(doc).find("©") != std::string::npos);
  const auto html = md4cHtml(source);
  MICRONOTES_REQUIRE(html.find("Fish &amp; chips ©") != std::string::npos);
}

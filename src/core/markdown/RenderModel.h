#pragma once

#include <string>
#include <vector>

namespace microcore::markdown {

enum class BlockType {
  Paragraph,
  Heading,
  UnorderedItem,
  OrderedItem,
  Quote,
  Code,
  Table,
  HorizontalRule,
  Html,
  Footnote,
  Admonition,
  BlankLine
};

enum class InlineType {
  Text,
  Link,
  Image,
  Code,
  Emphasis,
  Strong,
  Strikethrough,
  Html,
  FootnoteRef
};

enum class Align {
  Default,
  Left,
  Center,
  Right
};

struct Inline {
  InlineType type = InlineType::Text;
  std::string text;
  std::string target;
  std::string title;
  bool emphasis = false;
  bool strong = false;
  bool strikethrough = false;
  bool hardBreak = false;
};

struct TableCell {
  std::vector<Inline> inlines;
  Align align = Align::Default;
  bool header = false;
};

struct TableRow {
  std::vector<TableCell> cells;
  bool header = false;
};

struct Block {
  BlockType type = BlockType::Paragraph;
  int level = 0;
  int orderedNumber = 0;
  int depth = 0;
  bool task = false;
  bool taskChecked = false;
  bool tight = false;
  std::string info;
  std::string language;
  std::string footnoteLabel;
  std::string admonitionType;
  std::vector<Inline> inlines;
  std::vector<TableRow> tableRows;
};

struct Document {
  std::vector<Block> blocks;
};

std::string plainText(const Document& document);

// One run of inlines flattened to the words a reader sees: a link becomes its
// label (or its target, when it has no label), a footnote reference becomes
// `[label]`, and an image contributes nothing -- it is a picture, and its alt
// text is not part of the sentence around it.
//
// Down here rather than beside either caller because both the screen's table
// and the exported one measure a cell by it, and a cell measured one way and
// drawn the other is a column whose rules miss its own text.
std::string plainText(const std::vector<Inline>& inlines);

}

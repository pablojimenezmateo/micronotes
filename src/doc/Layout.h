#pragma once

#include "doc/BlockScan.h"
#include "doc/InlineScan.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace micronotes::doc {

// Document space: x grows from the content column's left edge, y from the top
// of the first block. The caller translates into window coordinates.
struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
};

struct RunStyle {
  bool mono = false;
  bool strong = false;
  bool italic = false;
  bool strike = false;
  float size = 0.0f;  // logical pixels

  bool operator==(const RunStyle& other) const {
    return mono == other.mono && strong == other.strong && italic == other.italic &&
           strike == other.strike && size == other.size;
  }
};

// Semantic color role. The layout never names a color; the view maps these onto
// theme tokens.
enum class TextRole {
  Body,
  Marker,
  Link,
  // A link to another note. Separate from Link because the interesting state is
  // whether it goes anywhere: a link to a note that does not exist yet is not a
  // mistake, it is the most common way to write one, and it has to look
  // different from one that resolves so the difference is visible at a glance.
  WikiLink,
  WikiLinkUnresolved,
  Code,
  Muted
};

struct TextRun {
  // Relative to the owning block's `start`, so one cached layout can serve
  // every position an identical block appears at. Add `blocks()[i].start`.
  std::size_t srcStart = 0;
  std::size_t srcEnd = 0;
  Rect rect;
  RunStyle style;
  TextRole role = TextRole::Body;
  bool isMarker = false;
  int linkIndex = -1;
  // Byte-for-byte the source it displays, except that `\n` and `\t` become a
  // single space each, so a prefix measurement still maps offsets to pixels.
  std::string text;
};

struct VisualLine {
  float y = 0.0f;
  float height = 0.0f;
  std::vector<TextRun> runs;
};

// Cached by content and geometry, so two identical blocks share one layout.
// Its position in the document lives alongside it, not inside it.
struct BlockLayout {
  BlockKind kind = BlockKind::Paragraph;
  float height = 0.0f;
  float indent = 0.0f;
  float textLeft = 0.0f;
  bool revealed = false;   // this block's markers are shown as text
  bool raw = false;        // laid out as plain source lines
  bool complex = false;    // drawn by the md4c render model
  bool hidden = false;     // inside a collapsed fold: no lines, no height
  std::vector<VisualLine> lines;
  std::vector<std::string> links;
};

struct TypeMetrics {
  float body = 16.0f;
  float mono = 14.0f;
  float heading[6] = {30.0f, 24.0f, 20.0f, 17.0f, 16.0f, 16.0f};
  float lineHeightRatio = 1.5f;
};

struct Metrics {
  std::function<float(std::string_view, const RunStyle&)> measure;
  std::function<float(const RunStyle&)> lineHeight;
  // Height of a block the scanner does not model, rendered through md4c.
  std::function<float(const SourceBlock&, float width)> measureComplex;
};

struct LayoutOptions {
  float width = 700.0f;
  float fontScale = 1.0f;
  TypeMetrics type;
  float indentStep = 24.0f;
  float listGutter = 24.0f;
  float quoteGutter = 18.0f;
  float blockSpacing = 4.0f;
  float headingSpaceAbove = 14.0f;
  // Markers are revealed only in the block holding the caret. Given as a source
  // offset so the caller never has to know the block numbering.
  std::size_t caretOffset = static_cast<std::size_t>(-1);
  bool revealAll = false;
  // An offset inside a `Complex` block the user clicked into, edited as raw
  // source until they leave it.
  std::size_t rawOffset = static_cast<std::size_t>(-1);
  // Asked of each block that could head a toggle; the layout resolves a "yes"
  // to the blocks that head hides. Answering per block rather than per offset
  // is what keeps a fold attached to its heading when an edit above moves it.
  // A hidden block keeps its offsets and its place in the partition and gives
  // up only its height, so nothing below it shifts and the caret has no line
  // there to land on.
  std::function<bool(const SourceBlock&)> folded;
  // Whether a `[[target]]` names a note that exists. The layout cannot know --
  // it has a buffer, not a library -- so it asks, exactly as it asks about
  // folds. Unset means "assume it does", which is what a layout with no library
  // behind it should draw.
  std::function<bool(std::string_view)> wikiLinkResolves;
};

class DocumentLayout {
public:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  void setMetrics(Metrics metrics);
  // Re-lays out only the blocks whose content or geometry actually changed.
  void update(std::string_view source, const LayoutOptions& options);

  const std::vector<SourceBlock>& blocks() const;
  std::size_t blockCount() const;
  const BlockLayout& layout(std::size_t index) const;
  // True when the block sits inside a collapsed toggle.
  bool blockHidden(std::size_t index) const;
  float blockTop(std::size_t index) const;
  float totalHeight() const;
  const LayoutOptions& options() const;
  const std::string& source() const;

  Rect caretRect(std::size_t offset) const;
  std::size_t offsetAt(float x, float y) const;
  std::vector<Rect> selectionRects(std::size_t from, std::size_t to) const;
  std::optional<std::size_t> blockAt(float y) const;
  // Moves `offset` by whole visual rows, keeping the horizontal position.
  std::size_t rowRelative(std::size_t offset, int deltaRows) const;
  std::size_t rowsPerHeight(float height) const;

  // Diagnostics for the perf harness.
  std::size_t cachedBlockCount() const;
  std::size_t lastRelaidBlocks() const;

private:
  struct Placed {
    std::size_t blockIndex = 0;
    float top = 0.0f;
    const BlockLayout* layout = nullptr;
  };

  struct FlatLine {
    std::size_t block = 0;
    std::size_t line = 0;
    float top = 0.0f;
  };

  // What a block's layout depends on beyond its own bytes and the geometry.
  struct Flags {
    bool revealed = false;      // markers shown as text
    bool raw = false;           // laid out as plain source lines
    bool trailingLine = false;  // owns the empty last line of the buffer
    bool hidden = false;        // inside a collapsed toggle
    bool groupFirst = true;     // first line of a quote or callout run
    bool groupLast = true;      // last line of one
  };

  BlockLayout layoutBlock(std::size_t index, const Flags& flags) const;
  const BlockLayout* layoutForOffset(std::size_t offset, std::size_t* blockIndex) const;
  std::size_t flatLineForOffset(std::size_t offset, float* caretX) const;

  Metrics metrics_;
  LayoutOptions options_;
  std::string source_;
  std::vector<SourceBlock> blocks_;
  std::vector<Placed> placed_;
  std::vector<bool> hidden_;
  std::vector<FlatLine> flatLines_;
  std::unordered_map<std::uint64_t, BlockLayout> cache_;
  std::vector<std::uint64_t> liveKeys_;
  float totalHeight_ = 0.0f;
  std::size_t lastRelaid_ = 0;
};

}

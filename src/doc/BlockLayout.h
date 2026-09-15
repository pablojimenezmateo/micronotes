#pragma once

#include "CoreAliases.h"

#include "doc/BlockScan.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What a laid-out block *is*: the rectangle, the styling, the tokens it is
// staged into, the runs and lines it becomes, and the buffer those address.
//
// Split out of `doc/Layout.h`, which had grown to hold both these value types
// and the whole incremental machinery of `DocumentLayout` over them. They are
// two subjects and only one of them is what `doc/Flow.h` and `doc/Tokenize.h`
// consume -- those two are the innermost loops in the app and were including a
// class with an `unordered_map` and three `std::function`s in it to see a
// `Token`.
//
// Nothing in here knows there is a *document*. That is the point: a block's
// shape is measurable, testable and paintable without one, which is what lets
// `doc/RenderLayout.h` produce the same `BlockLayout` from md4c's model with no
// note behind it at all.
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
enum class TextRole : std::uint8_t {
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
  // The alt text of `![alt](target)`. A role of its own because the picture is
  // drawn under the block now: an underlined accent-coloured line above every
  // image reads as a stray link, where the same words muted read as the caption
  // they are. Still a link -- a remote image has nothing but its alt text to be
  // reached by -- just not one shouting about it.
  ImageAlt,
  // Text a step back from the surface's own: the body rows of a table, whose
  // header is what reads as the page's text. `doc/RenderLayout.h` sets a
  // table's non-header cells in it.
  Muted
};

// One run of source that shares a style: a word, a run of spaces, or a marker.
// The staging form of a `TextRun`, before the flow has decided which line it
// lands on or where along it. Declared here only so the layout can own the
// buffer these are staged into; the tokenizer that fills them is in Layout.cpp.
struct Token {
  std::size_t start = 0;
  std::size_t end = 0;
  // Into the block's `display` buffer, which outlives every token staged from
  // it. A `std::string` here was a copy of those same bytes per token --
  // 391,218 of them for a 1.1 MB note -- made so that `Flow` could move it into
  // the run, back when the run held one.
  std::string_view text;
  RunStyle style;
  TextRole role = TextRole::Body;
  bool isMarker = false;
  bool hidden = false;
  bool space = false;
  // This token is where a line of the file ended, and the line on screen ends
  // with it. A single newline inside a paragraph is a *break* here, not the
  // space CommonMark folds it into: a note is written in lines, and a reader
  // who ends a line expects the next word to start below it rather than beside
  // it. See `docs/markdown-elements.md`, "Paragraphs And Line Breaks".
  bool lineBreak = false;
  int link = -1;
};

// What one byte of a block's content is: which inline spans cover it, and what
// they make it look like. The layout builds one of these per byte of a
// marked-up block and then walks the table to cut runs where it changes.
// Declared here, like `Token`, only so the layout can own the buffer it is
// staged into -- the code that fills it is in Layout.cpp.
struct Attr {
  bool strong = false;
  bool italic = false;
  bool mono = false;
  bool strike = false;
  bool marker = false;
  int link = -1;
  TextRole role = TextRole::Body;

  bool operator==(const Attr& other) const {
    return strong == other.strong && italic == other.italic && mono == other.mono &&
           strike == other.strike && marker == other.marker && link == other.link &&
           role == other.role;
  }
};

// One measured, placed stretch of a block's text.
//
// **The field order is the struct's size**, and the size is the layout's memory.
// There is one of these per *token* -- every word and every run of spaces -- for
// every block of the note, laid out whole rather than to the viewport. Measured
// on a real session, a note costs about 35 times its own bytes in resident
// memory, and this struct is nearly all of it: an English word plus its space
// is six source bytes and two runs.
//
// So the members are ordered wide to narrow, the two offsets are 32-bit, and
// **there is no text in here at all**. It went 88 bytes -> 72 (narrowing the
// offsets, which are block-relative, and ordering the fields) -> 40 (the
// `std::string` moving out to `BlockLayout::display`, which the offsets already
// addressed). On a 1.1 MB note that is 391,218 runs and 12.5 MB.
//
// Adding a member, or widening one, puts it back --
// `layout_text_run_stays_small` in `LayoutBlockTests` is what says so out loud.
struct TextRun {
  Rect rect;
  // Where this run's text is, in the owning `BlockLayout::display`. Also where
  // its *source* is, relative to the owning block's `start` -- the two are the
  // same number, which is the point of the substitution being length
  // preserving. Add `blocks()[i].start` for a buffer offset.
  std::uint32_t srcStart = 0;
  std::uint32_t srcEnd = 0;
  RunStyle style;
  int linkIndex = -1;
  TextRole role = TextRole::Body;
  bool isMarker = false;
  // Claims its offsets and shows nothing: a marker's `**`, the bytes of a line
  // continuation, the one placeholder run a `Complex` block gets. This used to
  // be `text.empty()` -- inferred from the absence of a string rather than
  // stated -- and with the string gone it has to be said. It costs nothing:
  // the struct had three spare bytes of tail padding.
  bool hidden = false;

  // Whether this run puts anything on screen. The paint and the query paths
  // both ask, and both used to ask it as `text.empty()` -- which worked, and
  // said "there is no string here" when it meant "there is nothing to draw".
  bool shows() const {
    return !hidden && srcEnd > srcStart;
  }
};

// One wrapped row of a block. Its runs live in the block's single run array
// rather than in a vector of their own: a vector per line was an allocation per
// visual row of the document -- around 13,000 of the 30,000 a cold open of a
// 200 KB note made, and the same 13,000 `free` calls again when the cache swept
// -- to hold on average four runs. Half-open, and always within the owning
// block's `runs`.
// Room kept clear at the trailing edge of a line that is the *file's* own -- a
// fenced code block, or a block dropped to raw -- for the mark that says the
// column broke it.
//
// Only there. Such a line breaks mid-token and so fills the column exactly,
// which leaves a mark drawn at the edge sitting on top of the code. A paragraph
// breaks at a space and leaves the room itself, so it keeps its full column and
// wears the mark only when the last word on the line ended short of it.
inline constexpr float kWrapMarkReserve = 12.0f;

struct VisualLine {
  float y = 0.0f;
  float height = 0.0f;
  std::uint32_t runBegin = 0;
  std::uint32_t runEnd = 0;
  // This line begins in the middle of a line of the *file*: the one above it
  // was too long for the column and broke. A line that begins because the file
  // ended one -- a paragraph's own newline, a fresh line of code -- is not a
  // continuation, which is the distinction the wrap mark is about.
  bool continuation = false;

  std::uint32_t runCount() const {
    return runEnd - runBegin;
  }
};

// The box an image takes once it has been fitted to the column and to the page.
// A zero one means the target names nothing drawable, which is a placeholder
// line rather than a picture.
struct ImageBox {
  float width = 0.0f;
  float height = 0.0f;
};

// One picture under a block, in the block's own space. `![alt](target)` is an
// inline as far as the scanner is concerned -- the alt text is a link-styled run
// like any other -- and the picture goes below the paragraph that named it,
// which is where every Markdown reader puts it.
struct BlockImage {
  std::string target;
  Rect rect;
};

// Cached by content and geometry, so two identical blocks share one layout.
// Its position in the document lives alongside it, not inside it.
struct BlockLayout {
  BlockKind kind = BlockKind::Paragraph;
  float height = 0.0f;
  float indent = 0.0f;
  float textLeft = 0.0f;
  bool complex = false;    // drawn by the md4c render model
  // The `> [!KIND]` line at the head of a callout, with its marker hidden. Its
  // text is the callout's title rather than the first sentence of its body,
  // which is what `[!KIND] Some title` means everywhere these files are read.
  // The view draws it in the kind's colour and puts the kind's mark beside it;
  // an empty one is where the view falls back to naming the kind itself.
  bool calloutTitle = false;
  std::vector<VisualLine> lines;
  // Every line's runs, in line order, so `lines[i]` owns `[runBegin, runEnd)`.
  std::vector<TextRun> runs;
  // The bytes every run's `srcStart`/`srcEnd` index: the block's own source
  // with `\n`, `\t` and `\r` each replaced by one space, so that a run's text
  // stays byte-aligned with the source it came from and a prefix measurement
  // maps offsets to pixels.
  //
  // One buffer per block rather than a `std::string` per run. The substitution
  // is length preserving and byte aligned, so a run's text is at exactly the
  // offset that already addressed its source -- which is what makes the run's
  // copy redundant rather than merely large. For a 1.1 MB note this buffer is
  // 1.1 MB in total and the copies it replaces were 12.5 MB.
  //
  // `doc/RenderLayout.h` fills this with md4c's flattened text instead, which
  // is the same contract said about a different buffer: it is what the offsets
  // index. That is why `InlineLayout` no longer carries a `text` of its own.
  std::string display;
  std::vector<std::string> links;
  // Empty for almost every block, so it costs a pointer triple and no
  // allocation for the ones with no picture in them.
  std::vector<BlockImage> images;

  // The runs of one of this block's lines. The line must be one of `lines`;
  // nothing else can address this array.
  std::span<const TextRun> runsOf(const VisualLine& line) const {
    return std::span<const TextRun>(runs).subspan(line.runBegin, line.runCount());
  }

  // What a run displays. A view into `display`, valid as long as this block is
  // -- which is what every reader already held, because a run is only reachable
  // through the block that owns it.
  //
  // Clamped rather than trusted: a hidden run claims offsets and no text, and a
  // `Complex` block's single placeholder run spans a range `display` is not
  // obliged to cover.
  std::string_view textOf(const TextRun& run) const {
    if(run.hidden || run.srcEnd <= run.srcStart || run.srcStart >= display.size()) return {};
    const std::size_t to = std::min<std::size_t>(run.srcEnd, display.size());
    return std::string_view(display).substr(run.srcStart, to - run.srcStart);
  }

  // Empties this for another block to be laid out into, keeping the four
  // vectors' capacity. See `DocumentLayout::recycleCache` for why that is
  // worth anything.
  //
  // Written as "default-construct, then put the buffers back" rather than as a
  // list of fields to clear, and that is the whole reason it is a method. A
  // list of fields to clear is one that a field added to this struct does not
  // get added to, and the failure then is a recycled layout carrying the
  // previous block's `complex` or `calloutTitle` -- a wrong block drawn, with
  // nothing to point at. Assigning a fresh value resets every scalar there is,
  // including the ones added after this was written.
  void reuse() {
    std::vector<VisualLine> keptLines = std::move(lines);
    std::vector<TextRun> keptRuns = std::move(runs);
    std::vector<std::string> keptLinks = std::move(links);
    std::vector<BlockImage> keptImages = std::move(images);
    std::string keptDisplay = std::move(display);
    keptLines.clear();
    keptRuns.clear();
    keptLinks.clear();
    keptImages.clear();
    keptDisplay.clear();
    *this = BlockLayout {};
    lines = std::move(keptLines);
    runs = std::move(keptRuns);
    links = std::move(keptLinks);
    images = std::move(keptImages);
    display = std::move(keptDisplay);
  }
};

struct TypeMetrics {
  float body = 16.0f;
  float mono = 14.0f;
  float heading[6] = {30.0f, 24.0f, 20.0f, 17.0f, 16.0f, 16.0f};
  float lineHeightRatio = 1.5f;
};

// What measuring *text* needs, and nothing else: the width of a string in a
// style and the height of a line set in one.
//
// Its own type because that is all the line breaker reads, and two callers
// have only this to give. `doc/RenderLayout.h` lays a table cell out with no
// document, no pictures and no complex blocks under it, and would otherwise
// have to invent the two callbacks below to hand over the two it uses.
struct TextMetrics {
  std::function<float(std::string_view, const RunStyle&)> measure;
  std::function<float(const RunStyle&)> lineHeight;
};

// Everything laying a *document* out needs to ask about, which is the two
// above plus what only a whole note has an answer for.
using LineGroup = std::vector<Token>;

// Where a block's text is allowed to go. One value rather than six parameters
// because every one of them is read-only for the whole of a block and they
// only make sense together: a `textLeft` without the `width` that follows it
// does not describe a column.
struct FlowGeometry {
  // Source offset the block starts at. A run's `srcStart`/`srcEnd` are
  // recorded relative to it, so the block can be re-placed without re-shaping.
  std::size_t base = 0;
  float textLeft = 0.0f;
  float width = 0.0f;
  float lineHeight = 0.0f;
  // Where the first line's top sits, in the block's own space.
  float top = 0.0f;
  bool wrap = true;
};

// The two buffers `Flow` needs per block and neither owns. They are borrowed so
// that a document's worth of blocks allocates them once rather than once each;
// the flow clears them on construction, so one of these can be kept for the
// life of a layout and handed to every block.
struct FlowScratch {
  // Whitespace held back until the next word decides whether the line breaks
  // before or after it: an index into the group being walked plus the width it
  // was measured at.
  std::vector<std::pair<std::size_t, float>> pending;
  // The widths of the unbreakable cluster being accumulated, so the break
  // decision is made once for the run rather than once per token.
  std::vector<float> cluster;
};

}

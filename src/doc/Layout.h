#pragma once

#include "CoreAliases.h"

#include "core/editor/TextEdit.h"

#include "doc/BlockScan.h"
#include "doc/InlineScan.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
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
  // The alt text of `![alt](target)`. A role of its own because the picture is
  // drawn under the block now: an underlined accent-coloured line above every
  // image reads as a stray link, where the same words muted read as the caption
  // they are. Still a link -- a remote image has nothing but its alt text to be
  // reached by -- just not one shouting about it.
  ImageAlt,
  Muted
};

// One run of source that shares a style: a word, a run of spaces, or a marker.
// The staging form of a `TextRun`, before the flow has decided which line it
// lands on or where along it. Declared here only so the layout can own the
// buffer these are staged into; the tokenizer that fills them is in Layout.cpp.
struct Token {
  std::size_t start = 0;
  std::size_t end = 0;
  std::string text;
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
  std::vector<std::string> links;
  // Empty for almost every block, so it costs a pointer triple and no
  // allocation for the ones with no picture in them.
  std::vector<BlockImage> images;

  // The runs of one of this block's lines. The line must be one of `lines`;
  // nothing else can address this array.
  std::span<const TextRun> runsOf(const VisualLine& line) const {
    return std::span<const TextRun>(runs).subspan(line.runBegin, line.runCount());
  }
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
  // The box `![alt](target)` takes, fitted to the column and to `maxHeight`.
  // The layout has a buffer, not a texture cache, so it asks -- exactly as it
  // asks about wikilinks. Unset means the note draws no pictures,
  // which is what a layout with no renderer behind it should do.
  std::function<ImageBox(std::string_view target, float column, float maxHeight)> measureImage;
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
  // Whether a `[[target]]` names a note that exists. The layout cannot know --
  // it has a buffer, not a library -- so it asks. Unset means "assume it does",
  // which is what a layout with no library behind it should draw.
  std::function<bool(std::string_view)> wikiLinkResolves;
  // The tallest a picture may be drawn. Part of the geometry, because a shorter
  // window makes a shorter image and so a shorter block: a note is not meant to
  // be one photograph you have to scroll past.
  float imageMaxHeight = 0.0f;
  // Moves whenever `Metrics::measureImage` could answer differently for a
  // target it has already been asked about -- a texture that has finished
  // loading changes the height of the block showing it. Same contract as
  // `wikiLinkRevision`, and for the same reason: it is the one input to a
  // block's layout that is not in the block's own bytes.
  std::uint64_t imageRevision = 0;

  // An identity stamp. Optional, and it exists because the reuse check
  // otherwise has to *prove* that nothing moved -- which costs a pass over the
  // document to establish that a pass over the document is unnecessary. It
  // saves a memcmp of the whole note.
  //
  // Zero means "cannot say", and the layout falls back to comparing bytes --
  // which is what a caller with no revision to offer, a test for instance,
  // should get. A caller that does supply one is promising that it moves
  // whenever the answer would: the stamp must change on every mutation of the
  // buffer.
  std::uint64_t sourceRevision = 0;

  // What the caller changed, and between which two source stamps.
  //
  // `update` is otherwise handed a buffer and no account of what happened to
  // it, so it finds the edit by comparing: forward to the first differing byte
  // and backward to the first differing byte from the end, two `memcmp` passes
  // that on a small edit sum to about the length of the note.
  // `layout.edit_bytes_matched` read 34,835,299 over the harness run against
  // `layout.source_bytes_copied`'s 2,459,143 -- fourteen bytes read for every
  // byte that moved, to locate one typed character.
  //
  // The caller knows: every edit in `src/doc/Edits.h` returns the span it
  // changed, and the editor records the span of every mutation it makes. So
  // this says "everything outside [start, oldEnd) of the buffer you are holding
  // and [start, newEnd) of this one is unchanged", and the comparison starts
  // from there instead of from the ends -- the same claim-and-verify shape
  // `sourceRevision` and `sourceMatches` already have.
  //
  // The two stamps are what make it checkable rather than a matter of trust.
  // `fromRevision` must be the stamp of the buffer the layout is standing on
  // and `toRevision` the stamp of `source`; a claim whose `from` does not match
  // what the layout holds -- two edits landed between updates, or a frame was
  // skipped -- is *discarded*, and the full comparison runs. So is one whose
  // arithmetic does not add up: the bytes outside the span have to be the same
  // count on both sides, or the claim describes some other pair of buffers.
  // Either stamp zero means "cannot say", which is what a caller with no span
  // to offer -- a test, the perf harness -- gets.
  editor::TextEdit editedSpan;
  // Moves whenever `wikiLinkResolves` would answer differently -- a note
  // created, renamed, deleted, or the library re-listed.
  //
  // Unlike the two above this one is not an optimisation. Whether a `[[target]]`
  // resolves decides a run's colour, and it is the one input to a block's layout
  // that is not a function of the block's own bytes, so without it a link that
  // starts or stops resolving keeps the colour it had until somebody happens to
  // edit that block. It is mixed into the geometry, which is to say into every
  // cache key, because a change to the library can change the answer for any
  // link anywhere in the note -- and it moves only on those handful of actions,
  // each of which already re-reads the library.
  std::uint64_t wikiLinkRevision = 0;
};

// One line of the file, tokenized. `Flow` walks a run of these and decides
// where the visual lines fall.
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

class DocumentLayout {
public:
  static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

  void setMetrics(Metrics metrics);
  // Re-lays out only the blocks whose content or geometry actually changed, and
  // returns immediately when nothing did.
  // `source` must not be a view into this layout's own buffer: the source is
  // patched in place from it, so the two cannot be the same bytes. Callers hand
  // in the editor's text, which never is.
  void update(std::string_view source, const LayoutOptions& options);

  // Trivial by design, and defined here rather than in the .cpp for the same
  // reason `doc/Flow.h` is a header: `layout(i)` and `blockTop(i)` are called
  // once per block per frame from four separate paint loops in
  // `app/PageViewPaint.cpp`, and with no LTO in Release a two-line body in
  // another translation unit is an opaque call for every one of them.
  const std::vector<SourceBlock>& blocks() const {
    return blocks_;
  }
  // The blocks, but only when they still partition the buffer stamped
  // `sourceRevision`; an empty span otherwise.
  //
  // This exists so that the block edits -- Enter, Tab, Backspace, the block
  // commands -- can stop re-deriving a partition the layout is already holding.
  // Each of them used to call `scanBlocks` over the whole note to find the one
  // block it acts on, which measured ~200 us on a 200 KB note against the 40 us
  // the keystroke's own layout update costs; Enter ran two or three of them.
  //
  // The stamp is the whole safety argument. `blocks_` describes `source_`, and
  // `sourceRevision_` is what the caller said `source_` was -- so a caller that
  // hands back its own stamp and gets a non-empty span has been told, by the
  // only party that knows, that these blocks are its buffer's. A caller whose
  // buffer has moved since the last update gets nothing and scans, which is
  // what an unstamped caller gets too.
  BlockSpan blocksAt(std::uint64_t sourceRevision) const {
    if(!built_ || sourceRevision == 0 || sourceRevision != sourceRevision_) return {};
    return blocks_;
  }
  std::size_t blockCount() const {
    return blocks_.size();
  }
  const BlockLayout& layout(std::size_t index) const {
    static const BlockLayout empty;
    if(index >= placed_.size() || !placed_[index].layout) return empty;
    return *placed_[index].layout;
  }
  float blockTop(std::size_t index) const {
    if(index >= placed_.size()) return totalHeight_;
    return placed_[index].top;
  }
  float totalHeight() const {
    return totalHeight_;
  }
  const LayoutOptions& options() const {
    return options_;
  }
  const std::string& source() const {
    return source_;
  }

  Rect caretRect(std::size_t offset) const;
  std::size_t offsetAt(float x, float y) const;
  // The rects a selection paints, clipped to the document-space band
  // [bandTop, bandBottom) and appended into a vector the caller owns.
  //
  // The band is not an optimisation the caller may skip. A selection is bounded
  // by the *document*, not by the window: Ctrl+A on a 200 KB note selects 6,600
  // visual rows, of which a screen shows forty. Building all of them measured
  // 673 us -- a third of the frame budget -- and then allocating a 6,600-entry
  // vector to hold them, on every frame the selection was up. The find
  // highlighter next door in `PageView` was given a visible band for exactly
  // this reason; the selection was not, and it is the larger of the two because
  // a selection has no query to be empty.
  void selectionRectsInto(std::size_t from, std::size_t to, float bandTop, float bandBottom,
                          std::vector<Rect>* out) const;
  // The first and last rect the selection paints, without building the ones
  // between them. What a caller that wants to *place* something relative to a
  // selection needs -- the formatting toolbar sits above the first row and falls
  // back to below the last -- and it used to get them by building every rect in
  // the document and reading `front()` and `back()`.
  std::optional<std::pair<Rect, Rect>> selectionEnds(std::size_t from, std::size_t to) const;
  // Every rect, unbounded. For a caller with no viewport to speak of -- the
  // tests -- and the same relationship `scanBlocks` has to `scanBlocksInto`.
  std::vector<Rect> selectionRects(std::size_t from, std::size_t to) const;
  std::optional<std::size_t> blockAt(float y) const;
  // Half-open range of block indices whose boxes intersect the document-space
  // band [top, bottom). Blocks are laid out contiguously and in order, so this
  // is two binary searches -- which is what lets a draw pass cost the viewport
  // rather than the document. A caller that walks every block and tests each
  // one against the viewport is O(document) per frame no matter how little it
  // ends up drawing.
  std::pair<std::size_t, std::size_t> blockRange(float top, float bottom) const;

  // Diagnostics for the perf harness.
  std::size_t lastRelaidBlocks() const {
    return lastRelaid_;
  }

private:
  // One call of `update`, with its five phases as methods. Defined in
  // `Layout.cpp`: it is the algorithm, not part of the interface, and nothing
  // outside that file can name it. See the comment on the definition for why
  // the phases needed a carrier and what it is not allowed to own.
  class UpdatePass;

  // Where a block sits, and the layout it resolved to. There was a
  // `blockIndex` field here as well; nothing ever read it, because it was
  // always the array index. Caching the layout's height and row count here too
  // was tried -- on the theory that reading them through the pointer is a
  // random access into an unordered_map node -- and measured no difference, so
  // the duplicated state is not worth carrying: the nodes for consecutive
  // blocks are allocated in order and the prefetcher covers them.
  struct Placed {
    float top = 0.0f;
    const BlockLayout* layout = nullptr;
  };

  // What a block's layout depends on beyond its own bytes and the geometry.
  struct Flags {
    // The two ends of the buffer. `first` gets no space above it however much
    // its kind would otherwise ask for, and `trailingLine` owns the empty last
    // line a buffer ending in a newline has to put a caret on. Both are here
    // rather than derived from the index at the point of use because the flags
    // are what a block's cached layout is keyed under: a block that keeps its
    // bytes but stops being the first one in the document has to stop sharing
    // the first one's layout, and this is the record that says so.
    bool first = false;
    bool trailingLine = false;
    bool groupFirst = true;     // first line of a quote or callout run
    bool groupLast = true;      // last line of one

    bool operator==(const Flags& other) const {
      return first == other.first && trailingLine == other.trailingLine &&
             groupFirst == other.groupFirst && groupLast == other.groupLast;
    }
  };

  // How much of the buffer an edit left alone: `prefix` bytes match from the
  // start and `suffix` from the end, so everything the edit could have touched
  // lies inside [prefix, size - suffix) of both buffers. Two memcmp-speed
  // passes, and between them they bound the edit.
  struct EditWindow {
    std::size_t prefix = 0;
    std::size_t suffix = 0;
  };
  // The caller's edited span, when it describes the pair of buffers *this*
  // update is about: the one the layout is standing on, and the one it was
  // handed. A claim stamped for any other pair is dropped here and the
  // comparison runs in full, which is what makes a stale claim cost speed
  // rather than correctness -- two edits landing between two updates is the
  // ordinary case of that, and it happens whenever a frame handles more than
  // one keystroke.
  editor::TextEdit claimFor(const LayoutOptions& options) const;

  // `claim` bounds the search when it describes this pair of buffers, and is
  // ignored when it does not. The answer is the same either way: the loops only
  // ever *narrow* the window, so a claim merely says where to start narrowing
  // from.
  static EditWindow matchEdges(std::string_view oldSource, std::string_view newSource,
                               const editor::TextEdit& claim);

  // `blocks_` brought up to date with `source_`, given the window the edit fell
  // inside and how many bytes the buffer held before it.
  //
  // The scan is spliced rather than redone: it resumes at a block boundary the
  // edit provably cannot have moved, stops at the first boundary the previous
  // scan also had inside the bytes the edit left alone, and takes the rest of
  // the list from where it already was -- so an edit costs a scan of the edit
  // plus an offset shift, instead of re-deriving all ten thousand blocks of a
  // 200 KB note to find the one that changed.
  //
  // `head` and `tail` come back as the number of blocks that came through
  // unchanged at each end. Those two numbers are also what the placement patch
  // is built on: the blocks between them are the entire extent of the edit, and
  // the ones after them moved by the change in block count and by nothing else.
  // A cache key is a pure function of a block's bytes and its scan fields --
  // never of where in the buffer it sits -- so a carried-over block keeps its
  // key, its map entry and the layout behind it however far the edit pushed it
  // down the page.
  void rescan(const EditWindow& window, std::size_t previousBytes, std::size_t* head,
              std::size_t* tail);

  // Whether `source` is byte-for-byte what the standing layout was built from,
  // which is also what makes `blocks_` still describe it.
  bool sourceMatches(std::string_view source) const;
  // Whether the standing layout already answers this call exactly. Asked only
  // once the source is known to match; see the definition for the rest.
  bool canReuse(const LayoutOptions& options, std::uint64_t geometry) const;
  // Everything about the shape a block would be laid out in, as one number; see
  // the definition.
  static std::uint64_t geometryKey(const LayoutOptions& options);
  // Sorts and merges `dirty_` so the walk sees each block at most once.
  void mergeDirtyRanges();
  // Drops every cached block layout no live key names any more. Bounded memory,
  // and the reason it is its own function is that it is where a resize spends
  // its worst frame -- see the definition for the numbers.
  void sweepLayoutCache();
  std::size_t blockIndexFor(std::size_t offset) const;
  // The images of a block that has some, laid out down the column from `top`;
  // returns where the block's ink now ends.
  float placeImages(BlockLayout& out, float top) const;

  // The flags block `index` is keyed and laid out under. They depend on the
  // block and on its neighbours' kinds -- and on nothing else, which is
  // precisely what lets the placement patch bound the set of blocks a given
  // change can have moved.
  Flags flagsFor(std::size_t index) const;
  // The rect one visual row of a selection paints, or nothing when that row
  // holds none of it.
  std::optional<Rect> selectionRectFor(std::size_t block, const VisualLine& line, std::size_t from,
                                       std::size_t to) const;
  // Counted into a local and posted once per update. A counter add is a relaxed
  // atomic read-modify-write on a process-wide cacheline: nothing on a call per
  // frame, and about 25 cycles per block when a cold open resolves ten thousand
  // of them.
  struct Tally {
    std::uint64_t keyBytes = 0;
    std::uint64_t cacheHits = 0;
  };
  // Block `index` under `flags`, taken from the cache or laid out into it, with
  // the key it lives under written back through `key`.
  const BlockLayout* resolveEntry(std::size_t index, const Flags& flags, std::uint64_t geometry,
                                  std::uint64_t* key, Tally* tally);

  // The type, the air above and below, and where the text starts. One switch
  // over the block's kind, and the only part of laying a block out that is
  // purely about what kind of block it is.
  struct BlockStyle {
    RunStyle base;
    float padTop = 0.0f;
    float padBottom = 0.0f;
  };
  BlockStyle styleForBlock(const SourceBlock& block, const Flags& flags, BlockLayout& out) const;

  // The next staging group in `flowGroups_`, cleared and ready. `count` is the
  // live prefix: the vector itself is never shrunk, so its tail is last block's
  // tokens, which is exactly the capacity this block wants to reuse.
  std::vector<Token>& nextGroup(std::size_t* count) const;

  // A block's content, staged as one token group per line's worth of source.
  // Two shapes and no third, which is why they are two functions: a fenced code
  // block or a block dropped to raw is the *file's* own lines, and everything
  // else is one group with the inline grammar applied to it. Both return how
  // many groups came out live.
  std::size_t stageSourceLines(const SourceBlock& block, const Flags& flags,
                               const RunStyle& base) const;
  std::size_t stageInlineContent(const SourceBlock& block, const Flags& flags,
                                 const RunStyle& base, BlockLayout& out) const;
  // Everything the inline scanner found, as a per-byte attribute table. The one
  // place the inline grammar reaches the layout: downstream reads only the
  // table. Fills `out.links` and `out.images`, because a span that names a
  // target is the only thing that knows the target.
  void applyInlineSpans(const SourceBlock& block, const std::vector<SourceSpan>& inlines,
                        std::vector<Attr>& attrs, BlockLayout& out) const;
  // What the flow is about to produce, so neither vector doubles its way there.
  void reserveFlowOutput(const SourceBlock& block, const Flags& flags, const RunStyle& base,
                         float available, std::size_t groupCount, BlockLayout& out) const;

  BlockLayout layoutBlock(std::size_t index, const Flags& flags) const;
  const BlockLayout* layoutForOffset(std::size_t offset, std::size_t* blockIndex) const;
  // The visual-row index space, addressed through `lineStart_` rather than
  // through a materialised table of rows.
  std::size_t flatLineCount() const;
  // The block owning row `flat`, and the row's index inside that block.
  std::pair<std::size_t, std::size_t> flatLineAt(std::size_t flat) const;
  float flatLineTop(std::size_t flat) const;
  // The last row starting at or above `y` -- the row `y` falls in, or the last
  // one above it when `y` lands in a block's padding.
  std::size_t flatLineAtY(float y) const;

  Metrics metrics_;
  LayoutOptions options_;
  std::string source_;
  std::vector<SourceBlock> blocks_;
  std::vector<Placed> placed_;
  // Prefix sum of visual lines: `lineStart_[i]` is how many rows the blocks
  // before `i` contribute, so `lineStart_.back()` is the document's row count
  // and a row maps back to its block by binary search. This was a materialised
  // (block, line, top) record per visual row -- 13.5k of them on a 460 KB note,
  // rebuilt from scratch on every update and then scanned *linearly* by both of
  // its readers. The prefix sum is one integer per block, filled by the
  // placement walk that was happening anyway, and every query is a binary
  // search over it.
  std::vector<std::uint32_t> lineStart_;
  std::unordered_map<std::uint64_t, BlockLayout> cache_;
  std::vector<std::uint64_t> liveKeys_;
  // `liveKeys_` sorted, for the cache sweep. A member so the sweep does not
  // allocate a copy of it on the one frame it is already the slowest thing in.
  std::vector<std::uint64_t> liveSorted_;
  // The flags each standing block was keyed under. Half of the identity a
  // carried-over key needs: identical bytes are not enough when a block stops
  // being the first in the document, or stops ending a quote run.
  std::vector<Flags> flags_;
  // The blocks the partial rescan produced, before they are spliced into
  // `blocks_`. A member so a keystroke's rescan allocates nothing.
  std::vector<SourceBlock> scanned_;
  // The blocks whose entry this update can have moved, as a few inclusive
  // ranges rather than one span: a caret at the top of a note and an edit at
  // the bottom are two blocks of work, and a single interval covering both
  // would be the whole document. Sorted and merged before the walk reads it.
  std::vector<std::pair<std::size_t, std::size_t>> dirty_;
  // Scratch for the per-block flow. `Flow` is constructed once per block, so a
  // buffer it owns is grown from empty ten thousand times over a document --
  // which is most of what laying one out allocates. Held here instead, the
  // buffers are grown once and reused by every block after the first.
  // `mutable` because laying a block out is logically a const query.
  mutable FlowScratch flowScratch_;
  // The source line spans of a fenced code block or a block dropped to raw.
  // Same reason: one per such block, returned by value, was one allocation per
  // such block.
  mutable std::vector<std::pair<std::size_t, std::size_t>> sourceLines_;
  // The token groups a block is staged into before it is flowed. Same reason:
  // one per block, and the inner vectors keep their capacity between blocks, so
  // a document's worth of tokenizing grows its buffers once.
  mutable std::vector<std::vector<Token>> flowGroups_;
  // The inline scan's buffers and the per-byte attribute table it fills, held
  // for the same reason: both were an allocation per block on a path that runs
  // once per block, and the mask inside the scan was paid even by the four
  // blocks in five with no markup in them.
  mutable InlineScratch inlineScratch_;
  mutable std::vector<Attr> attrs_;
  float totalHeight_ = 0.0f;
  std::size_t lastRelaid_ = 0;
  // What the standing layout was built from, so the next call can ask whether
  // it would produce the same thing again.
  std::uint64_t geometryHash_ = 0;
  // The stamp the standing layout was built under, or zero when it was built
  // by a caller that did not offer one.
  std::uint64_t sourceRevision_ = 0;
  bool built_ = false;
};

}

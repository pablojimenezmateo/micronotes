#include "export/NotePdf.h"

#include "core/pdf/PdfContent.h"
#include "core/pdf/PdfDocument.h"
#include "core/platform/DurableFile.h"
#include "doc/Layout.h"
#include "export/PdfBlocks.h"
#include "export/PdfFaces.h"
#include "export/PdfPage.h"
#include "export/PdfPictures.h"
#include "export/PdfComplex.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <utility>

namespace micronotes::exporting {
namespace {

using microcore::pdf::PdfContent;
using microcore::pdf::PdfDocument;

// The vertical span of one atomic piece of a block: a visual line, a picture,
// or a table row. Pagination happens between these and never inside one --
// a line cut in half horizontally is unreadable, and a picture cut in half is
// worse.
struct Item {
  float top = 0.0f;
  float bottom = 0.0f;
};

// A page that has been composed but not yet given to the document.
//
// Buffered rather than added as it is finished, because the footer says "3 of
// 11" and nothing knows what eleven is until the last note has been laid out.
// The alternative is a page number with no total, which on a printed stack is
// the one thing the number is for.
struct BufferedPage {
  std::string body;
  // What the running header at the top of the page says, or empty for the
  // first page of a note -- where the note's own title block already says it.
  std::string runningTitle;
  // The live rects the painters collected while composing this page. They
  // cannot go into the stream: an annotation is a property of the page object,
  // so it travels with the bytes and is handed to `addPage` beside them.
  std::vector<PdfDocument::Annotation> links;
};

// The bottom of the text area, and how much of it is left.
constexpr float kPageBottom = kMarginTop + kContentHeight;

// A heading with almost nothing under it is a heading on the wrong page. Two
// body lines is the usual threshold and it is what this uses: less than that
// left after the heading, and the heading goes over with its text.
constexpr float kOrphanGuard = 2.0f;

class Composer {
public:
  Composer(PdfDocument& document, const PdfFaces& faces, PdfComplex& complex, PdfPictures* pictures)
    : document_(&document), faces_(&faces), complex_(&complex), pictures_(pictures) {}

  void note(const PdfNote& note, const std::function<bool(std::string_view)>& wikiResolves);
  std::size_t flush();

private:
  PdfContent& page();
  void openPage();
  void closePage();
  float remaining() const { return kPageBottom - y_; }
  bool pageIsEmpty() const { return !content_ || content_->empty(); }

  void titleBlock(const PdfNote& note);
  void emitBlock(const doc::DocumentLayout& layout, std::size_t index, std::string_view source,
                 const QuoteGround& ground);
  std::vector<Item> itemsOf(const doc::DocumentLayout& layout, std::size_t index,
                            std::string_view source) const;

  PdfDocument* document_ = nullptr;
  const PdfFaces* faces_ = nullptr;
  PdfComplex* complex_ = nullptr;
  PdfPictures* pictures_ = nullptr;

  std::optional<PdfContent> content_;
  std::vector<PdfDocument::Annotation> links_;
  std::vector<BufferedPage> pages_;
  float y_ = kMarginTop;
  std::string noteTitle_;
  // The running header is suppressed on the first page of each note.
  bool firstPageOfNote_ = true;
  std::string pendingRunningTitle_;
};

PdfContent& Composer::page() {
  if(!content_) openPage();
  return *content_;
}

void Composer::openPage() {
  content_.emplace(kPageHeight);
  pendingRunningTitle_ = firstPageOfNote_ ? std::string {} : noteTitle_;
  firstPageOfNote_ = false;
  y_ = kMarginTop;
}

void Composer::closePage() {
  if(!content_) return;
  pages_.push_back({content_->bytes(), pendingRunningTitle_, std::move(links_)});
  links_.clear();
  content_.reset();
  y_ = kMarginTop;
}

// The note's own head: its name, the tags it carries, and a rule under both.
void Composer::titleBlock(const PdfNote& note) {
  const doc::TypeMetrics type = printTypeMetrics();
  const ui::Theme& theme = ink();
  PdfContent& content = page();

  doc::RunStyle titleStyle;
  titleStyle.size = type.heading[0];
  titleStyle.strong = true;
  const int titleSlot = faces_->slot(titleStyle);
  auto& titleFont = document_->font(titleSlot);
  const float titleLine = std::round(titleStyle.size * 1.25f);
  content.text(titleFont, titleSlot, titleStyle.size, kMarginX,
               y_ + titleFont.ascent(titleStyle.size), note.title, theme.textPrimary);
  y_ += titleLine;

  if(!note.tags.empty()) {
    doc::RunStyle tagStyle;
    tagStyle.size = kFurnitureSize + 1.0f;
    const int tagSlot = faces_->slot(tagStyle);
    auto& tagFont = document_->font(tagSlot);
    std::string line;
    for(const auto& tag : note.tags) {
      if(!line.empty()) line += "   ";
      line += "#" + tag;
    }
    y_ += 3.0f;
    content.text(tagFont, tagSlot, tagStyle.size, kMarginX, y_ + tagFont.ascent(tagStyle.size),
                 line, theme.textMuted);
    y_ += std::round(tagStyle.size * 1.4f);
  }

  y_ += 6.0f;
  content.line(kMarginX, y_, kMarginX + kContentWidth, y_, 0.6f, theme.border);
  y_ += kTitleBlockGap;
}

std::vector<Item> Composer::itemsOf(const doc::DocumentLayout& layout, std::size_t index,
                                    std::string_view source) const {
  const doc::BlockLayout& block = layout.layout(index);
  std::vector<Item> items;
  if(block.complex) {
    // The render model's own atoms: a table's rows, and every other block of
    // it whole. That is what lets a table longer than a page become several
    // pages of table instead of one page and a hole.
    for(const auto& slice : complex_->layoutOf(source, kContentWidth).slices) {
      items.push_back({slice.top, slice.bottom});
    }
    return items;
  }
  items.reserve(block.lines.size() + block.images.size());
  for(const auto& line : block.lines) items.push_back({line.y, line.y + line.height});
  for(const auto& image : block.images) {
    if(image.rect.h <= 0.0f) continue;
    items.push_back({image.rect.y, image.rect.y + image.rect.h});
  }
  std::sort(items.begin(), items.end(),
            [](const Item& a, const Item& b) { return a.top < b.top; });
  return items;
}

// Which `>` run each block belongs to, worked out once for the note.
//
// It cannot be read off a block on its own: only the run's first line carries
// the `[!KIND]` that makes it a callout rather than a quote, and the ground is
// drawn over the run rather than over any one line of it. The screen resolves
// the same thing per frame inside its paint loop, which it can afford because
// it only ever paints the blocks in the window.
std::vector<QuoteGround> quoteGrounds(const doc::DocumentLayout& layout) {
  std::vector<QuoteGround> grounds(layout.blockCount());
  const auto& blocks = layout.blocks();
  for(std::size_t i = 0; i < blocks.size(); ++i) {
    if(!doc::startsQuoteRun(blocks, i)) continue;
    std::size_t last = i;
    while(!doc::endsQuoteRun(blocks, last) && last + 1 < blocks.size()) ++last;
    const bool callout = blocks[i].kind == doc::BlockKind::Callout;
    const std::string_view kind = callout ? blocks[i].info(layout.source()) : std::string_view {};
    for(std::size_t j = i; j <= last; ++j) {
      grounds[j].inRun = true;
      grounds[j].callout = callout;
      grounds[j].kind = kind;
      // Down to where the next block of the run starts, so the pieces tile.
      grounds[j].height = j < last ? layout.blockTop(j + 1) - layout.blockTop(j)
                                   : layout.layout(j).height;
    }
    i = last;
  }
  return grounds;
}

void Composer::emitBlock(const doc::DocumentLayout& layout, std::size_t index,
                         std::string_view source, const QuoteGround& ground) {
  const doc::BlockLayout& block = layout.layout(index);
  if(block.height <= 0.0f) return;
  const doc::TypeMetrics type = printTypeMetrics();

  // A heading whose text would land on the next page anyway goes there with
  // it, rather than sitting alone at the foot of this one.
  if(layout.blocks()[index].kind == doc::BlockKind::Heading && !pageIsEmpty() &&
     remaining() < block.height + type.body * type.lineHeightRatio * kOrphanGuard) {
    closePage();
  }

  std::vector<Item> items = itemsOf(layout, index, source);
  if(items.empty()) items.push_back({0.0f, block.height});

  std::size_t next = 0;
  float from = 0.0f;
  while(next < items.size()) {
    PdfContent& content = page();
    // A table continued onto this page repeats the row its columns are named
    // in, which is page space the rows themselves do not account for -- the
    // header is drawn twice. Reserved here, off the same function the paint
    // asks, so the two cannot disagree about how far down the first row goes.
    const float repeated =
      block.complex ? doc::repeatedHeaderHeight(complex_->layoutOf(source, kContentWidth), next)
                    : 0.0f;
    const float available = remaining() - repeated;
    std::size_t cut = next;
    while(cut < items.size() && items[cut].bottom - from <= available) ++cut;
    if(cut == next) {
      // Nothing fits. On a page with something on it, try a fresh one; on an
      // empty page the item is taller than any page and overflowing it is the
      // only way to make progress -- refusing would loop forever.
      if(!pageIsEmpty()) {
        closePage();
        continue;
      }
      ++cut;
    }
    const float to = std::min(items[cut - 1].bottom, block.height);

    if(block.complex) {
      ComplexSlice complexSlice;
      complexSlice.source = source;
      complexSlice.width = kContentWidth;
      complexSlice.x = kMarginX;
      complexSlice.y = y_;
      complexSlice.from = next;
      complexSlice.to = cut;
      complexSlice.links = &links_;
      complex_->paint(content, complexSlice);
    } else {
      BlockInk paintInk {document_, faces_, pictures_, &links_};
      BlockSlice slice;
      slice.from = from;
      slice.to = to;
      slice.pageY = y_;
      slice.columnLeft = kMarginX;
      slice.columnWidth = kContentWidth;
      slice.ground = ground;
      paintBlockSlice(content, paintInk, layout, index, slice);
    }

    y_ += repeated + to - from;
    from = to;
    next = cut;
    if(next < items.size()) closePage();
  }
  // Whatever the block reserved past its last line -- the air under a heading,
  // a code block's bottom padding -- is space on the page too.
  if(from < block.height) y_ += block.height - from;
}

void Composer::note(const PdfNote& note, const std::function<bool(std::string_view)>& wikiResolves) {
  // Notes never share a page: a folder's PDF is its notes bound together, not
  // its notes run into each other.
  closePage();
  noteTitle_ = note.title;
  firstPageOfNote_ = true;

  doc::DocumentLayout layout;
  doc::Metrics metrics = printMetrics(*faces_, *document_);
  const std::string& body = note.body;
  PdfComplex* complex = complex_;
  PdfPictures* pictures = pictures_;
  metrics.measureComplex = [complex, &body](const doc::SourceBlock& block, float width) {
    const std::size_t start = std::min(block.start, body.size());
    const std::size_t end = std::min(block.end(), body.size());
    return complex->layoutOf(std::string_view(body).substr(start, end - start), width).height;
  };
  metrics.measureImage = [pictures](std::string_view target, float column,
                                    float maxHeight) -> doc::ImageBox {
    if(!pictures) return {};
    const auto& picture = pictures->resolve(target);
    if(!picture.drawable()) return {};
    // Fitted to the column and to a share of the page, the same way the
    // reading pane fits one -- so a note is not one photograph per page.
    const float scale = std::min({1.0f, column / picture.width, maxHeight / picture.height});
    return {std::round(picture.width * scale), std::round(picture.height * scale)};
  };
  layout.setMetrics(std::move(metrics));

  doc::LayoutOptions options = printLayoutOptions(kContentWidth);
  options.wikiLinkResolves = wikiResolves;
  options.imageMaxHeight = kContentHeight * 0.62f;
  layout.update(body, options);

  titleBlock(note);

  const std::vector<QuoteGround> grounds = quoteGrounds(layout);
  float previousBottom = layout.blockCount() > 0 ? layout.blockTop(0) : 0.0f;
  for(std::size_t index = 0; index < layout.blockCount(); ++index) {
    const float top = layout.blockTop(index);
    // The air the layout left between this block and the one above it. Not at
    // the top of a page: a page that opens with a paragraph's worth of
    // whitespace looks like a mistake rather than like spacing.
    if(!pageIsEmpty()) y_ += std::max(0.0f, top - previousBottom);
    const std::size_t start = std::min(layout.blocks()[index].start, body.size());
    const std::size_t end = std::min(layout.blocks()[index].end(), body.size());
    emitBlock(layout, index, std::string_view(body).substr(start, end - start), grounds[index]);
    previousBottom = top + layout.layout(index).height;
  }
}

std::size_t Composer::flush() {
  closePage();
  const ui::Theme& theme = ink();
  doc::RunStyle style;
  style.size = kFurnitureSize;
  const int slot = faces_->slot(style);

  for(std::size_t i = 0; i < pages_.size(); ++i) {
    // The furniture is appended to the body rather than drawn before it. Both
    // are balanced sequences of operators, so concatenating them composes; and
    // doing it here is what lets the footer name a total the composer did not
    // know while it was laying the page out.
    PdfContent furniture(kPageHeight);
    auto& font = document_->font(slot);
    const std::string number =
      std::to_string(i + 1) + " of " + std::to_string(pages_.size());
    const float width = font.width(number, style.size);
    furniture.text(font, slot, style.size, (kPageWidth - width) / 2.0f, kFooterBaseline, number,
                   theme.textMuted);

    if(!pages_[i].runningTitle.empty()) {
      furniture.text(font, slot, style.size, kMarginX, kHeaderBaseline, pages_[i].runningTitle,
                     theme.textMuted);
      furniture.line(kMarginX, kHeaderBaseline + 5.0f, kMarginX + kContentWidth,
                     kHeaderBaseline + 5.0f, 0.4f, theme.border);
    }
    document_->addPage(kPageWidth, kPageHeight, pages_[i].body + furniture.bytes(),
                       std::move(pages_[i].links));
  }
  return pages_.size();
}

}

std::string pdfDate(std::time_t when) {
  std::tm parts {};
#if defined(_WIN32)
  localtime_s(&parts, &when);
#else
  localtime_r(&when, &parts);
#endif
  // Big enough for the widest thing the format specifiers can produce rather
  // than for the sixteen characters a real date takes: `%04d` is a minimum
  // width, not a maximum, and a `tm` is a struct anyone can hand a year of
  // 2,000,000,000 to.
  char buffer[80] = {};
  std::snprintf(buffer, sizeof(buffer), "D:%04d%02d%02d%02d%02d%02d", parts.tm_year + 1900,
                parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
  return buffer;
}

PdfResult renderPdf(const PdfRequest& request, std::string* bytes) {
  PdfResult result;
  if(bytes == nullptr) {
    result.error = "Nowhere to write the PDF";
    return result;
  }
  if(request.notes.empty()) {
    result.error = "Nothing to export";
    return result;
  }

  PdfDocument document;
  PdfFaces faces;
  if(!faces.load(document)) {
    result.error = "No usable font to export with";
    return result;
  }
  PdfComplex complex(document, faces);
  std::unique_ptr<PdfPictures> pictures;
  if(!request.libraryRoot.empty()) {
    pictures = std::make_unique<PdfPictures>(document, request.libraryRoot);
  }

  complex.resolveWikiLinksWith(request.wikiLinkResolves);
  Composer composer(document, faces, complex, pictures.get());
  for(const auto& note : request.notes) composer.note(note, request.wikiLinkResolves);
  result.pages = composer.flush();

  PdfDocument::Info info;
  info.title = request.documentTitle;
  info.creator = "micronotes";
  info.creationDate = request.creationDate;
  *bytes = document.finish(info);
  result.ok = result.pages > 0;
  if(!result.ok) result.error = "Nothing to export";
  return result;
}

PdfResult writePdf(const PdfRequest& request, const std::filesystem::path& file) {
  std::string bytes;
  PdfResult result = renderPdf(request, &bytes);
  if(!result.ok) return result;
  if(!platform::writeFileDurably(file, bytes)) {
    result.ok = false;
    result.error = "Could not write " + file.filename().string();
  }
  return result;
}

}

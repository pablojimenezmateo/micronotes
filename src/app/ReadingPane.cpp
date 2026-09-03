#include "app/ReadingPane.h"

#include "app/InlineText.h"
#include "app/MarkdownBlocks.h"
#include "app/PageHeader.h"
#include "core/attachments/AttachmentService.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "ui/Metrics.h"
#include "ui/RowBand.h"
#include "ui/Settings.h"
#include "ui/ShellLayout.h"
#include "ui/TextUtil.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

namespace micronotes::app {

using ui::ClipGuard;
using ui::drawEmptyMessage;
using ui::drawVerticalScrollbar;
using ui::ellipsizeToWidth;
using ui::fill;
using ui::hLine;
using ui::ImageCache;
using ui::isRemoteTarget;
using ui::Rect;
using ui::splitLines;
using ui::TextRenderer;
using ui::theme;


std::string anchorFor(std::string value) {
  std::string out;
  bool pendingDash = false;
  for(unsigned char c : value) {
    if(std::isalnum(c)) {
      if(pendingDash && !out.empty()) out.push_back('-');
      out.push_back(static_cast<char>(std::tolower(c)));
      pendingDash = false;
    } else if(!out.empty()) {
      pendingDash = true;
    }
  }
  return out;
}

const markdown::Document& previewDocument(UiRuntime& ui) {
  if(!ui.cachedMarkdownDocument || ui.cachedMarkdownRevision != ui.editor.revision()) {
    ui.cachedMarkdownSource = ui.editor.text();
    ui.cachedMarkdownRevision = ui.editor.revision();
    ui.cachedMarkdownDocument = ui.parser.parse(ui.cachedMarkdownSource);
  }
  return *ui.cachedMarkdownDocument;
}

namespace {

std::vector<std::string> codeBlockLines(const markdown::Block& block) {
  auto lines = splitLines(blockText(block));
  if(lines.size() > 1 && lines.back().empty()) lines.pop_back();
  if(lines.empty()) lines.emplace_back();
  return lines;
}

std::vector<markdown::Inline> blockImages(const markdown::Block& block) {
  std::vector<markdown::Inline> out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) out.push_back(inlineItem);
  }
  return out;
}

float listMarkerWidth(const markdown::Block& block) {
  if(block.type == markdown::BlockType::OrderedItem) return 26.0f;
  if(block.type == markdown::BlockType::UnorderedItem) return 18.0f;
  return 0.0f;
}

float blockBottomSpacing(const markdown::Block& block) {
  if(block.type == markdown::BlockType::BlankLine) return 0.0f;
  if(block.type == markdown::BlockType::OrderedItem || block.type == markdown::BlockType::UnorderedItem) return 2.0f;
  if(block.type == markdown::BlockType::Heading) return 14.0f;
  // An Html block reads as part of the flow around it, so it closes tighter.
  // The measure pass used to pass the real answer here and the draw pass used
  // to pass `false`, so a note carrying raw HTML measured two pixels per block
  // taller than it drew and scrolled past its own end.
  if(block.type == markdown::BlockType::Html) return 8.0f;
  return 10.0f;
}

// Where a callout's name starts, measured from the box's left edge: clear of
// the mark that sits in the gutter beside it.
constexpr float kAdmonitionLabelLeft = 18.0f;

float admonitionLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Admonition) return 0.0f;
  const auto label = ui::calloutLabel(block.admonitionType);
  return kAdmonitionLabelLeft + static_cast<float>(text.width(label, false, false, true)) + 10.0f;
}

float footnoteLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Footnote) return 0.0f;
  const auto label = "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
  return static_cast<float>(text.width(label)) + 12.0f;
}

std::string imagePlaceholder(const markdown::Inline& image, const UiRuntime& ui,
                             attachments::AttachmentService& attachmentService) {
  if(isRemoteTarget(image.target) || !ui.state.hasLibrary()) return "[remote image skipped: " + image.target + "]";
  try {
    const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
    if(!attachmentService.isSupportedImage(path)) return "[image link: " + image.target + "]";
  } catch(const std::exception&) {
    return "[unsafe image path]";
  }
  return "[image unavailable: " + image.target + "]";
}

float imagePlaceholderHeight(TextRenderer& text, std::string_view placeholder, float width) {
  const auto lines = wrapText(text, placeholder, static_cast<int>(width), false, true);
  return static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight() + 2) + 8);
}

// The chrome a block puts in front of its first word, and what is left for the
// text after it. One answer, read by the measure and by the draw: they used to
// each add their own list of widths up, in a different order, and a text column
// that disagrees with the one the block was measured at re-wraps every line.
struct BlockChrome {
  float indent = 0.0f;
  float marker = 0.0f;
  float quote = 0.0f;
  float extra = 0.0f;
  float textLeft = 0.0f;   // from the content column's left edge
  float textWidth = 0.0f;
};

BlockChrome chromeFor(TextRenderer& text, const markdown::Block& block, float contentWidth) {
  BlockChrome chrome;
  chrome.indent = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
  chrome.marker = listMarkerWidth(block);
  chrome.quote = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
  chrome.extra = admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
  chrome.textLeft = chrome.indent + chrome.marker + chrome.quote + chrome.extra;
  chrome.textWidth = contentWidth - chrome.textLeft;
  return chrome;
}

// A block's own text height, without whatever follows it.
float blockTextHeight(TextRenderer& text, const markdown::Block& block, const BlockChrome& chrome) {
  const auto runs = inlineRuns(block, theme().text);
  const int lineStep = blockLineStep(text, block);
  return static_cast<float>(measureInlineLines(text, runs, static_cast<int>(chrome.textWidth),
                                               blockTextStyle(block).size) * lineStep);
}

// How tall an image ends up, and how wide, once it has been fitted to the
// column and to the page. Rounded here rather than at the draw, so the height
// the layout reserved and the height the texture takes are the same number.
struct ImageBox {
  float w = 0.0f;
  float h = 0.0f;
  bool drawable = false;
};

ImageBox fitImage(float imageW, float imageH, float contentWidth, float pageHeight) {
  ImageBox box;
  if(imageW <= 0.0f || imageH <= 0.0f) return box;
  const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
  const float maxH = std::max(40.0f, pageHeight * 0.55f);
  const float scale = std::min(std::min(1.0f, maxW / imageW), maxH / imageH);
  box.w = std::round(imageW * scale);
  box.h = std::round(imageH * scale);
  box.drawable = true;
  return box;
}

// The texture behind an image link, or nothing when there is not one. Both
// passes ask, and `ImageCache` answers the second one out of its map.
SDL_Texture* imageTexture(ImageCache& images, const UiRuntime& ui,
                          attachments::AttachmentService& attachmentService,
                          const markdown::Inline& image, float& imageW, float& imageH) {
  if(isRemoteTarget(image.target) || !ui.state.hasLibrary()) return nullptr;
  try {
    const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
    if(!attachmentService.isSupportedImage(path)) return nullptr;
    return images.load(path, imageW, imageH);
  } catch(const std::exception&) {
    return nullptr;
  }
}

// How much vertical space one image link takes, texture or placeholder.
float imageHeight(TextRenderer& text, ImageCache& images, const UiRuntime& ui,
                  attachments::AttachmentService& attachmentService, const markdown::Inline& image,
                  float contentWidth, float pageHeight) {
  float imageW = 0.0f;
  float imageH = 0.0f;
  if(imageTexture(images, ui, attachmentService, image, imageW, imageH)) {
    const ImageBox box = fitImage(imageW, imageH, contentWidth, pageHeight);
    if(box.drawable) return box.h + 14.0f;
  }
  return imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), contentWidth);
}

// One walk of the document: where every block sits in the note's own scrolling
// space, the ordinal each ordered item draws, and the anchors an in-note link
// can jump to. Everything the draw needs in order to start at the first visible
// block instead of measuring its way down to it.
void buildViewerLayout(TextRenderer& text, ImageCache& images, UiRuntime& ui,
                       const markdown::Document& doc, float contentWidth, float pageHeight) {
  const perf::ScopeTimer timer("viewer.layout");
  perf::addCounter(perf::CounterId::ViewerLayoutBuilds);
  auto& memo = ui.viewerLayout;
  memo.top.clear();
  memo.ordinal.clear();
  memo.anchors.clear();
  memo.top.reserve(doc.blocks.size() + 1);
  memo.ordinal.reserve(doc.blocks.size());

  attachments::AttachmentService attachmentService;
  float y = 0.0f;
  int orderedIndex = 1;
  int footnoteIndex = 1;
  for(const auto& block : doc.blocks) {
    perf::addCounter(perf::CounterId::ViewerBlocksMeasured);
    memo.top.push_back(y);
    const bool ordered = block.type == markdown::BlockType::OrderedItem;
    if(!ordered) orderedIndex = 1;
    memo.ordinal.push_back(block.orderedNumber > 0 ? block.orderedNumber : orderedIndex);

    if(block.type == markdown::BlockType::Heading) {
      const auto anchor = anchorFor(blockText(block));
      if(!anchor.empty()) memo.anchors[anchor] = static_cast<int>(std::max(0.0f, y));
    } else if(block.type == markdown::BlockType::Footnote && !block.footnoteLabel.empty()) {
      memo.anchors["fn-" + block.footnoteLabel] = static_cast<int>(std::max(0.0f, y));
      memo.anchors["fn-" + std::to_string(footnoteIndex++)] = static_cast<int>(std::max(0.0f, y));
    }

    const BlockChrome chrome = chromeFor(text, block, contentWidth);
    if(block.type == markdown::BlockType::BlankLine) {
      y += static_cast<float>(text.lineHeight());
    } else if(block.type == markdown::BlockType::HorizontalRule) {
      y += 22.0f;
    } else if(block.type == markdown::BlockType::Table) {
      y += tableHeight(text, block, contentWidth - chrome.indent) + 12.0f;
    } else if(block.type == markdown::BlockType::Code) {
      const auto lines = codeBlockLines(block);
      y += static_cast<float>(std::max<std::size_t>(1, lines.size()) *
                              lineStepFor(text, blockTextStyle(block), 1.5f)) + 18.0f;
    } else {
      if(!blockText(block).empty()) {
        y += blockTextHeight(text, block, chrome) + blockBottomSpacing(block);
      }
      for(const auto& image : blockImages(block)) {
        y += imageHeight(text, images, ui, attachmentService, image, contentWidth, pageHeight);
      }
    }
    if(ordered) ++orderedIndex;
  }
  // One past the last block, so the last block's end and the content height are
  // the same number and the band search needs no special case for the end.
  memo.top.push_back(y);
}

}

void drawReadingPane(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui,
                     Rect rect) {
  fill(renderer, rect, theme().viewerBg);
  // The live page's geometry, not a second copy of it: `ui::pageRectIn` and
  // `ui::pageColumnIn` are what the live surface lays itself out with, so the
  // same note has the same measure whichever pane is reading it. There is no
  // gutter here -- the reading pane has no hover handles to reserve room for.
  const Rect page = ui::pageRectIn(rect);
  // No border. A rule around a page that fills its pane draws a box nobody is
  // outside of; which pane has the keyboard is said by the focus edge instead.
  ui::fillRounded(renderer, page, theme().pageSurface, ui::kRadiusMedium);
  ui::drawFocusEdge(renderer, page, ui.focus == FocusArea::Viewer);
  const auto& doc = previewDocument(ui);
  // The header is part of the note's scrolling space, above its first block,
  // rather than a banner the note passes under.
  const float headerHeight = pageHeaderHeight(text, ui);
  const float scrollTop = page.y + 14.0f;
  const float contentTop = scrollTop + headerHeight;
  const ui::PageColumn column = ui::pageColumnIn(page, 0.0f);
  const float contentLeft = column.left;
  const float contentWidth = column.width;

  // The layout is a pure function of the parsed note and the geometry, and a
  // frame happens for reasons that have nothing to do with either -- a hover, a
  // scroll, a window focus. Every input it has is in this key; the image
  // generation is here because a texture that has finished loading changes the
  // height of the block that shows it.
  auto& memo = ui.viewerLayout;
  const bool reusable = memo.valid && memo.blocks == doc.blocks.size() &&
                        memo.sourceRevision == ui.editor.revision() &&
                        memo.imageGeneration == images.generation() &&
                        memo.contentWidth == contentWidth && memo.pageHeight == page.h &&
                        memo.fontScale == text.displayScale() && memo.bodySize == ui::type().body;
  if(reusable) {
    perf::addCounter(perf::CounterId::ViewerLayoutReused);
  } else {
    buildViewerLayout(text, images, ui, doc, contentWidth, page.h);
    memo.valid = true;
    memo.blocks = doc.blocks.size();
    memo.sourceRevision = ui.editor.revision();
    memo.imageGeneration = images.generation();
    memo.contentWidth = contentWidth;
    memo.pageHeight = page.h;
    memo.fontScale = text.displayScale();
    memo.bodySize = ui::type().body;
  }
  // Recorded rather than recomputed. This used to be a second whole-document
  // measure, block by block, run to answer "is the pointer over the scrollbar"
  // on every mouse motion and again per motion event for the length of a drag.
  const float contentHeight = memo.top.empty() ? 0.0f : memo.top.back();
  ui.viewerMaxScroll = std::max(0, static_cast<int>(std::ceil(contentHeight + headerHeight - page.h + 24.0f)));
  ui.viewerScroll = std::clamp(ui.viewerScroll, 0, ui.viewerMaxScroll);

  {
    ClipGuard clip(renderer, {page.x + 1, page.y + 1, page.w - 2, page.h - 2});
    drawPageHeader(renderer, text, ui, {contentLeft, scrollTop, contentWidth, headerHeight},
                   scrollTop - static_cast<float>(ui.viewerScroll));
    const float origin = contentTop - static_cast<float>(ui.viewerScroll);
    // The blocks the page can show, by two binary searches over their tops --
    // the same shape the live surface and the sidebar use, and for the same
    // reason: this walked every block of the document and measured each one to
    // find where the next began, which is O(note) per frame to draw a screenful.
    const auto [first, last] = ui::rowBand(doc.blocks.size(), page.y, page.y + page.h,
                                           [&memo, origin](std::size_t i) {
                                             return Rect {0.0f, origin + memo.top[i], 0.0f,
                                                          memo.top[i + 1] - memo.top[i]};
                                           });
    attachments::AttachmentService attachmentService;
    const float pageBottom = page.y + page.h;
    for(std::size_t i = first; i < last; ++i) {
      const auto& block = doc.blocks[i];
      perf::addCounter(perf::CounterId::ViewerBlocksDrawn);
      float y = origin + memo.top[i];
      const bool code = block.type == markdown::BlockType::Code;
      const bool ordered = block.type == markdown::BlockType::OrderedItem;
      const bool unordered = block.type == markdown::BlockType::UnorderedItem;
      const bool quote = block.type == markdown::BlockType::Quote;
      const bool rule = block.type == markdown::BlockType::HorizontalRule;
      const bool table = block.type == markdown::BlockType::Table;
      const bool admonition = block.type == markdown::BlockType::Admonition;
      const bool footnote = block.type == markdown::BlockType::Footnote;
      const bool blankLine = block.type == markdown::BlockType::BlankLine;
      const BlockChrome chrome = chromeFor(text, block, contentWidth);
      const float textX = contentLeft + chrome.textLeft;
      const ui::TextStyle blockStyle = blockTextStyle(block);
      if(blankLine) {
        continue;
      }
      if(rule) {
        hLine(renderer, contentLeft + chrome.indent, contentLeft + contentWidth, y + 8.0f, theme().divider);
        continue;
      }
      if(table) {
        const float blockH = tableHeight(text, block, contentWidth - chrome.indent);
        drawTable(renderer, text, ui.linkRegions, block,
                  {contentLeft + chrome.indent, y, contentWidth - chrome.indent, blockH});
        continue;
      }
      if(code) {
        const auto lines = codeBlockLines(block);
        const int step = lineStepFor(text, blockStyle, 1.5f);
        const float blockH = static_cast<float>(std::max<std::size_t>(1, lines.size()) * step) + 10.0f;
        const Rect codeRect {contentLeft + chrome.indent, y - 6.0f, contentWidth - chrome.indent, blockH};
        ui::fillRounded(renderer, codeRect, theme().codeBg, ui::kRadiusSmall);
        float codeY = y;
        for(const auto& codeLine : lines) {
          if(codeY + static_cast<float>(step) >= page.y && codeY <= pageBottom) {
            text.draw(ellipsizeToWidth(text, codeLine, static_cast<int>(codeRect.w - 20.0f), false, true),
                      codeRect.x + 10.0f, codeY, theme().text, blockStyle);
          }
          codeY += static_cast<float>(step);
        }
        continue;
      }

      if(!blockText(block).empty()) {
        const auto runs = inlineRuns(block, theme().text);
        const int lineStep = blockLineStep(text, block);
        const float blockH = blockTextHeight(text, block, chrome);
        if(quote) {
          fill(renderer, {contentLeft + chrome.indent, y - 2.0f, 3.0f, blockH + 2.0f}, theme().divider);
        }
        if(admonition) {
          // The palette *and* the geometry the live surface uses, from one
          // place: both were written out here as well, which is how the same
          // callout ended up with its kind capitalised on one surface and
          // lower-cased on the other.
          const ui::CalloutStyle callStyle = ui::calloutStyle(block.admonitionType);
          const Rect callout {contentLeft + chrome.indent, y - 7.0f, contentWidth - chrome.indent, blockH + 12.0f};
          ui::fillRounded(renderer, callout, callStyle.surface, ui::kRadiusMedium);
          ui::fillRounded(renderer,
                          {std::round(callout.x + ui::kCalloutMarkInset),
                           std::round(y + (static_cast<float>(text.lineHeight()) - ui::kCalloutMarkSize) / 2.0f),
                           ui::kCalloutMarkSize, ui::kCalloutMarkSize},
                          callStyle.accent, ui::kCalloutMarkSize / 2.0f);
          text.draw(ui::calloutLabel(block.admonitionType), callout.x + kAdmonitionLabelLeft, y,
                    callStyle.accent, false, false, true);
        }
        if(footnote) {
          const auto label = block.footnoteLabel.empty() ? "*" : block.footnoteLabel;
          text.draw("[" + label + "]", contentLeft + chrome.indent, y, theme().accent);
        }
        if(ordered) {
          text.draw(std::to_string(memo.ordinal[i]) + ".", contentLeft + chrome.indent, y, theme().muted);
        } else if(unordered) {
          if(block.task) {
            const Rect box {contentLeft + chrome.indent, y + 3.0f, 13.0f, 13.0f};
            if(block.taskChecked) {
              ui::fillRounded(renderer, box, theme().accent, ui::kRadiusSmall);
              const SDL_Color tick = theme().onAccent;
              SDL_SetRenderDrawColor(renderer, tick.r, tick.g, tick.b, tick.a);
              SDL_RenderLine(renderer, box.x + 3.0f, box.y + 6.5f, box.x + 5.5f, box.y + 9.0f);
              SDL_RenderLine(renderer, box.x + 5.5f, box.y + 9.0f, box.x + 10.0f, box.y + 4.0f);
            } else {
              ui::strokeRounded(renderer, box, theme().dim, ui::kRadiusSmall);
            }
          } else {
            text.draw("•", contentLeft + chrome.indent, y, theme().muted);
          }
        }
        drawInlineRuns(renderer, text, &ui.linkRegions, runs, textX, y,
                       static_cast<int>(chrome.textWidth), lineStep, blockStyle.size);
        y += blockH + blockBottomSpacing(block);
      }

      for(const auto& image : blockImages(block)) {
        float imageW = 0.0f;
        float imageH = 0.0f;
        SDL_Texture* texture = imageTexture(images, ui, attachmentService, image, imageW, imageH);
        const ImageBox box = texture ? fitImage(imageW, imageH, contentWidth, page.h) : ImageBox {};
        if(texture && box.drawable) {
          const SDL_FRect dst {contentLeft, std::round(y), box.w, box.h};
          if(dst.y + dst.h >= page.y && dst.y <= pageBottom) {
            SDL_RenderTexture(renderer, texture, nullptr, &dst);
            ui.linkRegions.push_back({{dst.x, dst.y, dst.w, dst.h}, image.target});
          }
          y += box.h + 14.0f;
          continue;
        }
        const auto placeholder = imagePlaceholder(image, ui, attachmentService);
        const auto lines = wrapText(text, placeholder, static_cast<int>(contentWidth), false, true);
        const bool clickable = isRemoteTarget(image.target);
        float placeholderY = y;
        for(const auto& line : lines) {
          if(placeholderY + static_cast<float>(text.lineHeight()) >= page.y && placeholderY <= pageBottom) {
            const auto lineW = static_cast<float>(text.width(line, false, true));
            text.draw(line, contentLeft, placeholderY, clickable ? theme().accent : theme().dim, false, true);
            if(clickable && lineW > 0.0f) {
              ui.linkRegions.push_back({{contentLeft, placeholderY, lineW,
                                         static_cast<float>(text.lineHeight())}, image.target});
              hLine(renderer, contentLeft, contentLeft + lineW,
                    placeholderY + static_cast<float>(text.lineHeight() - 2), theme().accentDim);
            }
          }
          placeholderY += static_cast<float>(text.lineHeight() + 2);
        }
        y = placeholderY + 8.0f;
      }
    }
    if(doc.blocks.empty()) {
      // Where the note's first block would have been: on the content column,
      // under the header. It used to be drawn at the page's own origin, which
      // is where `drawPageHeader` has just drawn the title -- so an empty note
      // showed its own name written across the message telling you it was
      // empty. The height `drawEmptyMessage` returns is not needed here, since
      // nothing follows it, but the origin it now takes is.
      drawEmptyMessage(text, "Nothing to read yet", "This note has no text in it.", contentLeft,
                       origin, contentWidth,
                       ui.state.workspace().paneMode() == ui::PaneMode::Split
                         ? "type on the left"
                         : ui::keysFor(ui::ActionId::PaneLive) + "  go back and write");
    }
  }
  drawVerticalScrollbar(renderer, page, ui.viewerScroll, ui.viewerMaxScroll);
}

}

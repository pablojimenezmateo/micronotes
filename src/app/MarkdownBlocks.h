#pragma once

#include "CoreAliases.h"

#include "app/InlineText.h"
#include "app/LinkRegion.h"
#include "core/markdown/MarkdownParser.h"
#include "doc/BlockScan.h"
#include "ui/TextRenderer.h"
#include "ui/Rect.h"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

// Measuring and drawing a `markdown::Block` -- the block-level counterpart of
// InlineText, which does the same for the runs inside one.
//
// Three surfaces share it: the reading view, the split pane, and the blocks the
// live editing surface hands back to md4c because its own scanner deliberately
// does not model them. It came out of Application.cpp because all three were
// calling the same file-local statics, which meant none of the three could move
// out on its own.
namespace micronotes::app {

// By reference only, in three signatures. A forward declaration rather than
// `app/Shell.h`: a drawing helper that takes the runtime to reach one cache on
// it does not need the whole runtime's definition to say so.
struct UiRuntime;

// A block's text with its inline syntax resolved: link labels rather than
// targets, code spans in backticks unless the block is itself code.
std::string blockText(const markdown::Block& block);
// The same for a bare run of inlines, without the backticks -- what a table
// cell needs to measure its own width.
std::string inlinePlainText(const std::vector<markdown::Inline>& inlines);

ui::TextStyle blockTextStyle(const markdown::Block& block);
// Baseline-to-baseline distance. The font's own height is roughly 1.2x, which
// reads too tight for body copy, so the type scale's ratio wins when larger.
int lineStepFor(ui::TextRenderer& text, const ui::TextStyle& style, float ratio);
int blockLineStep(ui::TextRenderer& text, const markdown::Block& block);

float tableHeight(ui::TextRenderer& text, const markdown::Block& block, float width);
void drawTable(SDL_Renderer* renderer, ui::TextRenderer& text, std::vector<LinkRegion>& links,
               const markdown::Block& block, ui::Rect rect);

// Drops every cached parse the note no longer contains, and nothing else.
//
// The parse cache is keyed on a block's own source, so its live set is exactly
// the `Complex` blocks of the buffer on screen. That is the rule
// `doc::DocumentLayout` uses for the block layouts these mirror -- keep one
// generation of the document, sweep when the cache runs past it -- and unlike
// an LRU it cannot be defeated by a note bigger than the cap: the cap *is* the
// note. The 64-entry version this replaced made room by clearing itself, so a
// note with 65 tables re-parsed all of them on every relayout, for a hit rate
// of exactly zero.
//
// Called once a frame, and costs one comparison on the frames where the cache
// has not grown past what the last sweep found live -- which is all of them,
// apart from a note opening or an edit that adds a table.
void sweepComplexCache(UiRuntime& ui, doc::BlockSpan blocks, std::string_view source);

// A block the live scanner does not model, parsed on its own and rendered
// through the md4c path so that tables and raw HTML look the same everywhere.
// The parse is cached against the block's own source text.
float measureComplexBlock(ui::TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block,
                          float width);
void drawComplexBlock(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                      const doc::SourceBlock& block, ui::Rect rect);

}

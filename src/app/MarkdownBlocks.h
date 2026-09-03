#pragma once

#include "CoreAliases.h"

#include "app/InlineText.h"
#include "app/Shell.h"
#include "core/markdown/MarkdownParser.h"
#include "doc/BlockScan.h"
#include "ui/Draw.h"
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

// A block the live scanner does not model, parsed on its own and rendered
// through the md4c path so that tables and raw HTML look the same everywhere.
// The parse is cached against the block's own source text.
float measureComplexBlock(ui::TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block,
                          float width);
void drawComplexBlock(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                      const doc::SourceBlock& block, ui::Rect rect);

}

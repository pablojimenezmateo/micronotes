#pragma once

#include "CoreAliases.h"

#include "app/Shell.h"
#include "core/markdown/MarkdownParser.h"
#include "ui/Draw.h"
#include "ui/Theme.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Laying out and drawing a run of formatted inline text -- the md4c render
// model's side of the shell, used by the reading view, the split pane, and the
// blocks the live surface hands back to md4c.
//
// It came out of Application.cpp whole. Nothing here touches the runtime, the
// library or the layout: it takes parsed inlines and a width and produces
// pixels, which is why it was the piece that could leave first.
namespace micronotes::app {

// One stretch of text with one appearance. A link keeps its target so the draw
// can underline it and hand back a region to click.
struct InlineRun {
  std::string text;
  std::string target;
  SDL_Color color = ui::theme().textPrimary;
  bool mono = false;
  bool strong = false;
  bool emphasis = false;
  bool strikethrough = false;
  // A `[[wikilink]]` names a note and every other link names a file or a URL.
  // Which one it is has to travel with the run: the two are indistinguishable
  // as strings and are followed in completely different ways.
  bool wiki = false;
  // Inline code takes a tinted box behind it, the way the live surface draws
  // it. Kept as a flag rather than baked into the colour because the fill is
  // the draw's business and the runs are the model.
  bool code = false;
};

// Whether a `[[target]]` names a note that exists. Unset means "assume it
// does" -- the same contract `PageViewHooks::wikiLinkResolves` has, because it
// answers the same question for the other surface.
using WikiResolver = std::function<bool(std::string_view)>;

// md4c has no notion of `[[Some Note]]`: it hands the brackets back as literal
// text. So the runs are split on them here, by the same rule
// `doc::InlineScan` applies on the live surface (`ui::findWikiLink`). Without
// this the pane that exists for *reading* a note showed the raw markup of every
// link between notes and offered nothing to click.
std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines,
                                  SDL_Color baseColor = ui::theme().textPrimary,
                                  const WikiResolver& wikiResolves = {});
std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor = ui::theme().textPrimary,
                                  const WikiResolver& wikiResolves = {});

// How many wrapped lines these runs need at this width. The measure and the
// draw wrap through the same tokenizer, so a block cannot be reserved one
// height and then drawn at another.
int measureInlineLines(ui::TextRenderer& text, const std::vector<InlineRun>& runs, int maxWidth, float size);

// Draws them, returning the y of the last line. `links` collects the clickable
// regions, and may be null for a caller that has nowhere to put them.
float drawInlineRuns(SDL_Renderer* renderer, ui::TextRenderer& text, std::vector<LinkRegion>* links,
                     const std::vector<InlineRun>& runs, float x, float y, int maxWidth, int lineStep,
                     float size);

}

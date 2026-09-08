#include "app/InlineText.h"


#include "doc/WikiLink.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace micronotes::app {

using ui::TextRenderer;
using ui::fill;
using ui::hLine;
using ui::theme;

namespace {

// One layout token. Whitespace between tokens is recorded as a flag rather than
// kept as text, so a run boundary never invents a space that the source did not
// have (the "[link](url)." case) and never drops one that it did.
struct LaidWord {
  const InlineRun* run = nullptr;
  std::string text;
  bool spaceBefore = false;
  bool lineBreak = false;
};

std::vector<LaidWord> layoutWords(const std::vector<InlineRun>& runs) {
  std::vector<LaidWord> out;
  bool pendingSpace = false;
  bool atStart = true;
  std::string token;
  const InlineRun* tokenRun = nullptr;

  auto flush = [&]() {
    if(token.empty()) return;
    LaidWord word;
    word.run = tokenRun;
    word.text = token;
    word.spaceBefore = pendingSpace && !atStart;
    out.push_back(std::move(word));
    token.clear();
    pendingSpace = false;
    atStart = false;
  };

  for(const auto& run : runs) {
    for(const char c : run.text) {
      if(c == '\n') {
        flush();
        LaidWord br;
        br.run = &run;
        br.lineBreak = true;
        out.push_back(std::move(br));
        pendingSpace = false;
        atStart = true;
      } else if(std::isspace(static_cast<unsigned char>(c))) {
        flush();
        pendingSpace = true;
      } else {
        if(token.empty()) tokenRun = &run;
        token.push_back(c);
      }
    }
    flush();
  }
  flush();
  return out;
}

ui::TextStyle runStyle(const InlineRun& run, float size) {
  ui::TextStyle style;
  style.family = run.mono ? ui::FontFamily::Mono : ui::FontFamily::Sans;
  style.strong = run.strong;
  style.italic = run.emphasis;
  style.size = size;
  return style;
}

// Whether two runs differ only in their text, so one can be appended to the
// other. A run with a target is never merged: a link is a unit.
bool sameAppearance(const InlineRun& a, const InlineRun& b) {
  return a.target.empty() && b.target.empty() && a.mono == b.mono && a.code == b.code &&
         a.strong == b.strong && a.emphasis == b.emphasis && a.strikethrough == b.strikethrough &&
         a.color.r == b.color.r && a.color.g == b.color.g && a.color.b == b.color.b &&
         a.color.a == b.color.a;
}

// Splits a run of plain text on the `[[wikilinks]]` in it, appending the
// literal stretches and the links in order. `run` carries whatever emphasis
// the text already had, so `*[[a]]*` stays italic.
void appendWithWikiLinks(std::vector<InlineRun>& runs, InlineRun&& run,
                         const WikiResolver& wikiResolves) {
  // The overwhelming majority of runs carry no `[[` at all, and this is on the
  // per-block path, so the cheap test comes first.
  if(run.text.find("[[") == std::string::npos) {
    runs.push_back(std::move(run));
    return;
  }
  std::size_t copied = 0;
  while(const auto span = doc::findWikiLink(run.text, copied)) {
    if(span->start > copied) {
      InlineRun before = run;
      before.text = run.text.substr(copied, span->start - copied);
      runs.push_back(std::move(before));
    }
    InlineRun link = run;
    link.text = span->label;
    link.target = span->target;
    link.wiki = true;
    // A link to a note that is not there yet is an offer, not an error -- the
    // same distinction, and the same two colours, the live surface draws.
    const bool resolves = !wikiResolves || wikiResolves(span->target);
    link.color = resolves ? theme().accent : theme().linkPending;
    runs.push_back(std::move(link));
    copied = span->end;
  }
  if(copied < run.text.size()) {
    InlineRun after = std::move(run);
    after.text = after.text.substr(copied);
    runs.push_back(std::move(after));
  }
}

}

std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines, SDL_Color baseColor,
                                  const WikiResolver& wikiResolves) {
  std::vector<InlineRun> runs;
  runs.reserve(inlines.size());
  // Adjacent plain-text runs are joined before the wikilink scan and only then
  // split again, because md4c breaks its text at every `[`: it tries to read
  // `[[Deep Note]]` as a link, finds no `(`, and hands back the pieces
  // `"A ["`, `"["`, `"Deep Note]] ..."`. No single one of those contains a
  // `[[`, so a scan that looked at them one at a time could never find one --
  // which is why the reading pane showed the raw brackets of every link
  // between notes.
  //
  // Joining them is worth doing on its own: the wrap tokenizer below walks
  // runs, and a paragraph with three brackets in it arrived as five runs where
  // one would do.
  // A plain value and a flag rather than an `optional`: GCC 13 cannot see
  // through `optional<InlineRun>` and warns that the string inside it may be
  // used uninitialised, which is a false positive the build cannot distinguish
  // from a real one under -Werror.
  InlineRun pending;
  bool havePending = false;
  const auto flush = [&]() {
    if(!havePending) return;
    appendWithWikiLinks(runs, std::move(pending), wikiResolves);
    pending = InlineRun {};
    havePending = false;
  };
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    InlineRun run;
    run.text = inlineItem.text;
    run.color = baseColor;
    run.strong = inlineItem.strong;
    run.emphasis = inlineItem.emphasis;
    run.strikethrough = inlineItem.strikethrough;
    // Only a run md4c handed back as text can be hiding a wikilink. A link's
    // label, a code span and raw HTML are all what they say they are.
    bool splittable = false;
    switch(inlineItem.type) {
      case markdown::InlineType::Link:
        run.text = inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
        run.target = inlineItem.target;
        run.color = theme().accent;
        break;
      case markdown::InlineType::Code:
        run.mono = true;
        run.code = true;
        // The ink the live surface uses. It was `theme().warn` here, which is
        // the colour a destructive action and an unsaved buffer are drawn in,
        // so the same `code` span read as ordinary text while being edited and
        // as a warning while being read.
        run.color = theme().textPrimary;
        break;
      case markdown::InlineType::Emphasis:
        run.emphasis = true;
        splittable = true;
        break;
      case markdown::InlineType::Strong:
        run.strong = true;
        splittable = true;
        break;
      case markdown::InlineType::Strikethrough:
        run.strikethrough = true;
        splittable = true;
        break;
      case markdown::InlineType::FootnoteRef:
        run.text = "[" + inlineItem.text + "]";
        run.target = "#fn-" + inlineItem.text;
        run.color = theme().accent;
        break;
      case markdown::InlineType::Html:
        run.color = theme().textMuted;
        break;
      case markdown::InlineType::Text:
        splittable = true;
        break;
      case markdown::InlineType::Image:
        break;
    }
    if(run.text.empty()) continue;
    if(!splittable) {
      flush();
      runs.push_back(std::move(run));
      continue;
    }
    if(havePending && sameAppearance(pending, run)) {
      pending.text += run.text;
      continue;
    }
    flush();
    pending = std::move(run);
    havePending = true;
  }
  flush();
  return runs;
}

std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor,
                                  const WikiResolver& wikiResolves) {
  return inlineRuns(block.inlines, baseColor, wikiResolves);
}

int measureInlineLines(TextRenderer& text, const std::vector<InlineRun>& runs, int maxWidth, float size) {
  int lines = 1;
  int x = 0;
  for(const auto& word : layoutWords(runs)) {
    if(word.lineBreak) {
      ++lines;
      x = 0;
      continue;
    }
    const auto style = runStyle(*word.run, size);
    const int wordW = text.width(word.text, style);
    const int spaceW = (x == 0 || !word.spaceBefore) ? 0 : text.width(" ", style);
    if(x > 0 && x + spaceW + wordW > maxWidth) {
      ++lines;
      x = wordW;
    } else {
      x += spaceW + wordW;
    }
  }
  return std::max(1, lines);
}

float drawInlineRuns(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>* links, const std::vector<InlineRun>& runs, float x, float y, int maxWidth, int lineStep, float size) {
  float cursorX = x;
  float cursorY = y;
  // Remembers where the previous word of the same link ended, so the underline
  // runs through the spaces inside a multi-word link instead of breaking up.
  const InlineRun* previousRun = nullptr;
  float previousEndX = 0.0f;
  float previousY = -1.0f;
  for(const auto& word : layoutWords(runs)) {
    if(word.lineBreak) {
      cursorX = x;
      cursorY += static_cast<float>(lineStep);
      previousRun = nullptr;
      continue;
    }
    const auto& run = *word.run;
    const auto style = runStyle(run, size);
    const int lineH = text.lineHeight(style);
    const int wordW = text.width(word.text, style);
    const int spaceW = (cursorX == x || !word.spaceBefore) ? 0 : text.width(" ", style);
    if(cursorX > x && cursorX + static_cast<float>(spaceW + wordW) > x + static_cast<float>(maxWidth)) {
      cursorX = x;
      cursorY += static_cast<float>(lineStep);
    } else {
      cursorX += static_cast<float>(spaceW);
    }
    // The tinted box behind inline code, which the live surface draws and this
    // one did not: `code` was distinguished only by being mono and orange.
    if(run.code) {
      fill(renderer, {cursorX - 2.0f, cursorY + 1.0f, static_cast<float>(wordW) + 4.0f,
                      static_cast<float>(lineH) - 2.0f}, theme().codeBackground);
    }
    text.draw(word.text, cursorX, cursorY, run.color, style);
    if(run.strikethrough) {
      const float lineY = cursorY + static_cast<float>(lineH) * 0.55f;
      hLine(renderer, cursorX, cursorX + static_cast<float>(wordW), lineY, run.color);
    }
    if(!run.target.empty()) {
      const bool continues = previousRun == &run && std::abs(previousY - cursorY) < 0.5f;
      const float underlineFrom = continues ? previousEndX : cursorX;
      hLine(renderer, underlineFrom, cursorX + static_cast<float>(wordW), cursorY + static_cast<float>(lineH - 2), theme().accent);
      if(links) {
        links->push_back({{underlineFrom, cursorY, cursorX + static_cast<float>(wordW) - underlineFrom,
                           static_cast<float>(lineH)}, run.target, run.wiki});
      }
    }
    cursorX += static_cast<float>(wordW);
    previousRun = run.target.empty() ? nullptr : &run;
    previousEndX = cursorX;
    previousY = cursorY;
  }
  return cursorY;
}

}

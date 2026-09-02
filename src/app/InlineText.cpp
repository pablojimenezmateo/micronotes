#include "app/InlineText.h"

#include "ui/TextUtil.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace micronotes::app {

using ui::TextRenderer;
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

}

std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines, SDL_Color baseColor) {
  std::vector<InlineRun> runs;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    InlineRun run;
    run.text = inlineItem.text;
    run.color = baseColor;
    run.strong = inlineItem.strong;
    run.emphasis = inlineItem.emphasis;
    run.strikethrough = inlineItem.strikethrough;
    if(inlineItem.type == markdown::InlineType::Link) {
      run.text = inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
      run.target = inlineItem.target;
      run.color = theme().accent;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      run.mono = true;
      run.color = theme().warn;
    } else if(inlineItem.type == markdown::InlineType::Emphasis) {
      run.emphasis = true;
    } else if(inlineItem.type == markdown::InlineType::Strong) {
      run.strong = true;
    } else if(inlineItem.type == markdown::InlineType::Strikethrough) {
      run.strikethrough = true;
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      run.text = "[" + inlineItem.text + "]";
      run.target = "#fn-" + inlineItem.text;
      run.color = theme().accent;
    } else if(inlineItem.type == markdown::InlineType::Html) {
      run.color = theme().dim;
    }
    if(!run.text.empty()) runs.push_back(std::move(run));
  }
  return runs;
}

std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor) {
  return inlineRuns(block.inlines, baseColor);
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
    text.draw(word.text, cursorX, cursorY, run.color, style);
    if(run.strikethrough) {
      const float lineY = cursorY + static_cast<float>(lineH) * 0.55f;
      hLine(renderer, cursorX, cursorX + static_cast<float>(wordW), lineY, run.color);
    }
    if(!run.target.empty()) {
      const bool continues = previousRun == &run && std::abs(previousY - cursorY) < 0.5f;
      const float underlineFrom = continues ? previousEndX : cursorX;
      hLine(renderer, underlineFrom, cursorX + static_cast<float>(wordW), cursorY + static_cast<float>(lineH - 2), theme().accentDim);
      if(links) {
        links->push_back({{underlineFrom, cursorY, cursorX + static_cast<float>(wordW) - underlineFrom, static_cast<float>(lineH)}, run.target});
      }
    }
    cursorX += static_cast<float>(wordW);
    previousRun = run.target.empty() ? nullptr : &run;
    previousEndX = cursorX;
    previousY = cursorY;
  }
  return cursorY;
}

std::vector<std::string> wrapText(TextRenderer& text, std::string_view value, int maxWidth, bool heading, bool mono) {
  std::vector<std::string> out;
  if(maxWidth <= 0) {
    out.emplace_back(value);
    return out;
  }
  std::istringstream logicalLines {std::string(value)};
  std::string logicalLine;
  while(std::getline(logicalLines, logicalLine)) {
    if(logicalLine.empty()) {
      out.emplace_back();
      continue;
    }
    std::string line;
    std::istringstream words {logicalLine};
    std::string word;
    while(words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if(!line.empty() && text.width(candidate, heading, mono) > maxWidth) {
        out.push_back(line);
        line = word;
        // A word too long for the measure is broken across lines. By bisection
        // over code point boundaries, because the two obvious ways to write this
        // are both wrong: shortening a byte at a time costs a shaping pass per
        // byte, and a byte is not a character, so a cut can land inside a UTF-8
        // sequence and hand the renderer something that is not text.
        while(text.width(line, heading, mono) > maxWidth) {
          const std::size_t cut = ui::breakToFit(
            line, maxWidth,
            [&](std::string_view part) { return text.width(part, heading, mono); });
          if(cut == 0 || cut >= line.size()) break;
          out.push_back(line.substr(0, cut));
          line.erase(0, cut);
        }
      } else {
        line = candidate;
      }
    }
    out.push_back(line);
  }
  if(out.empty()) out.emplace_back();
  return out;
}
}

#include "doc/InlineScan.h"

#include <algorithm>
#include <cctype>

namespace micronotes::doc {
namespace {

bool isAsciiPunct(char c) {
  return std::ispunct(static_cast<unsigned char>(c)) != 0;
}

bool isWordChar(char c) {
  const auto u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || u >= 0x80;
}

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool looksLikeAutolink(std::string_view value) {
  if(value.empty()) return false;
  if(value.find_first_of(" \t\n<>") != std::string_view::npos) return false;
  if(value.find("://") != std::string_view::npos) return true;
  if(value.rfind("mailto:", 0) == 0) return true;
  const auto at = value.find('@');
  return at != std::string_view::npos && at > 0 && value.find('.', at) != std::string_view::npos;
}

// Matches a bracket pair starting at `open`, skipping masked bytes so a `]`
// inside a code span never closes a link label.
std::size_t matchBracket(std::string_view text, const std::vector<char>& masked, std::size_t open, char openChar, char closeChar) {
  int depth = 0;
  for(std::size_t i = open; i < text.size(); ++i) {
    if(masked[i]) continue;
    if(text[i] == openChar) ++depth;
    else if(text[i] == closeChar) {
      --depth;
      if(depth == 0) return i;
    }
  }
  return std::string_view::npos;
}

std::string linkTarget(std::string_view inside) {
  while(!inside.empty() && isSpace(inside.front())) inside.remove_prefix(1);
  while(!inside.empty() && isSpace(inside.back())) inside.remove_suffix(1);
  if(!inside.empty() && inside.front() == '<') {
    const auto close = inside.find('>');
    if(close != std::string_view::npos) return std::string(inside.substr(1, close - 1));
  }
  const auto space = inside.find_first_of(" \t");
  if(space != std::string_view::npos) inside = inside.substr(0, space);
  return std::string(inside);
}

struct Delimiter {
  std::size_t pos = 0;
  std::size_t length = 0;
  char marker = '*';
};

}

std::vector<SourceSpan> scanInlines(std::string_view text, std::size_t base) {
  std::vector<SourceSpan> spans;
  if(text.empty()) return spans;

  // Bytes that structural scanning has claimed. Emphasis delimiters are only
  // recognised outside them.
  std::vector<char> masked(text.size(), 0);
  const auto mask = [&](std::size_t from, std::size_t to) {
    for(std::size_t i = from; i < to && i < masked.size(); ++i) masked[i] = 1;
  };
  const auto add = [&](SourceSpan span) {
    span.start += base;
    span.end += base;
    span.contentStart += base;
    span.contentEnd += base;
    span.openStart += base;
    span.openEnd += base;
    span.closeStart += base;
    span.closeEnd += base;
    spans.push_back(std::move(span));
  };

  // Pass 1: escapes, code spans and autolinks. These win over everything.
  for(std::size_t i = 0; i < text.size();) {
    const char c = text[i];
    if(c == '\\' && i + 1 < text.size() && isAsciiPunct(text[i + 1])) {
      SourceSpan span;
      span.kind = SpanKind::Escape;
      span.start = i;
      span.end = i + 2;
      span.openStart = i;
      span.openEnd = i + 1;
      span.contentStart = i + 1;
      span.contentEnd = i + 2;
      span.closeStart = i + 2;
      span.closeEnd = i + 2;
      add(std::move(span));
      mask(i, i + 2);
      i += 2;
      continue;
    }
    if(c == '`') {
      std::size_t run = 0;
      while(i + run < text.size() && text[i + run] == '`') ++run;
      std::size_t scan = i + run;
      std::size_t close = std::string_view::npos;
      while(scan < text.size()) {
        if(text[scan] != '`') {
          ++scan;
          continue;
        }
        std::size_t closeRun = 0;
        while(scan + closeRun < text.size() && text[scan + closeRun] == '`') ++closeRun;
        if(closeRun == run) {
          close = scan;
          break;
        }
        scan += closeRun;
      }
      if(close == std::string_view::npos) {
        i += run;
        continue;
      }
      SourceSpan span;
      span.kind = SpanKind::Code;
      span.start = i;
      span.end = close + run;
      span.openStart = i;
      span.openEnd = i + run;
      span.contentStart = i + run;
      span.contentEnd = close;
      span.closeStart = close;
      span.closeEnd = close + run;
      add(std::move(span));
      mask(i, close + run);
      i = close + run;
      continue;
    }
    if(c == '<') {
      const auto close = text.find('>', i + 1);
      if(close != std::string_view::npos && looksLikeAutolink(text.substr(i + 1, close - i - 1))) {
        SourceSpan span;
        span.kind = SpanKind::Autolink;
        span.start = i;
        span.end = close + 1;
        span.openStart = i;
        span.openEnd = i + 1;
        span.contentStart = i + 1;
        span.contentEnd = close;
        span.closeStart = close;
        span.closeEnd = close + 1;
        span.target = std::string(text.substr(i + 1, close - i - 1));
        add(std::move(span));
        mask(i, close + 1);
        i = close + 1;
        continue;
      }
    }
    ++i;
  }

  // Pass 2: wikilinks, before ordinary links, because `[[a]]` would otherwise
  // be read as the label `[a]` looking for a `(` it never finds -- and worse,
  // `[[a](b)]` is a link either way round. Claiming them first settles it.
  //
  // The whole span is masked, unlike a link: what is between the brackets is a
  // note's title, so a `*` in it is part of the name rather than emphasis.
  for(std::size_t i = 0; i + 3 < text.size();) {
    if(masked[i] || text[i] != '[' || text[i + 1] != '[' || masked[i + 1]) {
      ++i;
      continue;
    }
    std::size_t close = std::string_view::npos;
    for(std::size_t scan = i + 2; scan + 1 < text.size(); ++scan) {
      if(masked[scan]) continue;
      // A `[` inside would be someone typing, not a nested link: there is no
      // such thing, so the first `]]` closes.
      if(text[scan] == ']' && text[scan + 1] == ']') {
        close = scan;
        break;
      }
    }
    if(close == std::string_view::npos || close == i + 2) {
      // Unterminated, or empty. Leave the brackets as the literal text they are.
      ++i;
      continue;
    }
    const std::size_t innerStart = i + 2;
    // The first bar splits the target from what to show instead of it.
    std::size_t bar = std::string_view::npos;
    for(std::size_t scan = innerStart; scan < close; ++scan) {
      if(text[scan] == '|') {
        bar = scan;
        break;
      }
    }
    SourceSpan span;
    span.kind = SpanKind::WikiLink;
    span.start = i;
    span.end = close + 2;
    span.openStart = i;
    // With an alias, the target and the bar are part of the marker, so hiding
    // the markers leaves exactly the words the writer chose to show.
    span.openEnd = bar == std::string_view::npos ? innerStart : bar + 1;
    span.contentStart = span.openEnd;
    span.contentEnd = close;
    span.closeStart = close;
    span.closeEnd = close + 2;
    span.target = std::string(text.substr(innerStart, (bar == std::string_view::npos ? close : bar) - innerStart));
    // An alias with nothing before the bar has no target to go to.
    if(span.target.empty()) {
      ++i;
      continue;
    }
    add(std::move(span));
    mask(i, close + 2);
    i = close + 2;
  }

  // Pass 3: links and images. Only their markers are masked, so emphasis inside
  // a link label still matches.
  for(std::size_t i = 0; i < text.size();) {
    if(masked[i] || text[i] != '[') {
      ++i;
      continue;
    }
    const bool image = i > 0 && text[i - 1] == '!' && !masked[i - 1];
    const auto labelEnd = matchBracket(text, masked, i, '[', ']');
    if(labelEnd == std::string_view::npos || labelEnd + 1 >= text.size() || text[labelEnd + 1] != '(' || masked[labelEnd + 1]) {
      ++i;
      continue;
    }
    const auto targetEnd = matchBracket(text, masked, labelEnd + 1, '(', ')');
    if(targetEnd == std::string_view::npos) {
      ++i;
      continue;
    }
    SourceSpan span;
    span.kind = image ? SpanKind::Image : SpanKind::Link;
    span.start = image ? i - 1 : i;
    span.end = targetEnd + 1;
    span.openStart = span.start;
    span.openEnd = i + 1;
    span.contentStart = i + 1;
    span.contentEnd = labelEnd;
    span.closeStart = labelEnd;
    span.closeEnd = targetEnd + 1;
    span.target = linkTarget(text.substr(labelEnd + 2, targetEnd - labelEnd - 2));
    add(std::move(span));
    mask(span.openStart, span.openEnd);
    mask(span.closeStart, span.closeEnd);
    i = span.contentStart;
  }

  // Pass 4: emphasis, strong and strikethrough delimiter runs.
  std::vector<Delimiter> open;
  for(std::size_t i = 0; i < text.size();) {
    const char c = text[i];
    if(masked[i] || (c != '*' && c != '_' && c != '~')) {
      ++i;
      continue;
    }
    std::size_t length = 0;
    while(i + length < text.size() && text[i + length] == c && !masked[i + length]) ++length;
    const char before = i > 0 ? text[i - 1] : ' ';
    const char after = i + length < text.size() ? text[i + length] : ' ';
    bool canOpen = !isSpace(after);
    bool canClose = !isSpace(before);
    if(c == '_') {
      // Intraword underscores are literal, so snake_case survives.
      canOpen = canOpen && !isWordChar(before);
      canClose = canClose && !isWordChar(after);
    }
    if(c == '~') {
      canOpen = canOpen && length >= 2;
      canClose = canClose && length >= 2;
    }

    std::size_t remaining = length;
    if(canClose) {
      while(remaining > 0 && !open.empty()) {
        auto found = open.rend();
        for(auto it = open.rbegin(); it != open.rend(); ++it) {
          if(it->marker == c) {
            found = it;
            break;
          }
        }
        if(found == open.rend()) break;
        auto& opener = *found;
        const std::size_t use = (remaining >= 2 && opener.length >= 2) ? 2 : 1;
        if(c == '~' && use < 2) break;
        SourceSpan span;
        span.kind = use == 2 ? (c == '~' ? SpanKind::Strike : SpanKind::Strong) : SpanKind::Emphasis;
        span.openStart = opener.pos + opener.length - use;
        span.openEnd = opener.pos + opener.length;
        span.closeStart = i + (length - remaining);
        span.closeEnd = span.closeStart + use;
        span.contentStart = span.openEnd;
        span.contentEnd = span.closeStart;
        span.start = span.openStart;
        span.end = span.closeEnd;
        if(span.contentStart > span.contentEnd) break;
        add(std::move(span));
        opener.length -= use;
        remaining -= use;
        // Delimiters opened inside the span can no longer match anything.
        const auto keep = static_cast<std::size_t>(std::distance(found, open.rend()));
        open.resize(keep);
        if(open.back().length == 0) open.pop_back();
      }
    }
    if(remaining > 0 && canOpen) {
      open.push_back({i + (length - remaining), remaining, c});
    }
    i += length;
  }

  std::sort(spans.begin(), spans.end(), [](const SourceSpan& a, const SourceSpan& b) {
    if(a.start != b.start) return a.start < b.start;
    return a.end > b.end;
  });
  std::vector<std::size_t> ends;
  for(auto& span : spans) {
    while(!ends.empty() && ends.back() <= span.start) ends.pop_back();
    span.depth = static_cast<int>(ends.size());
    ends.push_back(span.end);
  }
  return spans;
}

}

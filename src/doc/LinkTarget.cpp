#include "doc/LinkTarget.h"

#include "CoreAliases.h"

#include "core/util/StringUtil.h"

#include <cctype>

namespace micronotes::doc {

bool isRemoteTarget(std::string_view target) {
  return target.starts_with("http://") || target.starts_with("https://");
}

std::string decodeLinkTarget(std::string_view target) {
  const auto hexDigit = [](char c) -> int {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(target.size());
  for(std::size_t i = 0; i < target.size(); ++i) {
    if(target[i] == '%' && i + 2 < target.size()) {
      const int high = hexDigit(target[i + 1]);
      const int low = hexDigit(target[i + 2]);
      // Both digits or neither. A lone `%` is a legal character in a file name,
      // so a malformed escape is kept rather than dropped -- decoding it into
      // something else would turn a link that works into one that does not.
      if(high >= 0 && low >= 0) {
        out.push_back(static_cast<char>(high * 16 + low));
        i += 2;
        continue;
      }
    }
    // CommonMark's other escape. A backslash only escapes ASCII punctuation;
    // before anything else it is itself a character, and on this platform a
    // legal one in a file name.
    if(target[i] == '\\' && i + 1 < target.size() &&
       std::ispunct(static_cast<unsigned char>(target[i + 1])) != 0) {
      out.push_back(target[i + 1]);
      ++i;
      continue;
    }
    out.push_back(target[i]);
  }
  return out;
}

std::string headingAnchor(std::string_view value) {
  std::string out;
  bool pendingDash = false;
  for(const unsigned char c : value) {
    if(std::isalnum(c)) {
      if(pendingDash && !out.empty()) out.push_back('-');
      out.push_back(util::toLowerAscii(static_cast<char>(c)));
      pendingDash = false;
    } else if(!out.empty()) {
      pendingDash = true;
    }
  }
  return out;
}
}

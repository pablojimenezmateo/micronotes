#include "core/util/StringUtil.h"

#include <algorithm>

namespace microcore::util {

std::string toLowerAscii(std::string_view value) {
  std::string out(value);
  toLowerAsciiInPlace(out);
  return out;
}

void toLowerAsciiInPlace(std::string& value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](char c) { return toLowerAscii(c); });
}

bool equalsIgnoringAsciiCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(),
                    [](char x, char y) { return toLowerAscii(x) == toLowerAscii(y); });
}

}

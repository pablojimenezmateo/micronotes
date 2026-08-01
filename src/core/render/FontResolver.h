#pragma once

#include <string>

namespace microcore::render {

// Which face to look for. Kept as a small value rather than a font family
// string, because callers want "the bold sans face this system has", not a
// specific font that may or may not be installed.
struct FontRequest {
  bool monospace = false;
  bool bold = false;
  bool italic = false;
};

// Absolute path to a usable font file, or an empty string if nothing was found.
//
// This replaces four hardcoded paths under
// /usr/share/fonts/truetype/dejavu/. Hardcoding had two costs. The obvious one
// is that a system without DejaVu at exactly that path rendered no text at all
// -- not a fallback face, nothing, because TTF_OpenFont simply returned null
// and every draw call silently did nothing. The subtler one is that pinning
// DejaVu means the app never picks up the system's configured UI font, which is
// most of why it looked out of place next to native applications.
//
// Resolution order:
//   1. fontconfig, when compiled in. This is what every other application on
//      the machine uses, so it returns the face the user actually configured.
//   2. A scan of the standard font directories for a known-good family.
// Results are memoized: resolution is not cheap and the answer cannot change
// within a run.
std::string resolveFontFile(const FontRequest& request);

// True when fontconfig was compiled in. Reported by the startup banner so a
// packaging mistake is visible rather than silently degrading font choice.
bool hasFontconfig();

}

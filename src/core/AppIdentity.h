#pragma once

// The only thing in src/core/ that differs between the applications that vendor
// it. Everything else here is byte-identical across micronotes and microagenda
// and is checked as such by tools/sync-core.sh --check.
//
// The host application defines MICROCORE_APP_NAME from CMake. Keeping it a
// macro rather than a runtime string means the derived paths below stay
// compile-time constants, so no allocation or lookup happens on the path
// helpers that run per attachment and per startup.

#ifndef MICROCORE_APP_NAME
#error "MICROCORE_APP_NAME must be defined by the host application (see CMakeLists.txt)"
#endif

namespace microcore {

// "micronotes" / "microagenda". Used for the XDG config, cache, and data dirs.
inline constexpr const char* kAppName = MICROCORE_APP_NAME;

// ".micronotes" / ".microagenda": the per-library state directory that lives
// inside whatever root the user opened.
inline constexpr const char* kAppDotDir = "." MICROCORE_APP_NAME;

// ".micronotes/attachments/" -- the prefix that marks a link target as pointing
// at an attachment this app owns rather than at an arbitrary relative file.
inline constexpr const char* kAttachmentsPathPrefix = "." MICROCORE_APP_NAME "/attachments/";

}

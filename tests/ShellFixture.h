#pragma once

#include "TempDir.h"
#include "TestSupport.h"

#include "app/Notes.h"
#include "app/SessionState.h"
#include "app/Shell.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace micronotes::tests {

// A shell with a library on disk and one note open in it.
//
// `openScratchNote` had been written twice, identically, in `FindBarTests.cpp`
// and `StatusBarTests.cpp` -- the two files that arrived together and each
// needed a note to search or to report on. Two copies of a fixture drift the
// same way two copies of anything else do, and the drift is worse here than
// elsewhere: a test that passes against a fixture nobody else uses is a test
// about that fixture.
//
// The directory is owned rather than leaked. The versions this replaces removed
// whatever a previous run left and then created their own, so a suite that
// failed left one temp tree per test behind it.
class ScratchNote {
public:
  ScratchNote(app::UiRuntime& ui, std::string_view name, const std::string& body)
      : dir_(name) {
    std::filesystem::create_directories(dir_.path());
    micronotes::tests::require(app::openLibraryRoot(ui, dir_.path()),
                               "the scratch library did not open");
    app::createNote(ui, "Note");
    ui.editor.setText(body);
  }

  const std::filesystem::path& root() const { return dir_.path(); }

private:
  TempDir dir_;
};

}

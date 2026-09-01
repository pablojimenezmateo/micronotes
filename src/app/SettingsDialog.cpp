#include "app/SettingsDialog.h"

#include "app/Shell.h"

#include "ui/Settings.h"
#include "ui/Theme.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace micronotes::app {
namespace {

// `~` for the home directory, so a library path fits on a settings row.
std::string displayPath(const std::filesystem::path& path) {
  const auto text = path.generic_string();
  const char* home = std::getenv("HOME");
  if(!home || !*home) return text;
  const std::string prefix(home);
  if(text.rfind(prefix, 0) != 0) return text;
  if(text.size() == prefix.size()) return "~";
  if(text[prefix.size()] != '/') return text;
  return "~" + text.substr(prefix.size());
}

}

void openSettings(UiRuntime& ui) {
  ui::Overlay overlay;
  overlay.id = "settings";
  overlay.title = "Settings";
  overlay.filterable = true;
  overlay.placeholder = "Type a setting";
  overlay.hint = "Enter change   Esc close";
  overlay.width = 480.0f;
  overlay.items.push_back({"theme", "Theme",
                           ui::themeMode() == ui::ThemeMode::Dark ? "Dark" : "Light", ui::keysFor(ui::ActionId::ToggleTheme), true, false});
  overlay.items.push_back({"text-size", "Text size", std::string(ui::textSizeLabel(ui::textSize())), "", true, false});
  overlay.items.push_back({"page-width", "Page width", std::string(ui::pageWidthLabel(ui::pageWidth())), "", true, false});
  // Trimmed from the left: a truncated path keeps the half that says which
  // folder this is, not the half every path on the machine shares.
  std::string library = ui.state.hasLibrary() ? displayPath(ui.state.libraryRoot()) : std::string("none");
  if(library.size() > 34) {
    const auto root = ui.state.libraryRoot();
    library = "\xe2\x80\xa6/" + root.parent_path().filename().generic_string() + "/" + root.filename().generic_string();
  }
  overlay.items.push_back({"library", "Library folder", library, "", true, false});
  overlay.items.push_back({"shortcuts", "Keyboard shortcuts...", "", "F1", true, false});
  ui.overlays.open(std::move(overlay));
}

// The values one setting can take. "current" rather than a tick: the vendored
// UI face is not guaranteed a check glyph, and a word cannot render as tofu.
void openSettingsValues(UiRuntime& ui, const std::string& which) {
  ui::Overlay overlay;
  overlay.width = 380.0f;
  overlay.hint = "Enter apply   Esc back";
  const auto mark = [](bool active) { return active ? "current" : ""; };
  if(which == "theme") {
    overlay.id = "settings-theme";
    overlay.title = "Theme";
    overlay.items.push_back({"light", "Light", "", mark(ui::themeMode() == ui::ThemeMode::Light), true, false});
    overlay.items.push_back({"dark", "Dark", "", mark(ui::themeMode() == ui::ThemeMode::Dark), true, false});
  } else if(which == "text-size") {
    overlay.id = "settings-text-size";
    overlay.title = "Text size";
    for(const auto size : {ui::TextSize::Small, ui::TextSize::Medium, ui::TextSize::Large}) {
      overlay.items.push_back({std::string(ui::textSizeName(size)), std::string(ui::textSizeLabel(size)),
                               "", mark(ui::textSize() == size), true, false});
    }
  } else if(which == "page-width") {
    overlay.id = "settings-page-width";
    overlay.title = "Page width";
    for(const auto width : {ui::PageWidth::Narrow, ui::PageWidth::Medium, ui::PageWidth::Wide}) {
      overlay.items.push_back({std::string(ui::pageWidthName(width)), std::string(ui::pageWidthLabel(width)),
                               "", mark(ui::pageWidth() == width), true, false});
    }
  } else {
    return;
  }
  ui.overlays.open(std::move(overlay));
}

}

#pragma once

#include <string>

namespace micronotes::app {

struct UiRuntime;

// The settings dialog: theme, text size, page width, and which folder is open.
//
// Each row opens the list of its own values and that list comes back with the
// settings list under it, so changing two things is not two trips through the
// dialog.
void openSettings(UiRuntime& ui);
void openSettingsValues(UiRuntime& ui, const std::string& which);

}

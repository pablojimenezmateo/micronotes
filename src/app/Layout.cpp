#include "app/Layout.h"

#include "app/Shell.h"

namespace micronotes::app {

ShellLayout shellLayout(UiRuntime& ui, int width, int height) {
  auto inputs = ui.state.workspace().layoutInputs(
    static_cast<float>(width), static_cast<float>(height), ui.layoutMode);
  // One tab is still a tab: hiding the strip until a second opens would make
  // the page jump down the moment it did.
  inputs.tabStripVisible = !ui.state.workspace().tabs.empty();
  const ShellLayout layout = ui::computeShellLayout(inputs);
  ui.layoutMode = layout.mode;
  return layout;
}

}

#include "ui/Actions.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace micronotes::ui {
namespace {

using S = ActionSection;

// The registry. One row per ActionId, in the order the shortcut list prints
// them. ArchitectureTests checks that every id appears exactly once, so an id
// added to the enum and forgotten here fails the build rather than showing up
// as a blank row.
constexpr std::array<ActionSpec, static_cast<std::size_t>(ActionId::Count)> kSpecs {{
  {ActionId::GoToNote,        "jump",           "Go to note...",                   "Ctrl+P",       "",              S::Navigation, false, true},
  {ActionId::CommandPalette,  "command-palette","Commands...",                     "Ctrl+Shift+P", "",              S::Navigation, false, false},
  {ActionId::FindInNote,      "find",           "Find in this note",               "Ctrl+F",       "",              S::Navigation, true,  true},
  {ActionId::SearchAllNotes,  "search",         "Search every note",               "Ctrl+Shift+F", "",              S::Navigation, false, true},
  {ActionId::Shortcuts,       "shortcuts",      "Keyboard shortcuts...",           "F1",           "",              S::Navigation, false, true},
  {ActionId::Settings,        "settings",       "Settings...",                     "Ctrl+,",       "",              S::Navigation, false, true},

  {ActionId::NewNote,         "new-note",       "New note",                        "Ctrl+N",       "",              S::Notes, false, true},
  {ActionId::NewFolder,       "new-folder",     "New notebook",                    "",             "",              S::Notes, false, true},
  {ActionId::Save,            "save",           "Save note",                       "Ctrl+S",       "",              S::Notes, true,  true},
  {ActionId::RenameNote,      "rename",         "Rename note...",                  "F2",           "",              S::Notes, true,  true},
  {ActionId::SetNoteIcon,     "icon",           "Set note icon...",                "",             "",              S::Notes, true,  true},
  {ActionId::EditTags,        "tags",           "Edit tags...",                    "Ctrl+T",       "",              S::Notes, true,  true},
  {ActionId::ToggleFavorite,  "favorite",       "Toggle favorite",                 "",             "",              S::Notes, true,  true},
  {ActionId::MoveNote,        "move-note",      "Move note to notebook...",        "",             "",              S::Notes, true,  true},
  {ActionId::MoveBlocks,      "move-blocks",    "Move selected blocks to note...", "",             "",              S::Notes, true,  true},
  {ActionId::DeleteNote,      "delete-note",    "Delete note...",                  "",             "",              S::Notes, true,  true},
  {ActionId::RenameFolder,    "rename-folder",  "Rename notebook...",              "",             "",              S::Notes, false, true},
  {ActionId::DeleteFolder,    "delete-folder",  "Delete notebook...",              "",             "",              S::Notes, false, true},
  {ActionId::RestoreFromTrash,"restore",        "Restore from trash...",           "",             "",              S::Notes, false, true},
  {ActionId::RefreshLibrary,  "refresh",        "Refresh library",                 "Ctrl+R",       "",              S::Notes, false, true},

  // The editing verbs act on what is selected, and opening the palette takes
  // the selection's focus away, so they are shortcuts and help rows only.
  {ActionId::Bold,            "bold",           "Bold",                            "Ctrl+B",       "",              S::Writing, true, false},
  {ActionId::Italic,          "italic",         "Italic",                          "Ctrl+I",       "",              S::Writing, true, false},
  {ActionId::Code,            "code",           "Inline code",                     "Ctrl+E",       "",              S::Writing, true, false},
  {ActionId::Link,            "link",           "Link the selection",              "Ctrl+K",       "",              S::Writing, true, false},
  {ActionId::Undo,            "undo",           "Undo",                            "Ctrl+Z",       "",              S::Writing, true, false},
  {ActionId::Redo,            "redo",           "Redo",                            "Ctrl+Y",       "",              S::Writing, true, false},
  {ActionId::ToggleTask,      "toggle-task",    "Tick or untick a task",           "Ctrl+Enter",   "",              S::Writing, true, false},

  {ActionId::DuplicateBlock,  "duplicate-block","Duplicate the block",             "Ctrl+D",       "",              S::Blocks, true, false},
  {ActionId::DeleteBlock,     "delete-block",   "Delete the block",                "Ctrl+Shift+D", "",              S::Blocks, true, false},
  {ActionId::MoveBlockUp,     "move-block-up",  "Move the block up",               "Alt+Up",       "",              S::Blocks, true, false},
  {ActionId::MoveBlockDown,   "move-block-down","Move the block down",             "Alt+Down",     "",              S::Blocks, true, false},
  {ActionId::InsertBlock,     "insert-block",   "Insert a block",                  "",             "/",             S::Blocks, true, false},
  {ActionId::TurnInto,        "turn-into",      "Turn the block into...",          "",             "Ctrl+Shift+1-9",S::Blocks, true, false},
  {ActionId::Fold,            "fold",           "Fold or unfold section",          "Ctrl+.",       "",              S::Blocks, true, true},

  {ActionId::PaneLive,        "pane-live",      "View: live",                      "Ctrl+1",       "",              S::View, false, true},
  {ActionId::PaneRaw,         "pane-raw",       "View: raw Markdown",              "Ctrl+2",       "",              S::View, false, true},
  {ActionId::PaneReading,     "pane-reading",   "View: reading",                   "Ctrl+3",       "",              S::View, false, true},
  {ActionId::PaneSplit,       "pane-split",     "View: split",                     "Ctrl+4",       "",              S::View, false, true},
  {ActionId::CyclePane,       "cycle-pane",     "Cycle the four views",            "Ctrl+L",       "",              S::View, false, true},
  {ActionId::ToggleTheme,     "theme",          "Toggle light and dark",           "Ctrl+Shift+L", "",              S::View, false, true},
  {ActionId::ToggleSidebar,   "toggle-sidebar", "Show or hide the sidebar",        "Ctrl+Alt+Left","",              S::View, false, true},
  {ActionId::ToggleNoteList,  "toggle-notes",   "Show or hide the note list",      "Ctrl+Alt+Down","",              S::View, false, true},
  {ActionId::ToggleRightPanel,"toggle-right",   "Show or hide the outline panel",  "Ctrl+Alt+Right","",             S::View, false, true},
  {ActionId::NextTab,         "next-tab",       "Next tab",                        "Ctrl+Tab",     "",              S::View, false, true},
  {ActionId::PreviousTab,     "previous-tab",   "Previous tab",                    "Ctrl+Shift+Tab","",             S::View, false, true},
  {ActionId::CloseTab,        "close-tab",      "Close this tab",                  "Ctrl+W",       "",              S::View, true,  true},
  {ActionId::OpenInNewTab,    "new-tab",        "Open a note in a new tab...",     "Ctrl+Shift+T", "",              S::View, false, true},
  {ActionId::PinTab,          "pin-tab",        "Pin or unpin this tab",           "",             "",              S::View, true,  true},
  {ActionId::CycleRightPanel, "cycle-right",    "Outline, links or tags",      "Ctrl+Alt+Up",  "",              S::View, false, true},
}};

// Named keys, so a chord can say "Enter" rather than a keycode. Only the keys
// the registry actually binds are listed; anything else is a single character.
struct NamedKey {
  std::string_view name;
  SDL_Keycode key;
};

constexpr NamedKey kNamedKeys[] = {
  {"Enter", SDLK_RETURN},
  {"Tab", SDLK_TAB},
  {"Space", SDLK_SPACE},
  {"Escape", SDLK_ESCAPE},
  {"Backspace", SDLK_BACKSPACE},
  {"Delete", SDLK_DELETE},
  {"Left", SDLK_LEFT},
  {"Right", SDLK_RIGHT},
  {"Up", SDLK_UP},
  {"Down", SDLK_DOWN},
  {"Home", SDLK_HOME},
  {"End", SDLK_END},
  {"PageUp", SDLK_PAGEUP},
  {"PageDown", SDLK_PAGEDOWN},
  {"F1", SDLK_F1},
  {"F2", SDLK_F2},
};

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

}

std::string_view sectionLabel(ActionSection section) {
  switch(section) {
    case ActionSection::Navigation: return "Getting around";
    case ActionSection::Notes: return "Notes and notebooks";
    case ActionSection::Writing: return "Writing";
    case ActionSection::Blocks: return "Blocks";
    case ActionSection::View: return "View";
    case ActionSection::Count: break;
  }
  return "";
}

std::optional<KeyChord> parseKeyChord(std::string_view text) {
  if(text.empty()) return std::nullopt;
  KeyChord chord;
  std::string_view rest = text;
  // Modifiers come first and in any order; the last token is the key itself.
  // A trailing "+" is a chord on the plus key, not a missing token, so the
  // search stops before the final character.
  while(true) {
    const auto plus = rest.substr(0, rest.size() - 1).find('+');
    if(plus == std::string_view::npos) break;
    const std::string_view token = rest.substr(0, plus);
    if(equalsIgnoringCase(token, "Ctrl")) chord.ctrl = true;
    else if(equalsIgnoringCase(token, "Shift")) chord.shift = true;
    else if(equalsIgnoringCase(token, "Alt")) chord.alt = true;
    else return std::nullopt;
    rest = rest.substr(plus + 1);
  }
  if(rest.empty()) return std::nullopt;
  for(const auto& named : kNamedKeys) {
    if(equalsIgnoringCase(rest, named.name)) {
      chord.key = named.key;
      return chord;
    }
  }
  if(rest.size() != 1) return std::nullopt;
  // Keycodes are the unshifted character, so "Ctrl+P" and "Ctrl+p" are one
  // chord and both spell the keycode 'p'.
  chord.key = static_cast<SDL_Keycode>(std::tolower(static_cast<unsigned char>(rest[0])));
  return chord;
}

std::string formatKeyChord(const KeyChord& chord) {
  if(chord.key == SDLK_UNKNOWN) return "";
  std::string out;
  if(chord.ctrl) out += "Ctrl+";
  if(chord.shift) out += "Shift+";
  if(chord.alt) out += "Alt+";
  for(const auto& named : kNamedKeys) {
    if(named.key == chord.key) {
      out += named.name;
      return out;
    }
  }
  // Printed uppercase because that is how a keyboard is labelled, even though
  // the keycode is the lowercase character.
  out += static_cast<char>(std::toupper(static_cast<unsigned char>(static_cast<int>(chord.key))));
  return out;
}

constexpr HelpRow kHelpRows[] = {
  {"Up, Down", "Walk the sidebar", S::Navigation},
  {"Right, Left", "Open or close a notebook", S::Navigation},
  {"Esc", "Close a dialog, or clear the search", S::Navigation},

  {"Ctrl+Left, Ctrl+Right", "Move by word", S::Writing},
  {"Ctrl+Home, Ctrl+End", "Start and end of the note", S::Writing},
  {"PageUp, PageDown", "Move by a screenful", S::Writing},
  {"Tab, Shift+Tab", "Indent, outdent a list item", S::Writing},
  {"Enter", "Continue the list, or leave it when empty", S::Writing},
  {"Ctrl+A", "Select everything", S::Writing},
  {"Ctrl+C, Ctrl+X", "Copy, cut", S::Writing},
  {"Ctrl+V, Ctrl+Shift+V", "Paste, paste an image as an attachment", S::Writing},

  {"Esc", "Select the block, again to go back", S::Blocks},
  {"Shift+Up, Shift+Down", "Extend the block selection", S::Blocks},
  {"Ctrl+Shift+0..3", "Turn into text or a heading", S::Blocks},
  {"Ctrl+Shift+7, 8, 9", "Turn into a numbered item, bullet, task", S::Blocks},
};

std::span<const HelpRow> helpRows() {
  return {kHelpRows, sizeof(kHelpRows) / sizeof(kHelpRows[0])};
}

std::span<const ActionSpec> actionSpecs() {
  return {kSpecs.data(), kSpecs.size()};
}

const ActionSpec* findAction(ActionId id) {
  for(const auto& spec : kSpecs) {
    if(spec.id == id) return &spec;
  }
  return nullptr;
}

const ActionSpec* findAction(std::string_view name) {
  if(name.empty()) return nullptr;
  for(const auto& spec : kSpecs) {
    if(spec.name == name) return &spec;
  }
  return nullptr;
}

const ActionSpec* findActionForChord(const KeyChord& chord) {
  for(const auto& spec : kSpecs) {
    const auto bound = parseKeyChord(spec.chord);
    if(bound && *bound == chord) return &spec;
  }
  return nullptr;
}

std::string keysFor(ActionId id) {
  const auto* spec = findAction(id);
  return spec ? acceleratorText(*spec) : std::string();
}

std::string acceleratorText(const ActionSpec& spec) {
  if(!spec.keyHint.empty()) return std::string(spec.keyHint);
  const auto chord = parseKeyChord(spec.chord);
  return chord ? formatKeyChord(*chord) : std::string();
}

}

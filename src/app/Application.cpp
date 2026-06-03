#include "app/Application.h"

#include "attachments/AttachmentService.h"
#include "editor/MarkdownEditor.h"
#include "markdown/MarkdownParser.h"
#include "perf/Perf.h"
#include "platform/PathUtils.h"
#include "ui/AppState.h"

#include <SDL3/SDL.h>
#if MICRONOTES_HAS_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif
#if MICRONOTES_HAS_SDL3_TTF
#include <SDL3_ttf/SDL_ttf.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <sys/types.h>
#include <unistd.h>

namespace micronotes::app {
namespace {

constexpr SDL_Color kText {238, 241, 244, 255};
constexpr SDL_Color kMuted {157, 165, 174, 255};
constexpr SDL_Color kDim {97, 106, 117, 255};
constexpr SDL_Color kAccent {114, 230, 198, 255};
constexpr SDL_Color kAccentDim {50, 124, 111, 255};
constexpr SDL_Color kAccentSoft {25, 48, 44, 255};
constexpr SDL_Color kWarn {239, 145, 104, 255};

constexpr SDL_Color kAppBg {9, 11, 14, 255};
constexpr SDL_Color kSidebarBg {16, 18, 23, 255};
constexpr SDL_Color kNotesBg {19, 22, 27, 255};
constexpr SDL_Color kEditorBg {12, 14, 18, 255};
constexpr SDL_Color kViewerBg {13, 15, 19, 255};
constexpr SDL_Color kHeaderBg {18, 21, 27, 255};
constexpr SDL_Color kSurface {22, 25, 31, 255};
constexpr SDL_Color kSurfaceElevated {28, 32, 39, 255};
constexpr SDL_Color kDivider {43, 48, 57, 255};
constexpr SDL_Color kHairline {30, 34, 41, 255};
constexpr SDL_Color kInputBg {11, 13, 17, 255};
constexpr SDL_Color kHoverBg {32, 38, 47, 255};
constexpr SDL_Color kSelectedBg {31, 46, 50, 255};
constexpr SDL_Color kButtonBg {25, 29, 36, 255};
constexpr SDL_Color kButtonHover {40, 47, 57, 255};
constexpr SDL_Color kButtonActive {31, 62, 56, 255};

enum class UiAction {
  Refresh,
  NewNote,
  RenameNote,
  DeleteNote,
  Save,
  Tags,
  PaneEditor,
  PaneViewer,
  PaneSplit
};

enum class FocusArea {
  Folders,
  Notes,
  Editor,
  Search,
  Find,
  Viewer,
  TagEditor,
  RenameNote,
  RenameFolder
};

enum class ScrollDragTarget {
  None,
  Editor,
  Viewer
};

enum class CursorKind {
  Default,
  Text,
  Pointer,
  ResizeHorizontal,
  ResizeVertical
};

static const char* focusName(FocusArea focus) {
  switch(focus) {
    case FocusArea::Folders: return "Folders";
    case FocusArea::Notes: return "Notes";
    case FocusArea::Editor: return "Editor";
    case FocusArea::Search: return "Search";
    case FocusArea::Find: return "Find";
    case FocusArea::Viewer: return "Viewer";
    case FocusArea::TagEditor: return "TagEditor";
    case FocusArea::RenameNote: return "RenameNote";
    case FocusArea::RenameFolder: return "RenameFolder";
  }
  return "Unknown";
}

static bool inputDebugEnabled() {
  static const bool enabled = [] {
    const char* value = std::getenv("MICRONOTES_DEBUG_INPUT");
    return value && *value && std::string_view(value) != "0";
  }();
  return enabled;
}

struct Rect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
};

static bool contains(const Rect& rect, float x, float y) {
  return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

static SDL_FRect sdlRect(const Rect& rect) {
  return {rect.x, rect.y, rect.w, rect.h};
}

static SDL_Rect clipRect(const Rect& rect) {
  return {
    static_cast<int>(std::floor(rect.x)),
    static_cast<int>(std::floor(rect.y)),
    static_cast<int>(std::ceil(rect.w)),
    static_cast<int>(std::ceil(rect.h)),
  };
}

class ClipGuard {
public:
  ClipGuard(SDL_Renderer* renderer, Rect rect) : renderer_(renderer) {
    clip_ = clipRect(rect);
    SDL_SetRenderClipRect(renderer_, &clip_);
  }

  ~ClipGuard() {
    SDL_SetRenderClipRect(renderer_, nullptr);
  }

private:
  SDL_Renderer* renderer_ = nullptr;
  SDL_Rect clip_ {};
};

struct LinkRegion {
  Rect rect;
  std::string target;
};

struct ButtonRegion {
  Rect rect;
  UiAction action = UiAction::Refresh;
};

struct AppLayout {
  Rect sidebar;
  Rect notes;
  Rect content;
  Rect status;
};

struct SystemCursors {
  SDL_Cursor* defaultCursor = nullptr;
  SDL_Cursor* text = nullptr;
  SDL_Cursor* pointer = nullptr;
  SDL_Cursor* resizeHorizontal = nullptr;
  SDL_Cursor* resizeVertical = nullptr;
  CursorKind active = CursorKind::Default;

  bool init() {
    defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    text = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    pointer = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    resizeHorizontal = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
    resizeVertical = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NS_RESIZE);
    if(!defaultCursor || !text || !pointer || !resizeHorizontal || !resizeVertical) return false;
    SDL_SetCursor(defaultCursor);
    return true;
  }

  void destroy() {
    if(defaultCursor) SDL_DestroyCursor(defaultCursor);
    if(text) SDL_DestroyCursor(text);
    if(pointer) SDL_DestroyCursor(pointer);
    if(resizeHorizontal) SDL_DestroyCursor(resizeHorizontal);
    if(resizeVertical) SDL_DestroyCursor(resizeVertical);
    defaultCursor = nullptr;
    text = nullptr;
    pointer = nullptr;
    resizeHorizontal = nullptr;
    resizeVertical = nullptr;
  }

  SDL_Cursor* cursor(CursorKind kind) const {
    switch(kind) {
      case CursorKind::Text: return text;
      case CursorKind::Pointer: return pointer;
      case CursorKind::ResizeHorizontal: return resizeHorizontal;
      case CursorKind::ResizeVertical: return resizeVertical;
      case CursorKind::Default:
      default: return defaultCursor;
    }
  }

  void apply(CursorKind kind) {
    if(kind == active) return;
    if(SDL_Cursor* next = cursor(kind)) {
      SDL_SetCursor(next);
      active = kind;
    }
  }
};

static void fill(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderFillRect(renderer, &out);
}

static void stroke(SDL_Renderer* renderer, Rect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  auto out = sdlRect(rect);
  SDL_RenderRect(renderer, &out);
}

static void hLine(SDL_Renderer* renderer, float x1, float x2, float y, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderLine(renderer, x1, y, x2, y);
}

static void drawSurface(SDL_Renderer* renderer, Rect rect, SDL_Color fillColor = kSurface, SDL_Color borderColor = kHairline) {
  fill(renderer, rect, fillColor);
  stroke(renderer, rect, borderColor);
  hLine(renderer, rect.x + 1, rect.x + rect.w - 2, rect.y + 1, SDL_Color {54, 60, 70, 120});
}

static void drawSelection(SDL_Renderer* renderer, Rect row, bool selected, bool hot) {
  if(selected) {
    fill(renderer, row, kSelectedBg);
    fill(renderer, {row.x, row.y, 3, row.h}, kAccent);
    stroke(renderer, row, kAccentDim);
  } else if(hot) {
    fill(renderer, row, kHoverBg);
    stroke(renderer, row, kHairline);
  }
}

static std::string trimTitle(std::string_view text) {
  std::istringstream lines {std::string(text)};
  std::string line;
  while(std::getline(lines, line)) {
    while(!line.empty() && (line.front() == '#' || std::isspace(static_cast<unsigned char>(line.front())))) {
      line.erase(line.begin());
    }
    if(!line.empty()) return line.substr(0, 60);
  }
  return "Untitled";
}

static std::vector<std::string> splitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string current;
  for(const char c : text) {
    if(c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  lines.push_back(current);
  return lines;
}

static std::string ellipsize(std::string text, std::size_t limit) {
  if(text.size() <= limit) return text;
  if(limit <= 3) return text.substr(0, limit);
  return text.substr(0, limit - 3) + "...";
}

static AppLayout computeLayout(const ui::ShellModel& shell, int width, int height) {
  const float statusH = 28.0f;
  const float usableW = static_cast<float>(std::max(width, 760));
  float sideW = static_cast<float>(shell.sidebarWidth);
  float notesW = static_cast<float>(shell.noteListWidth);
  if(usableW < 1000.0f) {
    sideW = 190.0f;
    notesW = 240.0f;
  }
  sideW = std::clamp(sideW, 170.0f, std::max(170.0f, usableW * 0.28f));
  notesW = std::clamp(notesW, 220.0f, std::max(220.0f, usableW * 0.34f));
  const float contentMin = 320.0f;
  if(sideW + notesW + contentMin > usableW) {
    notesW = std::max(190.0f, usableW - sideW - contentMin);
  }
  if(sideW + notesW + contentMin > usableW) {
    sideW = std::max(150.0f, usableW - notesW - contentMin);
  }

  return {
    {0, 0, sideW, static_cast<float>(height) - statusH},
    {sideW, 0, notesW, static_cast<float>(height) - statusH},
    {sideW + notesW, 0, static_cast<float>(width) - sideW - notesW, static_cast<float>(height) - statusH},
    {0, static_cast<float>(height) - statusH, static_cast<float>(width), statusH},
  };
}

static bool isRemoteTarget(std::string_view target) {
  return target.starts_with("http://") || target.starts_with("https://");
}

static std::string fileNameForMime(std::string_view mime) {
  if(mime == "image/png") return "clipboard.png";
  if(mime == "image/jpeg" || mime == "image/jpg") return "clipboard.jpg";
  if(mime == "image/bmp") return "clipboard.bmp";
  if(mime == "image/webp") return "clipboard.webp";
  return "clipboard-image";
}

static std::vector<std::string> splitTags(std::string_view value) {
  std::vector<std::string> tags;
  std::set<std::string> seen;
  std::istringstream in {std::string(value)};
  std::string tag;
  while(in >> tag) {
    if(!tag.empty() && tag.front() == '#') tag.erase(tag.begin());
    if(tag.empty() || seen.contains(tag)) continue;
    seen.insert(tag);
    tags.push_back(tag);
  }
  return tags;
}

static std::string joinTags(const std::vector<std::string>& tags) {
  std::string out;
  for(const auto& tag : tags) {
    if(!out.empty()) out += " ";
    out += tag;
  }
  return out;
}

static bool spawnDetached(const std::vector<std::string>& command) {
  if(command.empty()) return false;
  const pid_t pid = fork();
  if(pid < 0) return false;
  if(pid == 0) {
    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for(const auto& part : command) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  return true;
}

class TextRenderer {
public:
  explicit TextRenderer(SDL_Renderer* renderer) : renderer_(renderer) {
#if MICRONOTES_HAS_SDL3_TTF
    ttfReady_ = TTF_Init();
    if(ttfReady_) {
      regular_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 15.0f);
      mono_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 15.0f);
      heading_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 20.0f);
      bold_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15.0f);
      italic_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf", 15.0f);
      boldItalic_ = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf", 15.0f);
      if(!regular_) regular_ = mono_;
      if(!mono_) mono_ = regular_;
      if(!heading_) heading_ = regular_;
      if(!bold_) bold_ = regular_;
      if(!italic_) italic_ = regular_;
      if(!boldItalic_) boldItalic_ = bold_;
    }
#endif
  }

  ~TextRenderer() {
    clear();
#if MICRONOTES_HAS_SDL3_TTF
    if(boldItalic_ && boldItalic_ != bold_ && boldItalic_ != italic_ && boldItalic_ != regular_ && boldItalic_ != mono_ && boldItalic_ != heading_) TTF_CloseFont(boldItalic_);
    if(italic_ && italic_ != regular_ && italic_ != mono_ && italic_ != heading_) TTF_CloseFont(italic_);
    if(bold_ && bold_ != regular_ && bold_ != mono_ && bold_ != heading_) TTF_CloseFont(bold_);
    if(heading_ && heading_ != regular_ && heading_ != mono_) TTF_CloseFont(heading_);
    if(mono_ && mono_ != regular_) TTF_CloseFont(mono_);
    if(regular_) TTF_CloseFont(regular_);
    if(ttfReady_) TTF_Quit();
#endif
  }

  int lineHeight(bool heading = false) const {
#if MICRONOTES_HAS_SDL3_TTF
    TTF_Font* font = heading ? heading_ : regular_;
    if(font) return TTF_GetFontHeight(font) + 2;
#else
    (void)heading;
#endif
    return 16;
  }

  int width(std::string_view text, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) const {
    if(text.empty()) return 0;
#if MICRONOTES_HAS_SDL3_TTF
    TTF_Font* font = fontFor(heading, mono, strong, emphasis);
    if(ttfReady_ && font) {
      int w = 0;
      int h = 0;
      if(TTF_GetStringSize(font, text.data(), text.size(), &w, &h)) return w;
    }
#else
    (void)heading;
    (void)mono;
#endif
    return static_cast<int>(text.size() * 8);
  }

  void clear() {
    for(auto& [_, texture] : cache_) SDL_DestroyTexture(texture.texture);
    cache_.clear();
  }

  void draw(std::string_view text, float x, float y, SDL_Color color = kText, bool heading = false, bool mono = false, bool strong = false, bool emphasis = false) {
    if(text.empty()) return;
    const std::string value(text);
    x = std::round(x);
    y = std::round(y);
#if MICRONOTES_HAS_SDL3_TTF
    TTF_Font* font = fontFor(heading, mono, strong, emphasis);
    if(ttfReady_ && font) {
      if(cache_.size() > 4096) clear();
      const auto key = cacheKey(value, color, heading, mono, strong, emphasis);
      auto found = cache_.find(key);
      if(found == cache_.end()) {
        SDL_Surface* surface = TTF_RenderText_Blended(font, value.c_str(), value.size(), color);
        if(!surface) return;
        SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
        CachedText cached {texture, surface->w, surface->h};
        SDL_DestroySurface(surface);
        if(!texture) return;
        found = cache_.emplace(key, cached).first;
      }
      SDL_FRect dst {x, y, static_cast<float>(found->second.w), static_cast<float>(found->second.h)};
      SDL_RenderTexture(renderer_, found->second.texture, nullptr, &dst);
      return;
    }
#else
    (void)heading;
    (void)mono;
    (void)strong;
    (void)emphasis;
#endif
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer_, x, y, value.c_str());
  }

private:
  struct CachedText {
    SDL_Texture* texture = nullptr;
    int w = 0;
    int h = 0;
  };

  static std::string cacheKey(const std::string& text, SDL_Color color, bool heading, bool mono, bool strong, bool emphasis) {
    return std::to_string(color.r) + ":" + std::to_string(color.g) + ":" + std::to_string(color.b) + ":" +
      (heading ? "h:" : mono ? "m:" : strong && emphasis ? "bi:" : strong ? "b:" : emphasis ? "i:" : "r:") + text;
  }

#if MICRONOTES_HAS_SDL3_TTF
  TTF_Font* fontFor(bool heading, bool mono, bool strong, bool emphasis) const {
    if(heading) return heading_;
    if(mono) return mono_;
    if(strong && emphasis) return boldItalic_;
    if(strong) return bold_;
    if(emphasis) return italic_;
    return regular_;
  }
#endif

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, CachedText> cache_;
#if MICRONOTES_HAS_SDL3_TTF
  bool ttfReady_ = false;
  TTF_Font* regular_ = nullptr;
  TTF_Font* mono_ = nullptr;
  TTF_Font* heading_ = nullptr;
  TTF_Font* bold_ = nullptr;
  TTF_Font* italic_ = nullptr;
  TTF_Font* boldItalic_ = nullptr;
#endif
};

class ImageCache {
public:
  explicit ImageCache(SDL_Renderer* renderer) : renderer_(renderer) {}

  ~ImageCache() {
    clear();
  }

  void clear() {
    for(auto& [_, texture] : cache_) SDL_DestroyTexture(texture.texture);
    cache_.clear();
  }

  SDL_Texture* load(const std::filesystem::path& path, float& width, float& height) {
#if MICRONOTES_HAS_SDL3_IMAGE
    const auto key = path.string();
    auto found = cache_.find(key);
    if(found == cache_.end()) {
      SDL_Texture* texture = IMG_LoadTexture(renderer_, key.c_str());
      if(!texture) return nullptr;
      CachedImage image {texture, 0.0f, 0.0f};
      SDL_GetTextureSize(texture, &image.w, &image.h);
      if(cache_.size() > 512) clear();
      found = cache_.emplace(key, image).first;
    }
    width = found->second.w;
    height = found->second.h;
    return found->second.texture;
#else
    (void)path;
    (void)width;
    (void)height;
    return nullptr;
#endif
  }

private:
  struct CachedImage {
    SDL_Texture* texture = nullptr;
    float w = 0;
    float h = 0;
  };

  SDL_Renderer* renderer_ = nullptr;
  std::map<std::string, CachedImage> cache_;
};

struct UiRuntime {
  ui::AppState state;
  editor::MarkdownEditor editor;
  markdown::MarkdownParser parser;
  std::string cachedMarkdownSource;
  std::optional<markdown::Document> cachedMarkdownDocument;
  FocusArea focus = FocusArea::Editor;
  std::string loadedNoteId;
  std::string searchDraft;
  library::SearchScope searchScope = library::SearchScope::All;
  std::string findDraft;
  std::string tagDraft;
  std::string renameDraft;
  std::string folderRenameDraft;
  std::string status;
  std::vector<LinkRegion> linkRegions;
  std::map<std::string, int> viewerAnchors;
  std::vector<ButtonRegion> buttonRegions;
  int noteCursor = 0;
  int folderCursor = 0;
  int editorScroll = 0;
  int viewerScroll = 0;
  Uint64 lastRefresh = 0;
  float mouseX = -1;
  float mouseY = -1;
  bool noteMenuOpen = false;
  float noteMenuX = 0;
  float noteMenuY = 0;
  std::string noteMenuId;
  bool folderMenuOpen = false;
  float folderMenuX = 0;
  float folderMenuY = 0;
  std::filesystem::path folderMenuPath;
  bool creatingFolder = false;
  bool draggingNote = false;
  std::string draggingNoteId;
  ScrollDragTarget scrollDragTarget = ScrollDragTarget::None;
  float scrollDragOffsetY = 0.0f;
  Rect searchScopeToggle;
  bool resizingSidebar = false;
  bool resizingNotes = false;
  bool selectingEditorText = false;
  std::size_t editorSelectionAnchor = 0;
  Uint64 lastEditorClick = 0;
  int editorClickCount = 0;
  bool inputAllSelected = false;
  bool revealEditorCursor = true;
  Uint64 lastEdit = 0;
  Uint64 lastAutosaveAttempt = 0;
};

static void saveCurrent(UiRuntime& ui, bool quiet = false);
static void updateFindStatus(UiRuntime& ui);

static const markdown::Document& previewDocument(UiRuntime& ui) {
  const auto& source = ui.editor.text();
  if(!ui.cachedMarkdownDocument || ui.cachedMarkdownSource != source) {
    ui.cachedMarkdownSource = source;
    ui.cachedMarkdownDocument = ui.parser.parse(ui.cachedMarkdownSource);
  }
  return *ui.cachedMarkdownDocument;
}

static void markEdited(UiRuntime& ui) {
  ui.lastEdit = SDL_GetTicks();
}

static bool setClipboardText(std::string_view value) {
  const std::string text {value};
  SDL_ClearError();
  const bool clipboardOk = SDL_SetClipboardText(text.c_str());
  const std::string clipboardError = SDL_GetError();
  SDL_ClearError();
  SDL_SetPrimarySelectionText(text.c_str());
  const bool clipboardHasText = SDL_HasClipboardText();
  const bool primaryHasText = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard set"
              << " bytes=" << text.size()
              << " clipboard_ok=" << clipboardOk
              << " clipboard_has_text=" << clipboardHasText
              << " primary_has_text=" << primaryHasText;
    if(!clipboardOk) std::cerr << " error=\"" << clipboardError << "\"";
    std::cerr << "\n";
  }
  return clipboardOk;
}

static bool publishEditorPrimarySelection(UiRuntime& ui) {
  if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
    const auto selected = ui.editor.selectedText();
    return SDL_SetPrimarySelectionText(selected.c_str());
  }
  return false;
}

static void selectNoteAt(UiRuntime& ui, int index) {
  auto notes = ui.state.currentNotes();
  if(notes.empty()) return;
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty()) saveCurrent(ui);
  index = std::clamp(index, 0, static_cast<int>(notes.size()) - 1);
  ui.noteCursor = index;
  ui.state.selectNote(notes[static_cast<std::size_t>(index)].id);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    ui.editor.setText(note->body);
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = "Loaded " + note->metadata.title;
  }
}

static void selectNoteById(UiRuntime& ui, const std::string& noteId) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty()) saveCurrent(ui);
  ui.state.selectNote(noteId);
  if(auto note = ui.state.selectedNote()) {
    ui.loadedNoteId = note->metadata.id;
    ui.editor.setText(note->body);
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = false;
    ui.status = "Loaded " + note->metadata.title;
  }
}

static void loadSelectedIntoEditor(UiRuntime& ui) {
  if(auto note = ui.state.selectedNote()) {
    if(ui.loadedNoteId != note->metadata.id || !ui.editor.dirty()) {
      ui.loadedNoteId = note->metadata.id;
      ui.editor.setText(note->body);
    }
  }
}

static void createNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Start with --library <path> before creating notes";
    return;
  }
  if(ui.editor.dirty()) saveCurrent(ui);
  const auto folder = ui.state.selection().folder;
  if(auto created = ui.state.createNote("Untitled", folder, "# Untitled\n\n")) {
    ui.loadedNoteId = created->id;
    ui.editor.setText("# Untitled\n\n");
    ui.editorScroll = 0;
    ui.viewerScroll = 0;
    ui.revealEditorCursor = true;
    ui.focus = FocusArea::Editor;
    ui.status = "Created " + created->title;
  }
}

static void createNoteInFolder(UiRuntime& ui, const std::filesystem::path& folder) {
  const auto previousFolder = ui.state.selection().folder;
  ui.state.selectFolder(folder);
  createNote(ui);
  if(ui.state.selection().noteId.empty()) ui.state.selectFolder(previousFolder);
}

static void saveCurrent(UiRuntime& ui, bool quiet) {
  if(!ui.state.hasLibrary()) {
    if(!quiet) ui.status = "No library open";
    return;
  }
  if(ui.state.selection().noteId.empty()) {
    createNote(ui);
  }
  if(ui.state.saveSelectedNote(ui.editor.text())) {
    ui.editor.markSaved();
    if(!quiet) ui.status = "Saved " + trimTitle(ui.editor.text());
  } else {
    if(!quiet) ui.status = "Nothing selected to save";
  }
}

static bool ensureSelectedNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Open a library before attaching files";
    return false;
  }
  if(ui.state.selection().noteId.empty()) createNote(ui);
  return !ui.state.selection().noteId.empty();
}

static void insertAttachmentMarkdown(UiRuntime& ui, const attachments::AttachmentLink& link) {
  if(ui.editor.text().empty() || ui.editor.text().back() == '\n') ui.editor.insert(link.markdown + "\n");
  else ui.editor.insert("\n" + link.markdown + "\n");
  saveCurrent(ui);
}

static bool attachPathToEditor(UiRuntime& ui, const std::filesystem::path& source) {
  if(!ensureSelectedNote(ui)) return false;
  auto selected = ui.state.selectedNote();
  if(!selected) return false;
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.libraryRoot(), selected->metadata.id, source);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Attached " + source.filename().string();
    return true;
  } catch(const std::exception& error) {
    ui.status = "Attach failed: " + std::string(error.what());
    return false;
  }
}

static bool pasteClipboardImage(UiRuntime& ui) {
  if(!ensureSelectedNote(ui)) return false;
  static constexpr const char* kImageMimes[] = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/webp"};
  const char* mime = nullptr;
  for(const char* candidate : kImageMimes) {
    if(SDL_HasClipboardData(candidate)) {
      mime = candidate;
      break;
    }
  }
  if(!mime) return false;

  size_t size = 0;
  void* data = SDL_GetClipboardData(mime, &size);
  if(!data || size == 0) {
    if(data) SDL_free(data);
    ui.status = "Clipboard image data is empty";
    return true;
  }

  auto selected = ui.state.selectedNote();
  if(!selected) {
    SDL_free(data);
    return false;
  }

  attachments::AttachmentService service;
  try {
    const auto link = service.attachBytes(ui.state.libraryRoot(), selected->metadata.id, fileNameForMime(mime), data, size);
    SDL_free(data);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Pasted image attachment";
    return true;
  } catch(const std::exception& error) {
    SDL_free(data);
    ui.status = "Paste image failed: " + std::string(error.what());
    return true;
  }
}

static bool pasteClipboardText(UiRuntime& ui) {
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste editor"
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  markEdited(ui);
  SDL_free(raw);
  return true;
}

static std::string* focusedInput(UiRuntime& ui) {
  switch(ui.focus) {
    case FocusArea::Search: return &ui.searchDraft;
    case FocusArea::Find: return &ui.findDraft;
    case FocusArea::TagEditor: return &ui.tagDraft;
    case FocusArea::RenameNote: return &ui.renameDraft;
    case FocusArea::RenameFolder: return &ui.folderRenameDraft;
    default: return nullptr;
  }
}

static void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.searchDraft, ui.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    updateFindStatus(ui);
  }
}

static bool pasteClipboardIntoInput(UiRuntime& ui) {
  auto* input = focusedInput(ui);
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste input"
              << " input=" << (input != nullptr)
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!input || !hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste input bytes=" << std::strlen(raw) << "\n";
  if(ui.inputAllSelected) input->clear();
  *input += raw;
  ui.inputAllSelected = false;
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

static bool pastePrimarySelectionText(UiRuntime& ui) {
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste editor"
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  markEdited(ui);
  SDL_free(raw);
  return true;
}

static bool pastePrimarySelectionIntoInput(UiRuntime& ui) {
  auto* input = focusedInput(ui);
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste input"
              << " input=" << (input != nullptr)
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!input || !hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste input bytes=" << std::strlen(raw) << "\n";
  if(ui.inputAllSelected) input->clear();
  *input += raw;
  ui.inputAllSelected = false;
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

static void beginTagEdit(UiRuntime& ui) {
  if(ui.editor.dirty()) saveCurrent(ui);
  auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "Select a note before editing tags";
    return;
  }
  ui.tagDraft = joinTags(note->metadata.tags);
  ui.inputAllSelected = false;
  ui.focus = FocusArea::TagEditor;
  ui.status = "Edit tags, then Enter to save";
}

static void saveTags(UiRuntime& ui) {
  if(ui.state.updateSelectedTags(splitTags(ui.tagDraft))) {
    ui.focus = FocusArea::Editor;
    ui.status = "Saved tags";
  } else {
    ui.status = "No selected note for tags";
  }
}

static void beginRename(UiRuntime& ui) {
  auto note = ui.state.selectedNote();
  if(!note) {
    ui.status = "Select a note before renaming";
    return;
  }
  if(ui.editor.dirty()) saveCurrent(ui);
  ui.renameDraft = note->metadata.title.empty() ? note->item.title : note->metadata.title;
  ui.noteMenuOpen = false;
  ui.inputAllSelected = false;
  ui.focus = FocusArea::RenameNote;
  ui.status = "Rename note, then Enter to save";
}

static void saveRename(UiRuntime& ui) {
  if(ui.renameDraft.empty()) {
    ui.status = "Rename needs a title";
    return;
  }
  if(ui.state.renameSelectedNote(ui.renameDraft)) {
    loadSelectedIntoEditor(ui);
    ui.focus = FocusArea::Editor;
    ui.status = "Renamed note";
  } else {
    ui.status = "Rename failed";
  }
}

static void beginFolderCreate(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Open a library before creating notebooks";
    return;
  }
  ui.creatingFolder = true;
  ui.folderRenameDraft = "Notebook";
  ui.folderMenuOpen = false;
  ui.inputAllSelected = false;
  ui.focus = FocusArea::RenameFolder;
  ui.status = "Name notebook, then Enter to create";
}

static void beginFolderRename(UiRuntime& ui) {
  if(ui.state.selection().folder.empty()) {
    ui.status = "Root notebook cannot be renamed";
    return;
  }
  ui.creatingFolder = false;
  ui.folderRenameDraft = ui.state.selection().folder.generic_string();
  ui.folderMenuOpen = false;
  ui.inputAllSelected = false;
  ui.focus = FocusArea::RenameFolder;
  ui.status = "Rename notebook, then Enter to save";
}

static void saveFolderRename(UiRuntime& ui) {
  if(ui.folderRenameDraft.empty()) {
    ui.status = "Notebook name is required";
    return;
  }
  const bool creating = ui.creatingFolder;
  const bool saved = creating ? ui.state.createFolder(ui.folderRenameDraft) : ui.state.renameSelectedFolder(ui.folderRenameDraft);
  if(saved) {
    ui.creatingFolder = false;
    ui.focus = FocusArea::Folders;
    ui.status = creating ? "Created notebook" : "Saved notebook";
  } else {
    ui.status = "Notebook change failed";
  }
}

static void deleteSelected(UiRuntime& ui) {
  if(ui.state.deleteSelectedNote()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
    ui.noteMenuOpen = false;
    selectNoteAt(ui, 0);
    ui.status = "Deleted note";
  } else {
    ui.status = "Delete failed";
  }
}

static void deleteSelectedFolder(UiRuntime& ui) {
  if(ui.state.selection().folder.empty()) {
    ui.status = "Root notebook cannot be deleted";
    return;
  }
  if(ui.state.deleteSelectedFolder()) {
    ui.editor.setText("");
    ui.loadedNoteId.clear();
    ui.folderMenuOpen = false;
    ui.status = "Deleted notebook";
  } else {
    ui.status = "Notebook delete failed";
  }
}

static void cyclePaneMode(UiRuntime& ui) {
  switch(ui.state.shell().paneMode) {
    case ui::PaneMode::Editor:
      ui.state.shell().paneMode = ui::PaneMode::Viewer;
      ui.focus = FocusArea::Viewer;
      break;
    case ui::PaneMode::Viewer:
      ui.state.shell().paneMode = ui::PaneMode::Split;
      ui.focus = FocusArea::Editor;
      break;
    case ui::PaneMode::Split:
      ui.state.shell().paneMode = ui::PaneMode::Editor;
      ui.focus = FocusArea::Editor;
      break;
  }
}

static void updateFindStatus(UiRuntime& ui) {
  if(ui.findDraft.empty()) {
    ui.status = "Find in note";
    return;
  }
  std::size_t count = 0;
  std::size_t pos = ui.editor.text().find(ui.findDraft);
  while(pos != std::string::npos) {
    ++count;
    pos = ui.editor.text().find(ui.findDraft, pos + std::max<std::size_t>(1, ui.findDraft.size()));
  }
  ui.status = std::to_string(count) + " matches in note";
}

static std::string searchScopeLabel(library::SearchScope scope) {
  switch(scope) {
    case library::SearchScope::All: return "A";
    case library::SearchScope::Title: return "T";
    case library::SearchScope::Content: return "C";
  }
  return "A";
}

static library::SearchScope nextSearchScope(library::SearchScope scope) {
  switch(scope) {
    case library::SearchScope::All: return library::SearchScope::Title;
    case library::SearchScope::Title: return library::SearchScope::Content;
    case library::SearchScope::Content: return library::SearchScope::All;
  }
  return library::SearchScope::All;
}

static void performAction(UiRuntime& ui, UiAction action) {
  switch(action) {
    case UiAction::Refresh:
      ui.state.refreshLibrary();
      ui.status = "Refreshed library";
      break;
    case UiAction::NewNote:
      createNote(ui);
      break;
    case UiAction::RenameNote:
      beginRename(ui);
      break;
    case UiAction::DeleteNote:
      deleteSelected(ui);
      break;
    case UiAction::Save:
      saveCurrent(ui);
      break;
    case UiAction::Tags:
      beginTagEdit(ui);
      break;
    case UiAction::PaneEditor:
      ui.state.shell().paneMode = ui::PaneMode::Editor;
      ui.focus = FocusArea::Editor;
      break;
    case UiAction::PaneViewer:
      ui.state.shell().paneMode = ui::PaneMode::Viewer;
      ui.focus = FocusArea::Viewer;
      break;
    case UiAction::PaneSplit:
      ui.state.shell().paneMode = ui::PaneMode::Split;
      ui.focus = FocusArea::Editor;
      break;
  }
}

static std::filesystem::path uiStatePath(const std::filesystem::path& root) {
  return root / ".micronotes" / "ui.state";
}

static std::filesystem::path libraryPathConfigPath() {
  return micronotes::platform::resolveRuntimePaths().configDir / "library-path";
}

static std::optional<std::filesystem::path> readConfiguredLibraryRoot() {
  std::ifstream in(libraryPathConfigPath());
  if(!in) return std::nullopt;
  std::string line;
  std::getline(in, line);
  if(line.empty()) return std::nullopt;
  return std::filesystem::path(line);
}

static bool writeConfiguredLibraryRoot(const std::filesystem::path& root) {
  const auto configPath = libraryPathConfigPath();
  std::error_code ec;
  std::filesystem::create_directories(configPath.parent_path(), ec);
  if(ec) return false;
  std::ofstream out(configPath, std::ios::trunc);
  if(!out) return false;
  out << root.generic_string() << '\n';
  return static_cast<bool>(out);
}

static bool attachFromCli(UiRuntime& ui, const std::filesystem::path& source) {
  if(source.empty()) return true;
  if(!ui.state.hasLibrary()) {
    std::cerr << "--attach requires --library\n";
    return false;
  }
  ui.state.loadUiState(uiStatePath(ui.state.libraryRoot()));
  auto selected = ui.state.selectedNote();
  if(!selected) {
    std::cerr << "--attach requires a selected note saved in UI state\n";
    return false;
  }
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.libraryRoot(), selected->metadata.id, source);
    ui.editor.setText(selected->body);
    ui.editor.insert("\n" + link.markdown + "\n");
    ui.state.saveSelectedNote(ui.editor.text());
    std::cout << link.markdown << "\n";
    return true;
  } catch(const std::exception& error) {
    std::cerr << "attach failed: " << error.what() << "\n";
    return false;
  }
}

static bool hovered(const UiRuntime& ui, Rect rect) {
  return contains(rect, ui.mouseX, ui.mouseY);
}

static void drawSectionLabel(TextRenderer& text, std::string_view label, float x, float y) {
  text.draw(label, x, y, kDim);
}

static void drawEmptyMessage(TextRenderer& text, std::string_view title, std::string_view detail, Rect rect) {
  text.draw(title, rect.x + 18, rect.y + 18, kText, true);
  text.draw(detail, rect.x + 18, rect.y + 54, kMuted);
}

static std::string ellipsizeToWidth(TextRenderer& text, std::string value, int maxWidth, bool heading = false, bool mono = false) {
  if(maxWidth <= 0) return "";
  if(text.width(value, heading, mono) <= maxWidth) return value;
  while(!value.empty() && text.width(value + "...", heading, mono) > maxWidth) value.pop_back();
  return value.empty() ? "..." : value + "...";
}

static void drawFindHighlights(SDL_Renderer* renderer, TextRenderer& text, const UiRuntime& ui, const std::string& line, Rect writing, float y) {
  if(ui.findDraft.empty()) return;
  std::size_t pos = line.find(ui.findDraft);
  while(pos != std::string::npos) {
    const auto prefix = std::string_view(line.data(), pos);
    const float x = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
    const float w = static_cast<float>(std::max(6, text.width(ui.findDraft, false, true)));
    if(x < writing.x + writing.w - 8) {
      fill(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, SDL_Color {91, 77, 35, 230});
      stroke(renderer, {x, y - 2, std::min(w, writing.x + writing.w - 8 - x), static_cast<float>(text.lineHeight())}, SDL_Color {170, 142, 56, 220});
    }
    pos = line.find(ui.findDraft, pos + std::max<std::size_t>(1, ui.findDraft.size()));
  }
}

static void drawVerticalScrollbar(SDL_Renderer* renderer, Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return;
  Rect track {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, std::max(24.0f, viewport.h - 18.0f)};
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  Rect thumb {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
  fill(renderer, track, SDL_Color {32, 37, 45, 210});
  fill(renderer, thumb, kAccentDim);
  stroke(renderer, thumb, SDL_Color {89, 154, 142, 180});
}

static Rect scrollbarTrack(Rect viewport) {
  const float trackH = std::max(24.0f, viewport.h - 18.0f);
  return {viewport.x + viewport.w - 7.0f, viewport.y + 9.0f, 3.0f, trackH};
}

static Rect scrollbarThumb(Rect viewport, int scroll, int maxScroll) {
  if(maxScroll <= 0) return {};
  const auto track = scrollbarTrack(viewport);
  const float visibleRatio = std::clamp(viewport.h / (viewport.h + static_cast<float>(maxScroll)), 0.08f, 1.0f);
  const float thumbH = std::max(22.0f, track.h * visibleRatio);
  const float t = static_cast<float>(std::clamp(scroll, 0, maxScroll)) / static_cast<float>(maxScroll);
  return {track.x - 1.0f, track.y + (track.h - thumbH) * t, 5.0f, thumbH};
}

static Rect scrollbarHitRect(Rect thumb) {
  return {thumb.x - 7.0f, thumb.y - 2.0f, thumb.w + 14.0f, thumb.h + 4.0f};
}

static int scrollFromThumbY(Rect viewport, float y, float dragOffsetY, int maxScroll) {
  const auto track = scrollbarTrack(viewport);
  const auto thumb = scrollbarThumb(viewport, 0, maxScroll);
  const float range = std::max(1.0f, track.h - thumb.h);
  const float t = std::clamp((y - dragOffsetY - track.y) / range, 0.0f, 1.0f);
  return static_cast<int>(std::round(t * static_cast<float>(maxScroll)));
}

static Rect searchBoxRect(Rect notes) {
  return {notes.x + 14.0f, notes.y + 12.0f, notes.w - 28.0f, 34.0f};
}

static Rect editorWritingRect(Rect editorRect) {
  return {editorRect.x + 8.0f, editorRect.y + 8.0f, editorRect.w - 16.0f, editorRect.h - 28.0f};
}

static Rect viewerPageRect(Rect viewerRect) {
  return {viewerRect.x + 8.0f, viewerRect.y + 8.0f, viewerRect.w - 16.0f, viewerRect.h - 28.0f};
}

static bool isResizeGutter(const AppLayout& layout, float x, float y) {
  if(y < layout.sidebar.y || y > layout.sidebar.y + layout.sidebar.h) return false;
  return std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f ||
         std::abs(x - (layout.notes.x + layout.notes.w)) <= 4.0f;
}

static std::optional<std::size_t> folderIndexAt(const UiRuntime& ui, Rect sidebar, float x, float y) {
  if(!contains(sidebar, x, y)) return std::nullopt;
  const auto folders = ui.state.folders();
  float rowY = sidebar.y + 46.0f;
  for(std::size_t i = 0; i < folders.size(); ++i) {
    if(rowY >= sidebar.y + sidebar.h - 120.0f) break;
    Rect row {sidebar.x + 10.0f, rowY - 6.0f, sidebar.w - 20.0f, 28.0f};
    if(contains(row, x, y)) return i;
    rowY += 26.0f;
  }
  return std::nullopt;
}

static std::optional<std::size_t> tagIndexAt(const UiRuntime& ui, Rect sidebar, float x, float y) {
  if(!contains(sidebar, x, y)) return std::nullopt;
  const auto folders = ui.state.folders();
  float rowY = sidebar.y + 46.0f;
  if(folders.empty()) {
    rowY += 24.0f;
  } else {
    for(std::size_t i = 0; i < folders.size() && rowY < sidebar.y + sidebar.h - 120.0f; ++i) rowY += 26.0f;
  }
  rowY += 42.0f;
  const auto tags = ui.state.tags();
  for(std::size_t i = 0; i < tags.size(); ++i) {
    if(rowY > sidebar.y + sidebar.h - 28.0f) break;
    Rect row {sidebar.x + 10.0f, rowY - 6.0f, sidebar.w - 20.0f, 28.0f};
    if(contains(row, x, y)) return i;
    rowY += 26.0f;
  }
  return std::nullopt;
}

static bool noteRowAt(const UiRuntime& ui, Rect notesRect, float x, float y) {
  if(!contains(notesRect, x, y)) return false;
  float rowY = notesRect.y + 62.0f;
  if(!ui.searchDraft.empty()) {
    for(const auto& result : ui.state.currentSearchResults()) {
      const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
      const float availableH = notesRect.y + notesRect.h - 24.0f - (rowY - 8.0f);
      const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
      const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
      const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
      Rect row {notesRect.x + 10.0f, rowY - 8.0f, notesRect.w - 20.0f, rowH};
      if(contains(row, x, y)) return true;
      rowY += rowH;
    }
    return false;
  }
  const auto notes = ui.state.currentNotes();
  for(std::size_t i = 0; i < notes.size() && rowY < notesRect.y + notesRect.h - 24.0f; ++i) {
    Rect row {notesRect.x + 10.0f, rowY - 8.0f, notesRect.w - 20.0f, 50.0f};
    if(contains(row, x, y)) return true;
    rowY += 50.0f;
  }
  return false;
}

static std::string blockText(const markdown::Block& block) {
  std::string out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) {
      continue;
    } else if(inlineItem.type == markdown::InlineType::Link) {
      out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      out += block.type == markdown::BlockType::Code ? inlineItem.text : "`" + inlineItem.text + "`";
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      out += "[" + inlineItem.text + "]";
    } else {
      out += inlineItem.text;
    }
  }
  return out;
}

static std::vector<std::string> codeBlockLines(const markdown::Block& block) {
  auto lines = splitLines(blockText(block));
  if(lines.size() > 1 && lines.back().empty()) lines.pop_back();
  if(lines.empty()) lines.emplace_back();
  return lines;
}

static std::vector<markdown::Inline> blockImages(const markdown::Block& block) {
  std::vector<markdown::Inline> out;
  for(const auto& inlineItem : block.inlines) {
    if(inlineItem.type == markdown::InlineType::Image) out.push_back(inlineItem);
  }
  return out;
}

struct InlineRun {
  std::string text;
  std::string target;
  SDL_Color color = kText;
  bool mono = false;
  bool strong = false;
  bool emphasis = false;
  bool strikethrough = false;
};

static std::vector<InlineRun> inlineRuns(const std::vector<markdown::Inline>& inlines, SDL_Color baseColor = kText) {
  std::vector<InlineRun> runs;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    InlineRun run;
    run.text = inlineItem.text;
    run.color = baseColor;
    run.strong = inlineItem.strong;
    run.emphasis = inlineItem.emphasis;
    run.strikethrough = inlineItem.strikethrough;
    if(inlineItem.type == markdown::InlineType::Link) {
      run.text = inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
      run.target = inlineItem.target;
      run.color = kAccent;
    } else if(inlineItem.type == markdown::InlineType::Code) {
      run.mono = true;
      run.color = kWarn;
    } else if(inlineItem.type == markdown::InlineType::Emphasis) {
      run.emphasis = true;
    } else if(inlineItem.type == markdown::InlineType::Strong) {
      run.strong = true;
    } else if(inlineItem.type == markdown::InlineType::Strikethrough) {
      run.strikethrough = true;
    } else if(inlineItem.type == markdown::InlineType::FootnoteRef) {
      run.text = "[" + inlineItem.text + "]";
      run.target = "#fn-" + inlineItem.text;
      run.color = kAccent;
    } else if(inlineItem.type == markdown::InlineType::Html) {
      run.color = kDim;
    }
    if(!run.text.empty()) runs.push_back(std::move(run));
  }
  return runs;
}

static std::vector<InlineRun> inlineRuns(const markdown::Block& block, SDL_Color baseColor = kText) {
  return inlineRuns(block.inlines, baseColor);
}

static std::vector<InlineRun> splitRunWords(const InlineRun& run) {
  std::vector<InlineRun> out;
  std::string token;
  auto flush = [&]() {
    if(token.empty()) return;
    InlineRun item = run;
    item.text = token;
    out.push_back(std::move(item));
    token.clear();
  };
  for(const char c : run.text) {
    if(c == '\n') {
      flush();
      InlineRun item = run;
      item.text = "\n";
      out.push_back(std::move(item));
    } else if(std::isspace(static_cast<unsigned char>(c))) {
      flush();
    } else {
      token.push_back(c);
    }
  }
  flush();
  return out;
}

static int measureInlineLines(TextRenderer& text, const std::vector<InlineRun>& runs, int maxWidth, bool heading = false) {
  int lines = 1;
  int x = 0;
  for(const auto& run : runs) {
    for(const auto& word : splitRunWords(run)) {
      if(word.text == "\n") {
        ++lines;
        x = 0;
        continue;
      }
      const int wordW = text.width(word.text, heading, word.mono, word.strong, word.emphasis);
      const int spaceW = x == 0 ? 0 : text.width(" ", heading, word.mono, word.strong, word.emphasis);
      if(x > 0 && x + spaceW + wordW > maxWidth) {
        ++lines;
        x = wordW;
      } else {
        x += spaceW + wordW;
      }
    }
  }
  return std::max(1, lines);
}

static float drawInlineRuns(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>* links, const std::vector<InlineRun>& runs, float x, float y, int maxWidth, int lineStep, bool heading = false) {
  float cursorX = x;
  float cursorY = y;
  for(const auto& run : runs) {
    for(const auto& word : splitRunWords(run)) {
      if(word.text == "\n") {
        cursorX = x;
        cursorY += static_cast<float>(lineStep);
        continue;
      }
      const int wordW = text.width(word.text, heading, word.mono, word.strong, word.emphasis);
      const int spaceW = cursorX == x ? 0 : text.width(" ", heading, word.mono, word.strong, word.emphasis);
      if(cursorX > x && cursorX + static_cast<float>(spaceW + wordW) > x + static_cast<float>(maxWidth)) {
        cursorX = x;
        cursorY += static_cast<float>(lineStep);
      } else if(cursorX > x) {
        cursorX += static_cast<float>(spaceW);
      }
      text.draw(word.text, cursorX, cursorY, run.color, heading, run.mono, run.strong, run.emphasis);
      if(run.strikethrough) {
        const float lineY = cursorY + static_cast<float>(text.lineHeight(heading)) * 0.55f;
        hLine(renderer, cursorX, cursorX + static_cast<float>(wordW), lineY, run.color);
      }
      if(!run.target.empty() && links) {
        links->push_back({{cursorX, cursorY, static_cast<float>(wordW), static_cast<float>(text.lineHeight(heading))}, run.target});
        hLine(renderer, cursorX, cursorX + static_cast<float>(wordW), cursorY + static_cast<float>(text.lineHeight(heading) - 2), kAccentDim);
      }
      cursorX += static_cast<float>(wordW);
    }
  }
  return cursorY;
}

static std::vector<std::string> wrapText(TextRenderer& text, std::string_view value, int maxWidth, bool heading = false, bool mono = false) {
  std::vector<std::string> out;
  if(maxWidth <= 0) {
    out.emplace_back(value);
    return out;
  }
  std::istringstream logicalLines {std::string(value)};
  std::string logicalLine;
  while(std::getline(logicalLines, logicalLine)) {
    if(logicalLine.empty()) {
      out.emplace_back();
      continue;
    }
    std::string line;
    std::istringstream words {logicalLine};
    std::string word;
    while(words >> word) {
      const std::string candidate = line.empty() ? word : line + " " + word;
      if(!line.empty() && text.width(candidate, heading, mono) > maxWidth) {
        out.push_back(line);
        line = word;
        while(text.width(line, heading, mono) > maxWidth && line.size() > 1) {
          std::string chunk = line;
          while(chunk.size() > 1 && text.width(chunk, heading, mono) > maxWidth) chunk.pop_back();
          out.push_back(chunk);
          line.erase(0, chunk.size());
        }
      } else {
        line = candidate;
      }
    }
    out.push_back(line);
  }
  if(out.empty()) out.emplace_back();
  return out;
}

static std::string inlinePlainText(const std::vector<markdown::Inline>& inlines) {
  std::string out;
  for(const auto& inlineItem : inlines) {
    if(inlineItem.type == markdown::InlineType::Image) continue;
    if(inlineItem.type == markdown::InlineType::Link) out += inlineItem.text.empty() ? inlineItem.target : inlineItem.text;
    else if(inlineItem.type == markdown::InlineType::FootnoteRef) out += "[" + inlineItem.text + "]";
    else out += inlineItem.text;
  }
  return out;
}

static std::string anchorFor(std::string value) {
  std::string out;
  bool pendingDash = false;
  for(unsigned char c : value) {
    if(std::isalnum(c)) {
      if(pendingDash && !out.empty()) out.push_back('-');
      out.push_back(static_cast<char>(std::tolower(c)));
      pendingDash = false;
    } else if(!out.empty()) {
      pendingDash = true;
    }
  }
  return out;
}

static float listMarkerWidth(const markdown::Block& block) {
  if(block.type == markdown::BlockType::OrderedItem) return 26.0f;
  if(block.type == markdown::BlockType::UnorderedItem) return 18.0f;
  return 0.0f;
}

static float blockBottomSpacing(const markdown::Block& block, bool heading, bool html) {
  if(block.type == markdown::BlockType::OrderedItem || block.type == markdown::BlockType::UnorderedItem) return 2.0f;
  return heading ? 14.0f : html ? 8.0f : 10.0f;
}

static float admonitionLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Admonition) return 0.0f;
  const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
  return static_cast<float>(text.width(label, false, false, true)) + 18.0f;
}

static float footnoteLabelWidth(TextRenderer& text, const markdown::Block& block) {
  if(block.type != markdown::BlockType::Footnote) return 0.0f;
  const auto label = "[" + (block.footnoteLabel.empty() ? std::string("*") : block.footnoteLabel) + "]";
  return static_cast<float>(text.width(label)) + 12.0f;
}

static std::string imagePlaceholder(const markdown::Inline& image, const UiRuntime& ui, attachments::AttachmentService& attachmentService) {
  if(isRemoteTarget(image.target) || !ui.state.hasLibrary()) return "[remote image skipped: " + image.target + "]";
  try {
    const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
    if(!attachmentService.isSupportedImage(path)) return "[image link: " + image.target + "]";
  } catch(const std::exception&) {
    return "[unsafe image path]";
  }
  return "[image unavailable: " + image.target + "]";
}

static float imagePlaceholderHeight(TextRenderer& text, std::string_view placeholder, float width) {
  const auto lines = wrapText(text, placeholder, static_cast<int>(width), false, true);
  return static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight() + 2) + 8);
}

static float tableHeight(TextRenderer& text, const markdown::Block& block, float width) {
  const float rowPadY = 8.0f;
  const int cols = std::max(1, [&]() {
    int count = 0;
    for(const auto& row : block.tableRows) count = std::max(count, static_cast<int>(row.cells.size()));
    return count;
  }());
  const float cellW = std::max(48.0f, (width - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float h = 0.0f;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f)));
    }
    h += static_cast<float>(rowLines * (text.lineHeight() + 2)) + rowPadY * 2.0f;
  }
  return h + 10.0f;
}

static void drawTable(SDL_Renderer* renderer, TextRenderer& text, std::vector<LinkRegion>& links, const markdown::Block& block, Rect rect) {
  int cols = 0;
  for(const auto& row : block.tableRows) cols = std::max(cols, static_cast<int>(row.cells.size()));
  if(cols <= 0) return;
  const float cellW = std::max(48.0f, (rect.w - static_cast<float>(cols + 1)) / static_cast<float>(cols));
  float y = rect.y;
  for(const auto& row : block.tableRows) {
    int rowLines = 1;
    for(const auto& cell : row.cells) {
      rowLines = std::max(rowLines, measureInlineLines(text, inlineRuns(cell.inlines), static_cast<int>(cellW - 14.0f)));
    }
    const float rowH = static_cast<float>(rowLines * (text.lineHeight() + 2)) + 16.0f;
    float x = rect.x;
    for(int i = 0; i < cols; ++i) {
      const markdown::TableCell* cell = i < static_cast<int>(row.cells.size()) ? &row.cells[static_cast<std::size_t>(i)] : nullptr;
      Rect cellRect {x, y, cellW, rowH};
      fill(renderer, cellRect, row.header ? SDL_Color {24, 30, 37, 255} : SDL_Color {16, 20, 25, 255});
      stroke(renderer, cellRect, kDivider);
      if(cell) {
        auto runs = inlineRuns(cell->inlines, row.header ? kText : kMuted);
        const auto cellText = inlinePlainText(cell->inlines);
        float textX = x + 7.0f;
        if(cell->align == markdown::Align::Right) {
          textX = std::max(textX, x + cellW - 7.0f - static_cast<float>(text.width(cellText)));
        } else if(cell->align == markdown::Align::Center) {
          textX = std::max(textX, x + (cellW - static_cast<float>(text.width(cellText))) / 2.0f);
        }
        drawInlineRuns(renderer, text, &links, runs, textX, y + 8.0f, static_cast<int>(cellW - 14.0f), text.lineHeight() + 2, false);
      }
      x += cellW;
    }
    y += rowH;
  }
}

static int viewerMaxScroll(TextRenderer& text, UiRuntime& ui, Rect rect) {
  Rect page {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  attachments::AttachmentService attachmentService;
  const auto& doc = previewDocument(ui);
  const float contentTop = page.y + 14.0f;
  const float contentLeft = page.x + 12.0f;
  const float contentRight = page.x + page.w - 16.0f;
  const float contentWidth = std::max(80.0f, contentRight - contentLeft);
  float measureY = contentTop;
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool html = block.type == markdown::BlockType::Html;
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
    const int lineHeight = text.lineHeight(heading);
    if(rule) {
      measureY += 22.0f;
    } else if(table) {
      measureY += tableHeight(text, block, contentWidth - indentW) + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      measureY += static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight(false) + 2)) + 18.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, kText);
        const int lineStep = lineHeight + (heading ? 4 : 0);
        const float chromeW = markerW + quoteW + indentW + admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
        measureY += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(contentWidth - chromeW), heading) * lineStep) + blockBottomSpacing(block, heading, html);
      }
      for(const auto& image : blockImages(block)) {
        measureY += imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), contentWidth);
      }
    }
  }
  return std::max(0, static_cast<int>(std::ceil(measureY - contentTop - page.h + 24.0f)));
}

static void drawSidebar(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kSidebarBg);
  fill(renderer, rect, SDL_Color {15, 17, 22, 255});
  ClipGuard clip(renderer, rect);
  float y = rect.y + 18;
  drawSectionLabel(text, "NOTEBOOKS", rect.x + 18, y);
  y += 28;
  auto folders = ui.state.folders();
  if(folders.empty()) {
    text.draw("No notebooks yet", rect.x + 16, y, kMuted);
    y += 24;
  } else {
    for(std::size_t i = 0; i < folders.size() && y < rect.y + rect.h - 120; ++i) {
      const bool selected = ui.state.selection().folder == folders[i].path && ui.state.selection().tag.empty();
      Rect row {rect.x + 10, y - 6, rect.w - 20, 28};
      drawSelection(renderer, row, selected, hovered(ui, row));
      const auto label = folders[i].path.empty() ? "/" : folders[i].path.generic_string();
      text.draw(ellipsize(label, 19), rect.x + 20, y, selected ? kText : kMuted);
      const auto count = std::to_string(folders[i].noteCount);
      text.draw(count, row.x + row.w - static_cast<float>(text.width(count)) - 10, y, selected ? kAccent : kDim);
      y += 26;
    }
  }
  y += 14;
  drawSectionLabel(text, "TAGS", rect.x + 18, y);
  y += 28;
  auto tags = ui.state.tags();
  if(tags.empty()) {
    text.draw("No tags yet", rect.x + 16, y, kMuted);
  } else {
    for(const auto& tag : tags) {
      if(y > rect.y + rect.h - 28) break;
      const bool selected = ui.state.selection().tag == tag;
      Rect row {rect.x + 10, y - 6, rect.w - 20, 28};
      drawSelection(renderer, row, selected, hovered(ui, row));
      text.draw("#" + ellipsize(tag, 22), rect.x + 20, y, selected ? kText : kMuted);
      y += 26;
    }
  }
}

static void drawNotes(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kNotesBg);
  ClipGuard clip(renderer, rect);
  Rect search {rect.x + 14, rect.y + 12, rect.w - 28, 34};
  drawSurface(renderer, search, kInputBg, ui.focus == FocusArea::Search ? kAccentDim : kHairline);
  ui.searchScopeToggle = {search.x + search.w - 34, search.y + 5, 24, 24};
  const auto searchLabel = ui.searchDraft.empty() ? "Search all notes" : ui.searchDraft;
  if(ui.focus == FocusArea::Search && ui.inputAllSelected && !ui.searchDraft.empty()) {
    fill(renderer, {search.x + 50, search.y + 6, search.w - 88, 22}, SDL_Color {48, 82, 95, 210});
  }
  text.draw("Find", search.x + 12, search.y + 8, ui.focus == FocusArea::Search ? kAccent : kDim);
  text.draw(ellipsizeToWidth(text, searchLabel, static_cast<int>(search.w - 94)), search.x + 52, search.y + 8, ui.searchDraft.empty() ? kDim : kText);
  fill(renderer, ui.searchScopeToggle, ui.focus == FocusArea::Search ? kAccentSoft : kSurface);
  stroke(renderer, ui.searchScopeToggle, ui.focus == FocusArea::Search ? kAccentDim : kHairline);
  text.draw(searchScopeLabel(ui.searchScope), ui.searchScopeToggle.x + 8, ui.searchScopeToggle.y + 4, ui.focus == FocusArea::Search ? kAccent : kMuted);

  float y = rect.y + 62;
  if(!ui.searchDraft.empty()) {
    const auto results = ui.state.currentSearchResults();
    if(results.empty()) {
      drawEmptyMessage(text, "No matches", "Try a different global search.", {rect.x + 8, y - 8, rect.w - 16, 96});
      return;
    }
    for(const auto& result : results) {
      if(y >= rect.y + rect.h - 24) break;
      const bool selected = result.id == ui.state.selection().noteId;
      const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
      const float availableH = rect.y + rect.h - 24.0f - (y - 8.0f);
      const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
      const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
      const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
      Rect row {rect.x + 10, y - 8, rect.w - 20, rowH};
      drawSelection(renderer, row, selected, hovered(ui, row));
      if(!selected && !hovered(ui, row)) hLine(renderer, row.x + 8, row.x + row.w - 8, row.y + row.h, kHairline);
      text.draw(ellipsizeToWidth(text, result.title, static_cast<int>(row.w - 28)), rect.x + 20, y, selected ? kText : kMuted);
      if(visibleSnippets > 0) {
        float snippetY = y + 22;
        for(std::size_t i = 0; i < visibleSnippets; ++i) {
          const auto snippet = result.snippets.empty()
            ? library::SearchResult::Snippet {result.beforeLine, result.matchLine, result.afterLine}
            : result.snippets[i];
          text.draw(ellipsizeToWidth(text, snippet.beforeLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY, kDim, false, true);
          text.draw(ellipsizeToWidth(text, snippet.matchLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY + 20, selected ? kAccent : kText, false, true);
          text.draw(ellipsizeToWidth(text, snippet.afterLine, static_cast<int>(row.w - 28)), rect.x + 20, snippetY + 40, kDim, false, true);
          snippetY += 60.0f;
        }
      }
      y += rowH;
    }
    return;
  }

  const auto notes = ui.state.currentNotes();
  if(notes.empty()) {
    drawEmptyMessage(text, ui.state.hasLibrary() ? "No notes here" : "No library open", ui.state.hasLibrary() ? "Use New to start this collection." : "Launch with --library <path>.", {rect.x + 8, y - 8, rect.w - 16, 96});
    return;
  }
  for(std::size_t i = 0; i < notes.size() && y < rect.y + rect.h - 24; ++i) {
    const auto& note = notes[i];
    const bool selected = note.id == ui.state.selection().noteId;
    Rect row {rect.x + 10, y - 8, rect.w - 20, 50};
    drawSelection(renderer, row, selected, hovered(ui, row));
    if(!selected && !hovered(ui, row)) hLine(renderer, row.x + 8, row.x + row.w - 8, row.y + row.h, kHairline);
    text.draw(ellipsizeToWidth(text, note.title, static_cast<int>(row.w - 28)), rect.x + 20, y, selected ? kText : kMuted);
    if(!note.tags.empty()) {
      const auto tagLabel = "#" + ellipsize(note.tags.front(), 18);
      const float chipW = std::min(static_cast<float>(text.width(tagLabel) + 22), row.w - 36);
      Rect chip {rect.x + 20, y + 22, chipW, 22};
      fill(renderer, chip, selected ? kAccentSoft : SDL_Color {24, 28, 34, 255});
      stroke(renderer, chip, selected ? kAccentDim : kHairline);
      text.draw(tagLabel, chip.x + 10, chip.y + std::max(2.0f, (chip.h - static_cast<float>(text.lineHeight())) / 2.0f), selected ? kAccent : kDim);
    }
    y += 50;
  }
}

static std::pair<int, int> cursorLineColumn(std::string_view value, std::size_t cursor) {
  cursor = std::min(cursor, value.size());
  int line = 0;
  int column = 0;
  for(std::size_t i = 0; i < cursor; ++i) {
    if(value[i] == '\n') {
      ++line;
      column = 0;
    } else {
      ++column;
    }
  }
  return {line, column};
}

static std::size_t cursorIndexForLineColumn(std::string_view value, int targetLine, int targetColumn) {
  int line = 0;
  int column = 0;
  for(std::size_t i = 0; i < value.size(); ++i) {
    if(line == targetLine && column >= targetColumn) return i;
    if(value[i] == '\n') {
      if(line == targetLine) return i;
      ++line;
      column = 0;
    } else {
      ++column;
    }
  }
  return value.size();
}

static int editorMaxScroll(TextRenderer& text, const UiRuntime& ui, Rect rect) {
  Rect writing = editorWritingRect(rect);
  const int lineHeight = text.lineHeight();
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  const int lineCount = static_cast<int>(splitLines(ui.editor.text()).size());
  return std::max(0, lineCount - maxLines);
}

static bool scrollbarHit(Rect viewport, int scroll, int maxScroll, float x, float y) {
  if(maxScroll <= 0) return false;
  return contains(scrollbarHitRect(scrollbarThumb(viewport, scroll, maxScroll)), x, y);
}

static CursorKind classifyCursor(TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(ui.resizingSidebar || ui.resizingNotes) return CursorKind::ResizeHorizontal;
  if(ui.scrollDragTarget != ScrollDragTarget::None) return CursorKind::ResizeVertical;

  const float x = ui.mouseX;
  const float y = ui.mouseY;
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  if(isResizeGutter(layout, x, y)) return CursorKind::ResizeHorizontal;

  if(ui.noteMenuOpen) {
    Rect menu {
      std::min(ui.noteMenuX, static_cast<float>(width) - 150.0f),
      std::min(ui.noteMenuY, static_cast<float>(height) - 128.0f),
      140.0f,
      108.0f,
    };
    if(contains(menu, x, y)) return CursorKind::Pointer;
  }
  if(ui.folderMenuOpen) {
    Rect menu {
      std::min(ui.folderMenuX, static_cast<float>(width) - 150.0f),
      std::min(ui.folderMenuY, static_cast<float>(height) - 158.0f),
      140.0f,
      138.0f,
    };
    if(contains(menu, x, y)) return CursorKind::Pointer;
  }

  if(contains(layout.sidebar, x, y)) {
    if(folderIndexAt(ui, layout.sidebar, x, y) || tagIndexAt(ui, layout.sidebar, x, y)) return CursorKind::Pointer;
    return CursorKind::Default;
  }

  if(contains(layout.notes, x, y)) {
    const Rect search = searchBoxRect(layout.notes);
    if(contains(search, x, y)) {
      const Rect scopeToggle {search.x + search.w - 34.0f, search.y + 5.0f, 24.0f, 24.0f};
      return contains(scopeToggle, x, y) ? CursorKind::Pointer : CursorKind::Text;
    }
    return noteRowAt(ui, layout.notes, x, y) ? CursorKind::Pointer : CursorKind::Default;
  }

  if(!contains(layout.content, x, y)) return CursorKind::Default;

  Rect editorRect = layout.content;
  Rect viewerRect = layout.content;
  bool hasEditor = false;
  bool hasViewer = false;
  if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
    hasEditor = true;
  } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
    hasViewer = true;
  } else {
    hasEditor = true;
    hasViewer = true;
    editorRect.w = layout.content.w / 2.0f;
    viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
  }

  if(hasEditor && contains(editorRect, x, y)) {
    const Rect writing = editorWritingRect(editorRect);
    if(scrollbarHit(writing, ui.editorScroll, editorMaxScroll(text, ui, editorRect), x, y)) {
      return CursorKind::Pointer;
    }
    if(contains(writing, x, y)) return CursorKind::Text;
  }

  if(hasViewer && contains(viewerRect, x, y)) {
    const Rect page = viewerPageRect(viewerRect);
    if(scrollbarHit(page, ui.viewerScroll, viewerMaxScroll(text, ui, viewerRect), x, y)) {
      return CursorKind::Pointer;
    }
    for(const auto& link : ui.linkRegions) {
      if(contains(link.rect, x, y)) return CursorKind::Pointer;
    }
  }

  return CursorKind::Default;
}

static int nearestColumnForX(TextRenderer& text, const std::string& line, float localX) {
  if(localX <= 0) return 0;
  int best = 0;
  float bestDistance = localX;
  for(std::size_t i = 0; i <= line.size(); ++i) {
    const float w = static_cast<float>(text.width(std::string_view(line.data(), i), false, true));
    const float distance = std::abs(w - localX);
    if(distance <= bestDistance) {
      bestDistance = distance;
      best = static_cast<int>(i);
    }
  }
  return best;
}

static std::size_t editorIndexAtPoint(TextRenderer& text, const UiRuntime& ui, Rect rect, float x, float y) {
  const int lineHeight = text.lineHeight();
  auto lines = splitLines(ui.editor.text());
  Rect writing = editorWritingRect(rect);
  const int visibleLine = std::max(0, static_cast<int>((y - (writing.y + 12)) / static_cast<float>(lineHeight)));
  const int lineIndex = std::clamp(ui.editorScroll + visibleLine, 0, std::max(0, static_cast<int>(lines.size()) - 1));
  const int column = nearestColumnForX(text, lines[static_cast<std::size_t>(lineIndex)], x - (writing.x + 12));
  return cursorIndexForLineColumn(ui.editor.text(), lineIndex, column);
}

static void placeEditorCursor(TextRenderer& text, UiRuntime& ui, Rect rect, float x, float y) {
  ui.editor.moveCursor(editorIndexAtPoint(text, ui, rect, x, y));
  ui.revealEditorCursor = true;
}

static void selectWordAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  std::size_t cursor = std::min(ui.editor.cursor(), value.size());
  if(cursor > 0 && (cursor == value.size() || !std::isalnum(static_cast<unsigned char>(value[cursor])))) --cursor;
  std::size_t start = cursor;
  std::size_t end = cursor;
  while(start > 0 && (std::isalnum(static_cast<unsigned char>(value[start - 1])) || value[start - 1] == '_')) --start;
  while(end < value.size() && (std::isalnum(static_cast<unsigned char>(value[end])) || value[end] == '_')) ++end;
  ui.editor.selectRange(start, end);
}

static void selectLineAtCursor(UiRuntime& ui) {
  const auto& value = ui.editor.text();
  const auto cursor = std::min(ui.editor.cursor(), value.size());
  const auto lineStart = value.rfind('\n', cursor == 0 ? 0 : cursor - 1);
  const auto lineEnd = value.find('\n', cursor);
  const std::size_t start = lineStart == std::string::npos ? 0 : lineStart + 1;
  const std::size_t end = lineEnd == std::string::npos ? value.size() : lineEnd;
  ui.editor.selectRange(start, end);
}

static void drawEditor(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kEditorBg);
  Rect writing {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  drawSurface(renderer, writing, SDL_Color {13, 16, 20, 255}, ui.focus == FocusArea::Editor ? kAccentDim : kHairline);
  const int lineHeight = text.lineHeight();
  auto lines = splitLines(ui.editor.text());
  const int maxLines = std::max(1, static_cast<int>((writing.h - 22) / lineHeight));
  const auto [cursorLine, cursorColumn] = cursorLineColumn(ui.editor.text(), ui.editor.cursor());
  if(ui.revealEditorCursor) {
    if(cursorLine < ui.editorScroll) ui.editorScroll = cursorLine;
    if(cursorLine >= ui.editorScroll + maxLines) ui.editorScroll = cursorLine - maxLines + 1;
  }
  const int maxScroll = std::max(0, static_cast<int>(lines.size()) - maxLines);
  ui.editorScroll = std::clamp(ui.editorScroll, 0, maxScroll);
  ui.revealEditorCursor = false;
  {
    ClipGuard clip(renderer, {writing.x + 1, writing.y + 1, writing.w - 2, writing.h - 2});
    float y = writing.y + 12;
    for(int i = ui.editorScroll; i < static_cast<int>(lines.size()) && y < writing.y + writing.h - 12; ++i) {
      const auto& line = lines[static_cast<std::size_t>(i)];
      if(ui.editor.hasSelection()) {
        const auto lineStartIndex = cursorIndexForLineColumn(ui.editor.text(), i, 0);
        const auto lineEndIndex = lineStartIndex + line.size();
        const auto selStart = std::max(ui.editor.selectionStart(), lineStartIndex);
        const auto selEnd = std::min(ui.editor.selectionEnd(), lineEndIndex);
        if(selStart < selEnd) {
          const auto before = std::string_view(line.data(), selStart - lineStartIndex);
          const auto selected = std::string_view(line.data() + (selStart - lineStartIndex), selEnd - selStart);
          const float sx = writing.x + 12 + static_cast<float>(text.width(before, false, true));
          const float sw = static_cast<float>(text.width(selected, false, true));
          fill(renderer, {sx, y - 2, std::min(sw, writing.x + writing.w - 8 - sx), static_cast<float>(lineHeight)}, SDL_Color {48, 82, 95, 210});
        }
      }
      drawFindHighlights(renderer, text, ui, line, writing, y);
      text.draw(ellipsizeToWidth(text, line, static_cast<int>(writing.w - 24), false, true), writing.x + 12, y, kText, false, true);
      y += lineHeight;
    }
    if(ui.focus == FocusArea::Editor && cursorLine >= ui.editorScroll && cursorLine < ui.editorScroll + maxLines) {
      std::string prefix;
      if(cursorLine >= 0 && cursorLine < static_cast<int>(lines.size())) {
        const auto& line = lines[static_cast<std::size_t>(cursorLine)];
        prefix = line.substr(0, std::min<std::size_t>(line.size(), static_cast<std::size_t>(cursorColumn)));
      }
      const float cursorX = writing.x + 12 + static_cast<float>(text.width(prefix, false, true));
      const float cursorY = writing.y + 12 + static_cast<float>((cursorLine - ui.editorScroll) * lineHeight);
      fill(renderer, {std::min(cursorX, writing.x + writing.w - 8), cursorY, 2, static_cast<float>(lineHeight - 2)}, kAccent);
    }
    if(lines.empty()) text.draw("Start typing...", writing.x + 12, writing.y + 12, kMuted);
  }
  drawVerticalScrollbar(renderer, writing, ui.editorScroll, maxScroll);
}

static void drawViewer(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, Rect rect) {
  fill(renderer, rect, kViewerBg);
  Rect page {rect.x + 8, rect.y + 8, rect.w - 16, rect.h - 28};
  drawSurface(renderer, page, SDL_Color {14, 17, 21, 255}, ui.focus == FocusArea::Viewer ? kAccentDim : kHairline);
  attachments::AttachmentService attachmentService;
  const auto& doc = previewDocument(ui);
  const float contentTop = page.y + 14.0f;
  const float contentLeft = page.x + 12.0f;
  const float contentRight = page.x + page.w - 16.0f;
  const float contentWidth = std::max(80.0f, contentRight - contentLeft);
  float measureY = contentTop;
  int orderedIndex = 1;
  int footnoteIndex = 1;
  ui.viewerAnchors.clear();
  for(const auto& block : doc.blocks) {
    const bool heading = block.type == markdown::BlockType::Heading;
    const bool code = block.type == markdown::BlockType::Code;
    const bool ordered = block.type == markdown::BlockType::OrderedItem;
    const bool rule = block.type == markdown::BlockType::HorizontalRule;
    const bool table = block.type == markdown::BlockType::Table;
    const bool footnote = block.type == markdown::BlockType::Footnote;
    const bool html = block.type == markdown::BlockType::Html;
    if(!ordered) orderedIndex = 1;
    if(heading) {
      const auto anchor = anchorFor(blockText(block));
      if(!anchor.empty()) ui.viewerAnchors[anchor] = static_cast<int>(std::max(0.0f, measureY - contentTop));
    } else if(footnote && !block.footnoteLabel.empty()) {
      ui.viewerAnchors["fn-" + block.footnoteLabel] = static_cast<int>(std::max(0.0f, measureY - contentTop));
      ui.viewerAnchors["fn-" + std::to_string(footnoteIndex++)] = static_cast<int>(std::max(0.0f, measureY - contentTop));
    }
    const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
    const float markerW = listMarkerWidth(block);
    const float quoteW = block.type == markdown::BlockType::Quote ? 16.0f : 0.0f;
    const int lineHeight = text.lineHeight(heading);
    if(rule) {
      measureY += 22.0f;
    } else if(table) {
      measureY += tableHeight(text, block, contentWidth - indentW) + 12.0f;
    } else if(code) {
      const auto lines = codeBlockLines(block);
      measureY += static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight(false) + 2)) + 18.0f;
    } else {
      const auto value = blockText(block);
      if(!value.empty()) {
        const auto runs = inlineRuns(block, heading ? kText : kText);
        const int lineStep = lineHeight + (heading ? 4 : 0);
        const float chromeW = markerW + quoteW + indentW + admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
        measureY += static_cast<float>(measureInlineLines(text, runs, static_cast<int>(contentWidth - chromeW), heading) * lineStep) + blockBottomSpacing(block, heading, html);
      }
      for(const auto& image : blockImages(block)) {
        float imageW = 0;
        float imageH = 0;
        float renderedH = imagePlaceholderHeight(text, imagePlaceholder(image, ui, attachmentService), contentWidth);
        if(!isRemoteTarget(image.target) && ui.state.hasLibrary()) {
          try {
            const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
            SDL_Texture* texture = images.load(path, imageW, imageH);
            if(texture && imageW > 0 && imageH > 0) {
              const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
              const float maxH = std::max(40.0f, page.h * 0.55f);
              const float scale = std::min(1.0f, maxW / imageW);
              renderedH = imageH * std::min(scale, maxH / imageH) + 14.0f;
            }
          } catch(const std::exception&) {
          }
        }
        measureY += renderedH;
      }
    }
    if(ordered) ++orderedIndex;
  }
  const int maxScroll = std::max(0, static_cast<int>(std::ceil(measureY - contentTop - page.h + 24.0f)));
  ui.viewerScroll = std::clamp(ui.viewerScroll, 0, maxScroll);
  {
    ClipGuard clip(renderer, {page.x + 1, page.y + 1, page.w - 2, page.h - 2});
    float y = contentTop - static_cast<float>(ui.viewerScroll);
    orderedIndex = 1;
    for(const auto& block : doc.blocks) {
      const bool heading = block.type == markdown::BlockType::Heading;
      const bool code = block.type == markdown::BlockType::Code;
      const bool ordered = block.type == markdown::BlockType::OrderedItem;
      const bool unordered = block.type == markdown::BlockType::UnorderedItem;
      const bool quote = block.type == markdown::BlockType::Quote;
      const bool rule = block.type == markdown::BlockType::HorizontalRule;
      const bool table = block.type == markdown::BlockType::Table;
      const bool admonition = block.type == markdown::BlockType::Admonition;
      const bool footnote = block.type == markdown::BlockType::Footnote;
      if(!ordered) orderedIndex = 1;
      const float indentW = static_cast<float>(std::max(0, block.depth - 1)) * 14.0f;
      const float markerW = listMarkerWidth(block);
      const float quoteW = quote ? 16.0f : 0.0f;
      const float extraW = admonitionLabelWidth(text, block) + footnoteLabelWidth(text, block);
      const float textX = contentLeft + indentW + markerW + quoteW + extraW;
      const int lineHeight = text.lineHeight(heading);
      if(rule) {
        if(y + 12.0f >= page.y && y <= page.y + page.h) {
          hLine(renderer, contentLeft + indentW, contentLeft + contentWidth, y + 8.0f, kDivider);
          hLine(renderer, contentLeft + indentW, contentLeft + contentWidth, y + 9.0f, kHairline);
        }
        y += 22.0f;
      } else if(table) {
        const float blockH = tableHeight(text, block, contentWidth - indentW);
        if(y + blockH >= page.y && y <= page.y + page.h) {
          drawTable(renderer, text, ui.linkRegions, block, {contentLeft + indentW, y, contentWidth - indentW, blockH});
        }
        y += blockH + 12.0f;
      } else if(code) {
        const auto lines = codeBlockLines(block);
        const float blockH = static_cast<float>(std::max<std::size_t>(1, lines.size()) * (text.lineHeight(false) + 2)) + 10.0f;
        Rect codeRect {contentLeft + indentW, y - 6.0f, contentWidth - indentW, blockH};
        if(codeRect.y + codeRect.h >= page.y && codeRect.y <= page.y + page.h) {
          drawSurface(renderer, codeRect, SDL_Color {20, 24, 30, 255}, kHairline);
          float codeY = y;
          for(const auto& codeLine : lines) {
            text.draw(ellipsizeToWidth(text, codeLine, static_cast<int>(codeRect.w - 20.0f), false, true), codeRect.x + 10.0f, codeY, kWarn, false, true);
            codeY += static_cast<float>(text.lineHeight(false) + 2);
          }
        }
        y += blockH + 8.0f;
      } else {
        const auto value = blockText(block);
        if(!value.empty()) {
          const auto runs = inlineRuns(block, heading ? kText : kText);
          const int lineStep = lineHeight + (heading ? 4 : 0);
          const float blockWidth = contentWidth - indentW - markerW - quoteW - extraW;
          const float blockH = static_cast<float>(measureInlineLines(text, runs, static_cast<int>(blockWidth), heading) * lineStep);
          if(quote && y + blockH >= page.y && y <= page.y + page.h) {
            fill(renderer, {contentLeft + indentW, y - 2.0f, 3.0f, blockH + 2.0f}, kAccentDim);
          }
          if(admonition && y + blockH >= page.y && y <= page.y + page.h) {
            Rect callout {contentLeft + indentW, y - 7.0f, contentWidth - indentW, blockH + 12.0f};
            drawSurface(renderer, callout, SDL_Color {18, 28, 29, 255}, kAccentDim);
            const auto label = block.admonitionType.empty() ? "note" : block.admonitionType;
            text.draw(label, callout.x + 10.0f, y, kAccent, false, false, true);
          }
          if(footnote && y + blockH >= page.y && y <= page.y + page.h) {
            const auto label = block.footnoteLabel.empty() ? "*" : block.footnoteLabel;
            text.draw("[" + label + "]", contentLeft + indentW, y, kAccent);
          }
          if(ordered && y + blockH >= page.y && y <= page.y + page.h) {
            const int number = block.orderedNumber > 0 ? block.orderedNumber : orderedIndex;
            text.draw(std::to_string(number) + ".", contentLeft + indentW, y, kMuted);
          } else if(unordered && y + blockH >= page.y && y <= page.y + page.h) {
            if(block.task) {
              Rect box {contentLeft + indentW, y + 3.0f, 12.0f, 12.0f};
              stroke(renderer, box, block.taskChecked ? kAccent : kMuted);
              if(block.taskChecked) {
                hLine(renderer, box.x + 2.0f, box.x + 5.0f, box.y + 7.0f, kAccent);
                hLine(renderer, box.x + 5.0f, box.x + 10.0f, box.y + 3.0f, kAccent);
              }
            } else {
              text.draw("-", contentLeft + indentW, y, kMuted);
            }
          }
          if(y + blockH >= page.y && y <= page.y + page.h) {
            drawInlineRuns(renderer, text, &ui.linkRegions, runs, textX, y, static_cast<int>(blockWidth), lineStep, heading);
          }
          y += blockH + blockBottomSpacing(block, heading, false);
        }
        for(const auto& image : blockImages(block)) {
          float imageW = 0;
          float imageH = 0;
          SDL_Texture* texture = nullptr;
          std::string placeholder = imagePlaceholder(image, ui, attachmentService);
          if(!isRemoteTarget(image.target) && ui.state.hasLibrary()) {
            try {
              const auto path = attachmentService.resolveManaged(ui.state.libraryRoot(), image.target);
              if(attachmentService.isSupportedImage(path)) {
                texture = images.load(path, imageW, imageH);
              }
            } catch(const std::exception&) {
            }
          }
          if(texture && imageW > 0 && imageH > 0) {
            const float maxW = std::max(40.0f, std::min(contentWidth, 720.0f));
            const float maxH = std::max(40.0f, page.h * 0.55f);
            const float scale = std::min(1.0f, maxW / imageW);
            const float finalScale = std::min(scale, maxH / imageH);
            SDL_FRect dst {contentLeft, std::round(y), std::round(imageW * finalScale), std::round(imageH * finalScale)};
            if(dst.y + dst.h >= page.y && dst.y <= page.y + page.h) {
              SDL_RenderTexture(renderer, texture, nullptr, &dst);
              ui.linkRegions.push_back({{dst.x, dst.y, dst.w, dst.h}, image.target});
            }
            y += dst.h + 14.0f;
          } else {
            const auto lines = wrapText(text, placeholder, static_cast<int>(contentWidth), false, true);
            float placeholderY = y;
            const bool clickablePlaceholder = isRemoteTarget(image.target);
            for(const auto& line : lines) {
              if(placeholderY + text.lineHeight() >= page.y && placeholderY <= page.y + page.h) {
                const auto lineW = static_cast<float>(text.width(line, false, true));
                text.draw(line, contentLeft, placeholderY, clickablePlaceholder ? kAccent : kDim, false, true);
                if(clickablePlaceholder && lineW > 0.0f) {
                  ui.linkRegions.push_back({{contentLeft, placeholderY, lineW, static_cast<float>(text.lineHeight())}, image.target});
                  hLine(renderer, contentLeft, contentLeft + lineW, placeholderY + static_cast<float>(text.lineHeight() - 2), kAccentDim);
                }
              }
              placeholderY += static_cast<float>(text.lineHeight() + 2);
            }
            y = placeholderY + 8.0f;
          }
        }
      }
      if(ordered) ++orderedIndex;
    }
    if(doc.blocks.empty()) drawEmptyMessage(text, "Preview", "Markdown preview will appear here.", page);
  }
  drawVerticalScrollbar(renderer, page, ui.viewerScroll, maxScroll);
}

static void drawStatus(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, Rect rect) {
  std::string help = "Ctrl+N New note   Ctrl+S Save   Ctrl+F Find   Ctrl+Shift+F Search all   Ctrl+L Layout";
  if(ui.focus == FocusArea::TagEditor) help = "Tags: " + ui.tagDraft + "    Enter save  Esc cancel";
  if(ui.focus == FocusArea::RenameNote) help = "Rename: " + ui.renameDraft + "    Enter save  Esc cancel";
  if(ui.focus == FocusArea::RenameFolder) help = "Notebook: " + ui.folderRenameDraft + "    Enter save  Esc cancel";
  if(ui.focus == FocusArea::Search) help = "Search all: " + ui.searchDraft + "    Enter open  Esc clear";
  if(ui.focus == FocusArea::Find) help = "Find in note: " + ui.findDraft + "    Esc close";
  fill(renderer, {rect.x + 12, rect.y + 7, 6, 6}, ui.editor.dirty() ? kWarn : kAccent);
  text.draw(ellipsize(help, 100), rect.x + 28, rect.y + 6, kMuted);
  if(!ui.status.empty()) {
    const auto message = ellipsize(ui.status, 72);
    Rect pill {std::max(rect.x + 12, rect.x + rect.w - static_cast<float>(text.width(message)) - 30), rect.y + 3, static_cast<float>(text.width(message)) + 18, 22};
    fill(renderer, pill, kSurface);
    stroke(renderer, pill, kHairline);
    text.draw(message, pill.x + 9, rect.y + 6, kMuted);
  }
}

static void drawNoteMenu(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(!ui.noteMenuOpen) return;
  Rect menu {
    std::min(ui.noteMenuX, static_cast<float>(width) - 150.0f),
    std::min(ui.noteMenuY, static_cast<float>(height) - 128.0f),
    140,
    108,
  };
  drawSurface(renderer, menu, kSurfaceElevated, kDivider);
  const Rect newRow {menu.x + 6.0f, menu.y + 6.0f, menu.w - 12.0f, 30.0f};
  const Rect renameRow {menu.x + 6.0f, menu.y + 36.0f, menu.w - 12.0f, 30.0f};
  const Rect deleteRow {menu.x + 6.0f, menu.y + 66.0f, menu.w - 12.0f, 30.0f};
  drawSelection(renderer, newRow, false, hovered(ui, newRow));
  drawSelection(renderer, renameRow, false, hovered(ui, renameRow));
  drawSelection(renderer, deleteRow, false, hovered(ui, deleteRow));
  text.draw("New note", menu.x + 12, menu.y + 12, kText);
  text.draw("Rename", menu.x + 12, menu.y + 42, kText);
  text.draw("Delete", menu.x + 12, menu.y + 72, kWarn);
}

static void drawFolderMenu(SDL_Renderer* renderer, TextRenderer& text, UiRuntime& ui, int width, int height) {
  if(!ui.folderMenuOpen) return;
  Rect menu {
    std::min(ui.folderMenuX, static_cast<float>(width) - 150.0f),
    std::min(ui.folderMenuY, static_cast<float>(height) - 158.0f),
    140,
    138,
  };
  drawSurface(renderer, menu, kSurfaceElevated, kDivider);
  const bool folderSelected = !ui.state.selection().folder.empty();
  const Rect newFolderRow {menu.x + 6.0f, menu.y + 6.0f, menu.w - 12.0f, 30.0f};
  const Rect newNoteRow {menu.x + 6.0f, menu.y + 36.0f, menu.w - 12.0f, 30.0f};
  const Rect renameRow {menu.x + 6.0f, menu.y + 66.0f, menu.w - 12.0f, 30.0f};
  const Rect deleteRow {menu.x + 6.0f, menu.y + 96.0f, menu.w - 12.0f, 30.0f};
  drawSelection(renderer, newFolderRow, false, hovered(ui, newFolderRow));
  drawSelection(renderer, newNoteRow, false, folderSelected && hovered(ui, newNoteRow));
  drawSelection(renderer, renameRow, false, folderSelected && hovered(ui, renameRow));
  drawSelection(renderer, deleteRow, false, folderSelected && hovered(ui, deleteRow));
  text.draw("New notebook", menu.x + 12, menu.y + 12, kText);
  text.draw("New note", menu.x + 12, menu.y + 42, folderSelected ? kText : kDim);
  text.draw("Rename", menu.x + 12, menu.y + 72, folderSelected ? kText : kDim);
  text.draw("Delete", menu.x + 12, menu.y + 102, folderSelected ? kWarn : kDim);
}

static void drawApp(SDL_Renderer* renderer, TextRenderer& text, ImageCache& images, UiRuntime& ui, int width, int height) {
  SDL_SetRenderDrawColor(renderer, kAppBg.r, kAppBg.g, kAppBg.b, kAppBg.a);
  SDL_RenderClear(renderer);

  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  ui.linkRegions.clear();
  ui.buttonRegions.clear();

  drawSidebar(renderer, text, ui, layout.sidebar);
  drawNotes(renderer, text, ui, layout.notes);
  fill(renderer, {layout.sidebar.x + layout.sidebar.w, 0, 1, layout.sidebar.h}, kHairline);
  fill(renderer, {layout.notes.x + layout.notes.w, 0, 1, layout.notes.h}, kHairline);
  if(!ui.state.hasLibrary()) {
    fill(renderer, layout.content, kEditorBg);
    drawEmptyMessage(text, "Open a local library", "Launch micronotes with --library <path> to load Markdown notes.", {layout.content.x + 18, layout.content.y + 40, layout.content.w - 36, 120});
  } else if(ui.state.selection().noteId.empty()) {
    fill(renderer, layout.content, kEditorBg);
    drawEmptyMessage(text, "No note selected", "Use New or select a note from the list.", {layout.content.x + 18, layout.content.y + 40, layout.content.w - 36, 120});
  } else if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
    drawEditor(renderer, text, ui, layout.content);
  } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
    drawViewer(renderer, text, images, ui, layout.content);
  } else {
    const float split = layout.content.w / 2.0f;
    drawEditor(renderer, text, ui, {layout.content.x, layout.content.y, split, layout.content.h});
    fill(renderer, {layout.content.x + split, 0, 1, layout.content.h}, kHairline);
    drawViewer(renderer, text, images, ui, {layout.content.x + split, layout.content.y, layout.content.w - split, layout.content.h});
  }
  fill(renderer, layout.status, SDL_Color {12, 14, 18, 255});
  fill(renderer, {layout.status.x, layout.status.y, layout.status.w, 1}, kHairline);
  drawStatus(renderer, text, ui, layout.status);
  drawNoteMenu(renderer, text, ui, width, height);
  drawFolderMenu(renderer, text, ui, width, height);
  SDL_RenderPresent(renderer);
}

static void handleText(UiRuntime& ui, const char* input) {
  if(!input) return;
  if(auto* draft = focusedInput(ui)) {
    if(ui.inputAllSelected) draft->clear();
    *draft += input;
    ui.inputAllSelected = false;
    syncFocusedInput(ui);
  } else if(ui.focus == FocusArea::Editor) {
    ui.editor.insert(input);
    markEdited(ui);
    ui.revealEditorCursor = true;
  }
}

static void handleKey(UiRuntime& ui, SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod) {
  const SDL_Keymod currentMod = SDL_GetModState();
  const bool ctrl = ((mod | currentMod) & SDL_KMOD_CTRL) != 0;
  const bool shift = ((mod | currentMod) & SDL_KMOD_SHIFT) != 0;
  const auto shortcut = [&](SDL_Keycode keycode, SDL_Scancode code) {
    return ctrl && (key == keycode || scancode == code);
  };
  if(inputDebugEnabled()) {
    std::cerr << "input keydown"
              << " key=" << SDL_GetKeyName(key)
              << " keycode=0x" << std::hex << static_cast<Uint32>(key) << std::dec
              << " scancode=" << SDL_GetScancodeName(scancode)
              << " mod=0x" << std::hex << static_cast<Uint32>(mod)
              << " current_mod=0x" << static_cast<Uint32>(currentMod) << std::dec
              << " ctrl=" << ctrl
              << " focus=" << focusName(ui.focus)
              << " editor_selection=" << ui.editor.hasSelection()
              << " input_all_selected=" << ui.inputAllSelected
              << "\n";
  }
  if(shortcut(SDLK_N, SDL_SCANCODE_N)) {
    createNote(ui);
  } else if(shortcut(SDLK_S, SDL_SCANCODE_S)) {
    saveCurrent(ui);
  } else if(shortcut(SDLK_R, SDL_SCANCODE_R)) {
    ui.state.refreshLibrary();
    ui.status = "Refreshed library";
  } else if(shortcut(SDLK_T, SDL_SCANCODE_T)) {
    beginTagEdit(ui);
  } else if(shortcut(SDLK_A, SDL_SCANCODE_A)) {
    if(ui.focus == FocusArea::Editor) {
      ui.editor.selectAll();
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
    else if(auto* input = focusedInput(ui)) {
      ui.inputAllSelected = !input->empty();
      if(ui.inputAllSelected) SDL_SetPrimarySelectionText(input->c_str());
    }
  } else if(shortcut(SDLK_C, SDL_SCANCODE_C)) {
    if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      ui.status = setClipboardText(ui.editor.selectedText()) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    } else if(auto* input = focusedInput(ui); input && ui.inputAllSelected) {
      ui.status = setClipboardText(*input) ? "Copied selection" : "Copy failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_X, SDL_SCANCODE_X)) {
    if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
      const bool copied = setClipboardText(ui.editor.selectedText());
      ui.editor.eraseSelection();
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    } else if(auto* input = focusedInput(ui); input && ui.inputAllSelected) {
      const bool copied = setClipboardText(*input);
      input->clear();
      ui.inputAllSelected = false;
      syncFocusedInput(ui);
      ui.status = copied ? "Cut selection" : "Cut copied text failed: " + std::string(SDL_GetError());
    }
  } else if(shortcut(SDLK_Z, SDL_SCANCODE_Z)) {
    if(ui.focus == FocusArea::Editor && ui.editor.undo()) {
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = "Undo";
    }
  } else if(shortcut(SDLK_Y, SDL_SCANCODE_Y)) {
    if(ui.focus == FocusArea::Editor && ui.editor.redo()) {
      markEdited(ui);
      ui.revealEditorCursor = true;
      ui.status = "Redo";
    }
  } else if(shortcut(SDLK_V, SDL_SCANCODE_V)) {
    if(focusedInput(ui)) pasteClipboardIntoInput(ui);
    else if(ui.focus == FocusArea::Editor) {
      if(!pasteClipboardText(ui)) pasteClipboardImage(ui);
      ui.revealEditorCursor = true;
    }
  } else if(shortcut(SDLK_L, SDL_SCANCODE_L)) {
    cyclePaneMode(ui);
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F) && shift) {
    if(ui.editor.dirty() && !ui.state.selection().noteId.empty()) saveCurrent(ui);
    ui.inputAllSelected = false;
    ui.focus = FocusArea::Search;
    ui.state.setSearch(ui.searchDraft, ui.searchScope);
    ui.status = "Search all notes";
  } else if(shortcut(SDLK_F, SDL_SCANCODE_F)) {
    ui.inputAllSelected = false;
    ui.focus = FocusArea::Find;
    updateFindStatus(ui);
  } else if(key == SDLK_ESCAPE) {
    if(ui.focus == FocusArea::Search && !ui.searchDraft.empty()) {
      ui.searchDraft.clear();
      ui.state.setSearch("", ui.searchScope);
    }
    if(ui.focus == FocusArea::Find) ui.findDraft.clear();
    if(ui.focus == FocusArea::TagEditor) ui.tagDraft.clear();
    if(ui.focus == FocusArea::RenameNote) ui.renameDraft.clear();
    if(ui.focus == FocusArea::RenameFolder) ui.folderRenameDraft.clear();
    ui.creatingFolder = false;
    ui.inputAllSelected = false;
    ui.noteMenuOpen = false;
    ui.folderMenuOpen = false;
    ui.focus = FocusArea::Editor;
  } else if(ui.focus == FocusArea::Search) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.searchDraft.clear();
      ui.inputAllSelected = false;
      ui.state.setSearch(ui.searchDraft, ui.searchScope);
      selectNoteAt(ui, 0);
    } else if(key == SDLK_BACKSPACE && !ui.searchDraft.empty()) {
      ui.searchDraft.pop_back();
      ui.state.setSearch(ui.searchDraft, ui.searchScope);
      selectNoteAt(ui, 0);
    } else if(key == SDLK_RETURN) {
      ui.focus = FocusArea::Editor;
    }
  } else if(ui.focus == FocusArea::Find) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.findDraft.clear();
      ui.inputAllSelected = false;
      updateFindStatus(ui);
    } else if(key == SDLK_BACKSPACE && !ui.findDraft.empty()) {
      ui.findDraft.pop_back();
      updateFindStatus(ui);
    } else if(key == SDLK_RETURN) {
      ui.focus = FocusArea::Editor;
    }
  } else if(ui.focus == FocusArea::TagEditor) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.tagDraft.clear();
      ui.inputAllSelected = false;
    } else if(key == SDLK_BACKSPACE && !ui.tagDraft.empty()) ui.tagDraft.pop_back();
    else if(key == SDLK_RETURN) saveTags(ui);
  } else if(ui.focus == FocusArea::RenameNote) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.renameDraft.clear();
      ui.inputAllSelected = false;
    } else if(key == SDLK_BACKSPACE && !ui.renameDraft.empty()) ui.renameDraft.pop_back();
    else if(key == SDLK_RETURN) saveRename(ui);
  } else if(ui.focus == FocusArea::RenameFolder) {
    if((key == SDLK_BACKSPACE || key == SDLK_DELETE) && ui.inputAllSelected) {
      ui.folderRenameDraft.clear();
      ui.inputAllSelected = false;
    } else if(key == SDLK_BACKSPACE && !ui.folderRenameDraft.empty()) ui.folderRenameDraft.pop_back();
    else if(key == SDLK_RETURN) saveFolderRename(ui);
  } else if(ui.focus == FocusArea::Editor) {
    if(key == SDLK_BACKSPACE) {
      ui.editor.erasePrevious();
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DELETE) {
      ui.editor.eraseNext();
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RETURN) {
      ui.editor.insert("\n");
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_TAB) {
      ui.editor.insert("  ");
      markEdited(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_LEFT) {
      ui.editor.moveLeft();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_RIGHT) {
      ui.editor.moveRight();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_UP) {
      ui.editor.moveLineUp();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_DOWN) {
      ui.editor.moveLineDown();
      ui.revealEditorCursor = true;
    } else if(key == SDLK_HOME) {
      ui.editor.moveLineStart(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    } else if(key == SDLK_END) {
      ui.editor.moveLineEnd(shift);
      publishEditorPrimarySelection(ui);
      ui.revealEditorCursor = true;
    }
  } else if(ui.focus == FocusArea::Notes) {
    if(key == SDLK_DOWN) selectNoteAt(ui, ui.noteCursor + 1);
    else if(key == SDLK_UP) selectNoteAt(ui, ui.noteCursor - 1);
    else if(key == SDLK_RETURN) ui.focus = FocusArea::Editor;
  }
}

static void handleMouse(TextRenderer& text, UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);

  if(button == SDL_BUTTON_MIDDLE) {
    if(contains(layout.notes, x, y) && y >= layout.notes.y + 12 && y <= layout.notes.y + 46) {
      ui.focus = FocusArea::Search;
      ui.inputAllSelected = false;
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
      return;
    }
    if(contains(layout.content, x, y)) {
      Rect editorRect = layout.content;
      bool editorAtPoint = ui.state.shell().paneMode == ui::PaneMode::Editor;
      if(ui.state.shell().paneMode == ui::PaneMode::Split) {
        editorRect.w = layout.content.w / 2.0f;
        editorAtPoint = contains(editorRect, x, y);
      }
      if(editorAtPoint) {
        ui.focus = FocusArea::Editor;
        placeEditorCursor(text, ui, editorRect, x, y);
        ui.revealEditorCursor = true;
        ui.status = pastePrimarySelectionText(ui) ? "Pasted primary selection" : "No primary selection text";
        return;
      }
    }
    if(focusedInput(ui)) {
      ui.status = pastePrimarySelectionIntoInput(ui) ? "Pasted primary selection" : "No primary selection text";
    }
    return;
  }

  if(button == SDL_BUTTON_LEFT && contains(layout.content, x, y)) {
    Rect editorRect = layout.content;
    Rect viewerRect = layout.content;
    bool hasEditor = false;
    bool hasViewer = false;
    if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
      hasEditor = true;
    } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
      hasViewer = true;
    } else {
      hasEditor = true;
      hasViewer = true;
      editorRect.w = layout.content.w / 2.0f;
      viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
    }
    if(hasEditor) {
      Rect writing {editorRect.x + 8, editorRect.y + 8, editorRect.w - 16, editorRect.h - 28};
      const int maxScroll = editorMaxScroll(text, ui, editorRect);
      const auto thumb = scrollbarThumb(writing, ui.editorScroll, maxScroll);
      if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Editor;
        ui.scrollDragOffsetY = y - thumb.y;
        ui.focus = FocusArea::Editor;
        ui.revealEditorCursor = false;
        return;
      }
    }
    if(hasViewer) {
      Rect page {viewerRect.x + 8, viewerRect.y + 8, viewerRect.w - 16, viewerRect.h - 28};
      const int maxScroll = viewerMaxScroll(text, ui, viewerRect);
      const auto thumb = scrollbarThumb(page, ui.viewerScroll, maxScroll);
      if(maxScroll > 0 && contains(scrollbarHitRect(thumb), x, y)) {
        ui.scrollDragTarget = ScrollDragTarget::Viewer;
        ui.scrollDragOffsetY = y - thumb.y;
        ui.focus = FocusArea::Viewer;
        return;
      }
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    if(std::abs(x - (layout.sidebar.x + layout.sidebar.w)) <= 4.0f) {
      ui.resizingSidebar = true;
      return;
    }
    if(std::abs(x - (layout.notes.x + layout.notes.w)) <= 4.0f) {
      ui.resizingNotes = true;
      return;
    }
  }

  if(button == SDL_BUTTON_LEFT) {
    for(const auto& region : ui.buttonRegions) {
      if(contains(region.rect, x, y)) {
        performAction(ui, region.action);
        return;
      }
    }
  }

  if(ui.noteMenuOpen) {
    Rect menu {
      std::min(ui.noteMenuX, static_cast<float>(width) - 150.0f),
      std::min(ui.noteMenuY, static_cast<float>(height) - 128.0f),
      140,
      108,
    };
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      if(y < menu.y + 36) createNote(ui);
      else if(y < menu.y + 66) beginRename(ui);
      else deleteSelected(ui);
      return;
    }
    ui.noteMenuOpen = false;
  }

  if(ui.folderMenuOpen) {
    Rect menu {
      std::min(ui.folderMenuX, static_cast<float>(width) - 150.0f),
      std::min(ui.folderMenuY, static_cast<float>(height) - 158.0f),
      140,
      138,
    };
    if(button == SDL_BUTTON_LEFT && contains(menu, x, y)) {
      if(y < menu.y + 36) beginFolderCreate(ui);
      else if(y < menu.y + 66) {
        if(!ui.state.selection().folder.empty()) createNoteInFolder(ui, ui.state.selection().folder);
      }
      else if(y < menu.y + 96) beginFolderRename(ui);
      else deleteSelectedFolder(ui);
      return;
    }
    ui.folderMenuOpen = false;
  }

  if(contains(layout.sidebar, x, y)) {
    ui.focus = FocusArea::Folders;
    auto folders = ui.state.folders();
    int index = static_cast<int>((y - (layout.sidebar.y + 40.0f)) / 26.0f);
    if(index >= 0 && index < static_cast<int>(folders.size())) {
      ui.state.selectFolder(folders[static_cast<std::size_t>(index)].path);
      selectNoteAt(ui, 0);
      if(button == SDL_BUTTON_RIGHT) {
        ui.folderMenuOpen = true;
        ui.folderMenuX = x;
        ui.folderMenuY = y;
        ui.folderMenuPath = ui.state.selection().folder;
      }
      return;
    }
    if(button == SDL_BUTTON_RIGHT) {
      ui.folderMenuOpen = true;
      ui.folderMenuX = x;
      ui.folderMenuY = y;
      ui.folderMenuPath.clear();
      return;
    }
    const float listEnd = folders.empty() ? layout.sidebar.y + 62.0f : layout.sidebar.y + 40.0f + static_cast<float>(folders.size()) * 26.0f;
    const float firstTagY = listEnd + 42.0f;
    auto tags = ui.state.tags();
    int tagIndex = static_cast<int>((y - firstTagY) / 26.0f);
    if(tagIndex >= 0 && tagIndex < static_cast<int>(tags.size())) {
      ui.state.selectTag(tags[static_cast<std::size_t>(tagIndex)]);
      selectNoteAt(ui, 0);
    }
    return;
  }
  if(contains(layout.notes, x, y)) {
    if(y >= layout.notes.y + 12 && y <= layout.notes.y + 46) {
      if(contains(ui.searchScopeToggle, x, y)) {
        ui.searchScope = nextSearchScope(ui.searchScope);
        ui.state.setSearch(ui.searchDraft, ui.searchScope);
        ui.status = "Search scope " + searchScopeLabel(ui.searchScope);
        return;
      }
      ui.inputAllSelected = false;
      ui.focus = FocusArea::Search;
      return;
    }
    ui.focus = FocusArea::Notes;
    if(!ui.searchDraft.empty()) {
      float rowY = layout.notes.y + 62.0f;
      for(const auto& result : ui.state.currentSearchResults()) {
        const std::size_t snippetCount = std::max<std::size_t>(result.snippets.size(), result.matchLine.empty() ? 0 : 1);
        const float availableH = layout.notes.y + layout.notes.h - 24.0f - (rowY - 8.0f);
        const std::size_t maxVisibleSnippets = availableH <= 90.0f ? 0 : static_cast<std::size_t>((availableH - 30.0f) / 60.0f);
        const std::size_t visibleSnippets = std::min<std::size_t>(snippetCount, std::min<std::size_t>(4, maxVisibleSnippets));
        const float rowH = visibleSnippets > 0 ? 30.0f + static_cast<float>(visibleSnippets * 60) : 50.0f;
        Rect row {layout.notes.x + 10, rowY - 8, layout.notes.w - 20, rowH};
        if(contains(row, x, y)) {
          selectNoteById(ui, result.id);
          break;
        }
        rowY += rowH;
      }
    } else {
      int index = static_cast<int>((y - (layout.notes.y + 54.0f)) / 50.0f);
      selectNoteAt(ui, index);
      if(button == SDL_BUTTON_LEFT && !ui.state.selection().noteId.empty()) {
        ui.draggingNote = true;
        ui.draggingNoteId = ui.state.selection().noteId;
      }
    }
    if(button == SDL_BUTTON_RIGHT) {
      ui.noteMenuOpen = true;
      ui.noteMenuX = x;
      ui.noteMenuY = y;
      ui.noteMenuId = ui.state.selection().noteId;
    }
    return;
  }
  if(contains(layout.content, x, y)) {
    if(ui.state.shell().paneMode != ui::PaneMode::Editor) {
      for(const auto& link : ui.linkRegions) {
        if(contains(link.rect, x, y)) {
          const auto target = link.target;
          const auto hash = target.find('#');
          const auto filePart = hash == std::string::npos ? target : target.substr(0, hash);
          const auto anchorPart = hash == std::string::npos ? std::string() : target.substr(hash + 1);
          if(filePart.empty() && !anchorPart.empty()) {
            const auto anchor = anchorFor(anchorPart);
            auto found = ui.viewerAnchors.find(anchor);
            if(found == ui.viewerAnchors.end()) found = ui.viewerAnchors.find(anchorPart);
            if(found != ui.viewerAnchors.end()) {
              ui.viewerScroll = std::max(0, found->second);
              ui.focus = FocusArea::Viewer;
              ui.status = "Jumped to " + anchorPart;
            } else {
              ui.status = "Anchor not found: " + anchorPart;
            }
            return;
          }
          if(isRemoteTarget(target)) {
            ui.status = spawnDetached({"xdg-open", target}) ? "Opened " + target : "Open failed";
            return;
          }
          if(!anchorPart.empty()) {
            auto note = ui.state.selectedNote();
            const auto sameNote = filePart.empty() || (note && (note->item.path.filename() == std::filesystem::path(filePart).filename()));
            if(sameNote) {
              const auto anchor = anchorFor(anchorPart);
              auto found = ui.viewerAnchors.find(anchor);
              if(found != ui.viewerAnchors.end()) {
                ui.viewerScroll = std::max(0, found->second);
                ui.focus = FocusArea::Viewer;
                ui.status = "Jumped to " + anchorPart;
                return;
              }
            }
          }
          if(ui.state.hasLibrary()) {
            attachments::AttachmentService service;
            try {
              const auto command = service.openCommand(ui.state.libraryRoot(), filePart.empty() ? target : filePart);
              ui.status = spawnDetached(command) ? "Opened " + std::filesystem::path(filePart.empty() ? target : filePart).filename().string() : "Open failed";
            } catch(const std::exception&) {
              ui.status = "Unsafe or unavailable link path";
            }
            return;
          }
          ui.status = "No library for local link";
          return;
        }
      }
    }
    if(ui.state.shell().paneMode == ui::PaneMode::Viewer) ui.focus = FocusArea::Viewer;
    else if(ui.state.shell().paneMode == ui::PaneMode::Split && x >= layout.content.x + layout.content.w / 2.0f) ui.focus = FocusArea::Viewer;
    else {
      ui.focus = FocusArea::Editor;
      Rect editorRect = layout.content;
      if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
      placeEditorCursor(text, ui, editorRect, x, y);
      ui.selectingEditorText = true;
      ui.editorSelectionAnchor = ui.editor.cursor();
      const Uint64 now = SDL_GetTicks();
      ui.editorClickCount = now - ui.lastEditorClick < 450 ? ui.editorClickCount + 1 : 1;
      ui.lastEditorClick = now;
      if(ui.editorClickCount == 2) {
        selectWordAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
      }
      else if(ui.editorClickCount >= 3) {
        selectLineAtCursor(ui);
        ui.editorSelectionAnchor = ui.editor.selectionStart();
        publishEditorPrimarySelection(ui);
        ui.editorClickCount = 0;
      }
    }
  }
}

static void handleMouseUp(UiRuntime& ui, float x, float y, Uint8 button, int width, int height) {
  if(button == SDL_BUTTON_LEFT) {
    if(ui.selectingEditorText) publishEditorPrimarySelection(ui);
    ui.resizingSidebar = false;
    ui.resizingNotes = false;
    ui.selectingEditorText = false;
    ui.scrollDragTarget = ScrollDragTarget::None;
  }
  if(button != SDL_BUTTON_LEFT || !ui.draggingNote) return;
  const AppLayout layout = computeLayout(ui.state.shell(), width, height);
  if(contains(layout.sidebar, x, y)) {
    const auto folders = ui.state.folders();
    const int index = static_cast<int>((y - (layout.sidebar.y + 40.0f)) / 26.0f);
    if(index >= 0 && index < static_cast<int>(folders.size())) {
      selectNoteById(ui, ui.draggingNoteId);
      if(ui.state.moveSelectedNoteToFolder(folders[static_cast<std::size_t>(index)].path)) {
        ui.status = "Moved note to " + folders[static_cast<std::size_t>(index)].path.generic_string();
      } else {
        ui.status = "Move note failed";
      }
    }
  }
  ui.draggingNote = false;
  ui.draggingNoteId.clear();
}

}

ApplicationOptions parseArgs(int argc, char** argv) {
  ApplicationOptions options;
  for(int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if(arg == "--headless") {
      options.headless = true;
    } else if(arg == "--library" && i + 1 < argc) {
      options.libraryRoot = argv[++i];
    } else if(arg == "--set-library" && i + 1 < argc) {
      options.configuredLibraryRoot = std::filesystem::path(argv[++i]);
    } else if(arg == "--attach" && i + 1 < argc) {
      options.attachPath = argv[++i];
    }
  }
  return options;
}

int run(ApplicationOptions options) {
  micronotes::perf::ScopeTimer startup("startup");
  UiRuntime ui;
  if(options.configuredLibraryRoot) {
    if(!writeConfiguredLibraryRoot(*options.configuredLibraryRoot)) {
      std::cerr << "Failed to write library path config: " << *options.configuredLibraryRoot << "\n";
      return 1;
    }
  }
  if(options.libraryRoot.empty()) {
    if(auto configured = readConfiguredLibraryRoot()) options.libraryRoot = *configured;
  }
  if(!options.libraryRoot.empty()) {
    if(!ui.state.openOrCreateLibrary(options.libraryRoot)) {
      std::cerr << "Failed to open library: " << options.libraryRoot << "\n";
      return 1;
    }
    ui.state.loadUiState(uiStatePath(ui.state.libraryRoot()));
    ui.searchDraft = ui.state.selection().search;
    ui.searchScope = ui.state.selection().searchScope;
    loadSelectedIntoEditor(ui);
    if(ui.state.selection().noteId.empty()) selectNoteAt(ui, 0);
  }
  if(!attachFromCli(ui, options.attachPath)) return 1;
  if(options.headless) return 0;

  if(!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window* window = SDL_CreateWindow("micronotes", 1280, 800, SDL_WINDOW_RESIZABLE);
  if(!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if(!renderer) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_StartTextInput(window);
  TextRenderer text(renderer);
  ImageCache images(renderer);
  SystemCursors cursors;
  if(!cursors.init()) {
    std::cerr << "SDL_CreateSystemCursor failed: " << SDL_GetError() << "\n";
  }
  auto updateCursor = [&](int width, int height) {
    cursors.apply(classifyCursor(text, ui, width, height));
  };

  auto autosaveWaitMs = [&]() -> int {
    if(!ui.state.hasLibrary() || !ui.editor.dirty() || ui.state.selection().noteId.empty()) return -1;
    const Uint64 now = SDL_GetTicks();
    const Uint64 next = std::max(ui.lastEdit + 1201, ui.lastAutosaveAttempt + 1001);
    if(now >= next) return 0;
    return std::clamp(static_cast<int>(next - now), 1, 1200);
  };

  bool running = true;
  bool needsDraw = true;
  while(running) {
    SDL_Event event;
    const int waitMs = autosaveWaitMs();
    const bool hasEvent = waitMs < 0 ? SDL_WaitEvent(&event) : SDL_WaitEventTimeout(&event, waitMs);
    if(hasEvent) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      do {
        needsDraw = true;
      if(event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if(event.type == SDL_EVENT_TEXT_INPUT) {
        handleText(ui, event.text.text);
      } else if(event.type == SDL_EVENT_KEY_DOWN) {
        handleKey(ui, event.key.key, event.key.scancode, event.key.mod);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        ui.mouseX = event.button.x;
        ui.mouseY = event.button.y;
        handleMouse(text, ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        ui.mouseX = event.button.x;
        ui.mouseY = event.button.y;
        handleMouseUp(ui, event.button.x, event.button.y, event.button.button, width, height);
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_MOTION) {
        ui.mouseX = event.motion.x;
        ui.mouseY = event.motion.y;
        if(ui.selectingEditorText) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          Rect editorRect = layout.content;
          if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
          const auto cursor = editorIndexAtPoint(text, ui, editorRect, event.motion.x, event.motion.y);
          ui.editor.selectRange(ui.editorSelectionAnchor, cursor);
          publishEditorPrimarySelection(ui);
          ui.revealEditorCursor = true;
        } else if(ui.scrollDragTarget != ScrollDragTarget::None) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          if(ui.scrollDragTarget == ScrollDragTarget::Editor) {
            Rect editorRect = layout.content;
            if(ui.state.shell().paneMode == ui::PaneMode::Split) editorRect.w = layout.content.w / 2.0f;
            Rect writing {editorRect.x + 8, editorRect.y + 8, editorRect.w - 16, editorRect.h - 28};
            const int maxScroll = editorMaxScroll(text, ui, editorRect);
            ui.editorScroll = scrollFromThumbY(writing, event.motion.y, ui.scrollDragOffsetY, maxScroll);
            ui.revealEditorCursor = false;
          } else if(ui.scrollDragTarget == ScrollDragTarget::Viewer) {
            Rect viewerRect = layout.content;
            if(ui.state.shell().paneMode == ui::PaneMode::Split) {
              const float split = layout.content.w / 2.0f;
              viewerRect = {layout.content.x + split, layout.content.y, layout.content.w - split, layout.content.h};
            }
            Rect page {viewerRect.x + 8, viewerRect.y + 8, viewerRect.w - 16, viewerRect.h - 28};
            const int maxScroll = viewerMaxScroll(text, ui, viewerRect);
            ui.viewerScroll = scrollFromThumbY(page, event.motion.y, ui.scrollDragOffsetY, maxScroll);
          }
        } else if(ui.resizingSidebar) {
          ui.state.shell().sidebarWidth = std::clamp(static_cast<int>(event.motion.x), 150, std::max(150, width - 520));
        } else if(ui.resizingNotes) {
          const AppLayout layout = computeLayout(ui.state.shell(), width, height);
          ui.state.shell().noteListWidth = std::clamp(static_cast<int>(event.motion.x - layout.sidebar.w), 190, std::max(190, width - static_cast<int>(layout.sidebar.w) - 320));
        }
        updateCursor(width, height);
      } else if(event.type == SDL_EVENT_MOUSE_WHEEL) {
        const AppLayout layout = computeLayout(ui.state.shell(), width, height);
        Rect editorRect = layout.content;
        Rect viewerRect = layout.content;
        bool wheelEditor = false;
        bool wheelViewer = false;
        if(ui.state.shell().paneMode == ui::PaneMode::Editor) {
          wheelEditor = contains(editorRect, ui.mouseX, ui.mouseY) || ui.focus == FocusArea::Editor;
        } else if(ui.state.shell().paneMode == ui::PaneMode::Viewer) {
          wheelViewer = contains(viewerRect, ui.mouseX, ui.mouseY) || ui.focus == FocusArea::Viewer;
        } else {
          editorRect.w = layout.content.w / 2.0f;
          viewerRect = {layout.content.x + editorRect.w, layout.content.y, layout.content.w - editorRect.w, layout.content.h};
          wheelEditor = contains(editorRect, ui.mouseX, ui.mouseY);
          wheelViewer = contains(viewerRect, ui.mouseX, ui.mouseY);
        }
        if(wheelViewer) {
          ui.viewerScroll = std::max(0, ui.viewerScroll - static_cast<int>(event.wheel.y * 42));
        } else if(wheelEditor) {
          ui.editorScroll = std::clamp(ui.editorScroll - static_cast<int>(event.wheel.y * 3), 0, editorMaxScroll(text, ui, editorRect));
          ui.revealEditorCursor = false;
        }
      } else if(event.type == SDL_EVENT_DROP_FILE) {
        if(event.drop.data) attachPathToEditor(ui, event.drop.data);
      } else if(event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if(ui.state.hasLibrary() && !ui.editor.dirty()) ui.state.refreshLibrary();
      }
      } while(SDL_PollEvent(&event));
    }
    const Uint64 now = SDL_GetTicks();
    if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty() &&
       now - ui.lastEdit > 1200 && now - ui.lastAutosaveAttempt > 1000) {
      ui.lastAutosaveAttempt = now;
      saveCurrent(ui, true);
      needsDraw = true;
    }
    if(needsDraw) {
      int width = 1280;
      int height = 800;
      SDL_GetWindowSize(window, &width, &height);
      drawApp(renderer, text, images, ui, width, height);
      needsDraw = false;
    }
  }

  if(ui.state.hasLibrary() && ui.editor.dirty() && !ui.state.selection().noteId.empty()) saveCurrent(ui, true);
  if(ui.state.hasLibrary()) ui.state.saveUiState(uiStatePath(ui.state.libraryRoot()));
  SDL_StopTextInput(window);
  cursors.destroy();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

}

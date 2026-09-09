#include "TestSupport.h"

#include "app/CursorTheme.h"

#include <filesystem>
#include <fstream>
#include <vector>

// Which cursor theme SDL is pointed at, and why the shell has to choose one.
//
// SDL's Wayland backend loads cursor images out of an Xcursor theme named by
// `XCURSOR_THEME`, defaulting to `default`, and asks for the modern freedesktop
// shape names. A theme older than those names carries `hand2` and `xterm`
// instead, the lookup fails, and nothing says so: `SDL_SetCursor` returns
// success and the shape on screen does not change. The hand over a link and the
// I-beam over text both silently stop working while the arrow keeps working,
// under Wayland only.
//
// These drive the resolution over a fixture rather than over the machine's own
// `/usr/share/icons`, because a rule that only holds on one desktop is not a
// rule. See `app/CursorTheme.h`.
namespace {

struct IconFixture {
  std::filesystem::path root;

  explicit IconFixture(const std::string& name)
      : root(std::filesystem::temp_directory_path() / ("micronotes-cursor-" + name)) {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
  }
  ~IconFixture() {
    std::filesystem::remove_all(root);
  }

  // A theme carrying exactly `shapes`, and inheriting `parent` when given.
  void theme(const std::string& name, const std::vector<std::string>& shapes,
             const std::string& parent = {}) {
    const auto dir = root / name;
    std::filesystem::create_directories(dir);
    if(!shapes.empty()) std::filesystem::create_directories(dir / "cursors");
    for(const auto& shape : shapes) {
      std::ofstream out(dir / "cursors" / shape, std::ios::binary);
      out << "not really a cursor, but a file that exists";
    }
    if(parent.empty()) return;
    std::ofstream index(dir / "index.theme");
    index << "[Icon Theme]\nName=" << name << "\nInherits=" << parent << "\n";
  }

  std::vector<std::filesystem::path> dirs() const {
    return {root};
  }
};

const std::vector<std::string>& shapes() {
  return micronotes::app::shellCursorShapes();
}

}

// The shapes asked for are the two that go missing, and `default` is
// deliberately not among them: the theme this was found against carries no
// `default` either, and yet the arrow appears -- SDL has a fallback for that
// one. Requiring it would reject themes that work.
MICRONOTES_TEST(cursor_theme_asks_for_the_shapes_that_actually_go_missing) {
  const auto& asked = shapes();
  MICRONOTES_REQUIRE(asked.size() == 2);
  MICRONOTES_REQUIRE(std::find(asked.begin(), asked.end(), "pointer") != asked.end());
  MICRONOTES_REQUIRE(std::find(asked.begin(), asked.end(), "text") != asked.end());
}

MICRONOTES_TEST(cursor_theme_sees_the_shapes_a_theme_carries) {
  IconFixture icons("carries");
  icons.theme("Modern", {"pointer", "text", "left_ptr"});
  icons.theme("Ancient", {"hand2", "xterm", "left_ptr"});

  MICRONOTES_REQUIRE(micronotes::app::cursorThemeHasShapes(icons.dirs(), "Modern", shapes()));
  // The exact failure this exists for: the old names are there and the ones
  // asked for are not.
  MICRONOTES_REQUIRE(!micronotes::app::cursorThemeHasShapes(icons.dirs(), "Ancient", shapes()));
  MICRONOTES_REQUIRE(!micronotes::app::cursorThemeHasShapes(icons.dirs(), "Absent", shapes()));
}

// `default` is normally a stub whose whole content is an `Inherits=` line, so
// following the chain is not an extra: it is where the answer always is.
MICRONOTES_TEST(cursor_theme_follows_the_inherits_chain) {
  IconFixture icons("inherits");
  icons.theme("Modern", {"pointer", "text"});
  icons.theme("Stub", {}, "Modern");
  icons.theme("DeadEnd", {}, "Ancient");
  icons.theme("Ancient", {"hand2"});

  MICRONOTES_REQUIRE(micronotes::app::cursorThemeHasShapes(icons.dirs(), "Stub", shapes()));
  MICRONOTES_REQUIRE(!micronotes::app::cursorThemeHasShapes(icons.dirs(), "DeadEnd", shapes()));

  // Half from the theme and half from its parent is still a working theme: the
  // shapes do not have to come from one place.
  icons.theme("Half", {"pointer"}, "Mixed");
  icons.theme("Mixed", {"text"});
  MICRONOTES_REQUIRE(micronotes::app::cursorThemeHasShapes(icons.dirs(), "Half", shapes()));
}

// A theme inheriting itself, or inheriting in a ring, must not loop.
MICRONOTES_TEST(cursor_theme_survives_an_inherits_cycle) {
  IconFixture icons("cycle");
  icons.theme("Ouroboros", {}, "Ouroboros");
  icons.theme("Ping", {}, "Pong");
  icons.theme("Pong", {}, "Ping");
  MICRONOTES_REQUIRE(!micronotes::app::cursorThemeHasShapes(icons.dirs(), "Ouroboros", shapes()));
  MICRONOTES_REQUIRE(!micronotes::app::cursorThemeHasShapes(icons.dirs(), "Ping", shapes()));
}

// The whole decision, and the two cases where the answer is "do nothing".
MICRONOTES_TEST(cursor_theme_is_left_alone_when_there_is_nothing_to_fix) {
  IconFixture icons("nothing");
  icons.theme("Modern", {"pointer", "text"});
  icons.theme("default", {}, "Modern");

  // The theme SDL would load already answers.
  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes()).empty());
  // And a theme the user has chosen is theirs, working or not: overriding it
  // would be the app deciding it knows better than the person who set it.
  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "Ancient", shapes()).empty());
}

MICRONOTES_TEST(cursor_theme_replaces_one_that_cannot_answer) {
  IconFixture icons("replace");
  // Exactly the shape of the machine this was found on: `default` inherits an
  // ancient theme with `hand2` and no `pointer`, while a modern theme sits
  // installed beside it -- the one every GTK application is already using,
  // because GTK reads it from gsettings and SDL reads an environment variable
  // nobody sets.
  icons.theme("Ancient", {"hand2", "xterm", "left_ptr"});
  icons.theme("default", {}, "Ancient");
  icons.theme("Yaru", {"pointer", "text", "left_ptr"});

  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes()) == "Yaru");
}

// Adwaita by name rather than by search order, being the freedesktop default
// and the one a reader is least likely to find surprising.
MICRONOTES_TEST(cursor_theme_prefers_adwaita_among_several_that_work) {
  IconFixture icons("adwaita");
  icons.theme("Ancient", {"hand2"});
  icons.theme("default", {}, "Ancient");
  icons.theme("Adwaita", {"pointer", "text"});
  icons.theme("Aaardvark", {"pointer", "text"});
  icons.theme("Zebra", {"pointer", "text"});

  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes()) == "Adwaita");
}

// Nothing installed can answer, so there is no improvement to make and the
// variable stays as it is. Setting a theme that also fails would only make the
// next reader wonder why it is there.
MICRONOTES_TEST(cursor_theme_makes_nothing_up_when_no_theme_answers) {
  IconFixture icons("hopeless");
  icons.theme("Ancient", {"hand2", "xterm"});
  icons.theme("default", {}, "Ancient");

  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes()).empty());
}

// The desktop's own setting wins over Adwaita when it is readable and it works,
// so the cursor matches the rest of the session rather than merely working.
MICRONOTES_TEST(cursor_theme_prefers_what_the_desktop_is_configured_with) {
  IconFixture icons("preferred");
  icons.theme("Ancient", {"hand2"});
  icons.theme("default", {}, "Ancient");
  icons.theme("Adwaita", {"pointer", "text"});
  icons.theme("Yaru", {"pointer", "text"});

  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes(), "Yaru") ==
                     "Yaru");
  // A configured theme that is itself missing the shapes is no help, so the
  // fallback still applies -- which is the case that started all of this: the
  // desktop's setting is what SDL cannot see, and it is not always the answer
  // either.
  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes(), "Ancient") ==
                     "Adwaita");
  MICRONOTES_REQUIRE(micronotes::app::chooseCursorTheme(icons.dirs(), "", shapes(), "Absent") ==
                     "Adwaita");
}

// The search path is the one Xcursor searches, most specific first, or a
// per-user override would lose to the system copy of the same theme.
MICRONOTES_TEST(cursor_theme_search_path_puts_the_user_first) {
  const auto path = micronotes::app::iconSearchPath();
  MICRONOTES_REQUIRE(!path.empty());
  const auto system = std::find(path.begin(), path.end(), std::filesystem::path("/usr/share/icons"));
  MICRONOTES_REQUIRE(system != path.end());
  // Something user-owned comes before /usr/share/icons.
  MICRONOTES_REQUIRE(system != path.begin());
}

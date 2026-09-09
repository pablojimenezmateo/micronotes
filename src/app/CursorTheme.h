#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Which cursor theme SDL loads, and why the shell has to have an opinion.
//
// SDL's Wayland backend loads cursor *images* out of an Xcursor theme, named by
// `XCURSOR_THEME` and defaulting to `default` -- and it asks for the modern
// freedesktop shape names, `pointer` and `text`. A theme that predates those
// names carries `hand2` and `xterm` instead, and the lookup then fails
// silently: `SDL_SetCursor` still returns success, and the shape on screen
// simply does not change.
//
// That is not hypothetical and it is not exotic. On the machine this was found
// on, `XCURSOR_THEME` is unset, `/usr/share/icons/default/index.theme` says
// `Inherits=DMZ-White`, and DMZ-White has `left_ptr` and `hand2` but neither
// `pointer` nor `text`. So the arrow worked and the hand and the I-beam never
// appeared -- while the *configured* theme, the one every GTK application uses,
// was Yaru, which carries all of them. GTK reads it from gsettings; SDL reads
// an environment variable nobody sets.
//
// X11 does not have the problem, because Xcursor resolves through the user's
// own theme rather than through `XCURSOR_THEME` alone. So the symptom is
// "the cursor works under X11 and not under Wayland", which reads like an SDL
// bug and is a theme that cannot answer the question it was asked.
//
// Pure filesystem logic, with the search path and the shapes as arguments, so
// the resolution is a test rather than a thing that only reproduces on one
// desktop.
namespace micronotes::app {

// The shapes the shell asks SDL for, by their freedesktop names.
//
// `pointer` and `text`, and deliberately not `default`: DMZ-White has no
// `default` either, yet the arrow appears, so SDL has a fallback for that one
// and requiring it here would reject themes that work.
const std::vector<std::string>& shellCursorShapes();

// The standard Xcursor search path, most specific first. `$XDG_DATA_HOME` and
// `~/.icons` before the system directories, which is the order Xcursor itself
// searches.
std::vector<std::filesystem::path> iconSearchPath();

// Whether `theme` carries an image for every one of `shapes`, following its
// `index.theme` `Inherits=` chain -- which is where the answer usually is,
// since `default` is typically a stub that inherits the real theme.
bool cursorThemeHasShapes(const std::vector<std::filesystem::path>& iconDirs,
                          std::string_view theme, const std::vector<std::string>& shapes);

// The cursor theme the desktop is configured with, as far as that can be read
// without linking a desktop library, or empty.
//
// GTK's own settings file, which is where a GTK-based desktop writes it and
// where a plain window manager's user is most likely to have put it. It is
// *not* always readable: GNOME keeps the setting in dconf, a binary database,
// and a Wayland compositor may advertise `wp_cursor_shape_v1` and never consult
// a theme at all. So this is a preference and not the answer -- when it comes
// back empty, or names a theme as broken as `default`, `chooseCursorTheme`
// falls through to a theme that works.
std::string configuredCursorTheme();

// The theme to put in `XCURSOR_THEME`, or empty to leave it as it is.
//
// Empty in the two cases where there is nothing to do: `configured` is already
// set, so the user has chosen and is entitled to whatever they chose; or the
// theme SDL would load answers for every shape.
//
// Otherwise the first candidate that answers, in this order: the theme the
// desktop is configured with (`preferred`), so the cursor matches the rest of
// the session where that is knowable; then `Adwaita`, the freedesktop default
// and the theme the broken chain should have pointed at; then whatever else is
// installed, in name order so the choice is stable across machines.
//
// Empty again if nothing installed carries the shapes, because a theme that
// cannot answer either is not an improvement -- it is the same bug under a
// different name, with a line of configuration to explain away.
std::string chooseCursorTheme(const std::vector<std::filesystem::path>& iconDirs,
                              std::string_view configured, const std::vector<std::string>& shapes,
                              std::string_view preferred = {});

// Applies `chooseCursorTheme` to the running process. Must be called before
// `SDL_Init`: SDL loads the theme when it first creates a cursor, and reads the
// variable once.
void ensureCursorTheme();

}

#pragma once

#include <SDL3/SDL_stdinc.h>

#include <string>
#include <utility>

// What the shell has just been told to say, and when it was told.
//
// The status bar used to be, in its left half, a permanent echo of the last
// thing that happened: "Copied selection" sat there until the next action
// replaced it, so most of the time the bar's widest column was reporting
// something the reader did several minutes ago and already knew about. That is
// the one thing a status bar should not spend its width on.
//
// The messages themselves cannot simply go. A hundred and forty of them are
// set across the shell and a good many are failures -- a save that could not
// write, a move that was refused, a clipboard the compositor would not hand
// over -- and a failure with nowhere to appear is a failure the reader never
// learns about. So the text stays and the *permanence* goes: a message is shown
// for a few seconds and then the bar goes back to saying what is true, which is
// what the rest of it now does.
//
// A struct with an assignment operator rather than a `say()` on the runtime,
// because `ui.status = "..."` is spelled at every one of those hundred and
// forty sites and the timestamp has to be stamped at all of them. A setter
// somebody can forget to use is a message that appears and never clears.
namespace micronotes::app {

// How long a message stays up. Long enough to read a sentence, short enough
// that a reader who looked away is not misled into thinking it is current.
inline constexpr Uint64 kStatusLingerMs = 4000;

struct StatusLine {
  std::string text;
  // `SDL_GetTicks()` when it was set. Zero means "never", which is what an
  // empty bar and a fresh shell both are -- and what makes `showing` false
  // without a special case for the empty string.
  Uint64 at = 0;

  StatusLine& operator=(std::string message) {
    text = std::move(message);
    at = SDL_GetTicks();
    return *this;
  }

  // Whether the message is still worth the reader's attention, as of `nowMs`.
  bool showing(Uint64 nowMs) const {
    return at != 0 && !text.empty() && nowMs - at < kStatusLingerMs;
  }

  // Milliseconds until it stops being worth it, or -1 when there is nothing
  // pending. What the run loop's wait has to know: nothing else moves when a
  // message expires, so without a deadline the bar would keep the stale text
  // until the next event happened to wake the window -- which on a shell that
  // sleeps until something happens can be a very long time.
  int lingerMs(Uint64 nowMs) const {
    if(!showing(nowMs)) return -1;
    return static_cast<int>(kStatusLingerMs - (nowMs - at));
  }
};

}

#pragma once

#include "export/NotePdf.h"

#include <atomic>
#include <mutex>
#include <string>

namespace micronotes::app {

// An export waiting on the desktop's file chooser.
//
// `SDL_ShowSaveFileDialog` is asynchronous and says plainly that its callback
// may arrive on a thread other than the one that opened it -- on Linux it
// comes back through the XDG portal's D-Bus connection. So this is the handoff
// between that thread and the loop, and it is deliberately the smallest one
// that works: a flag the loop reads, a mutex around the path, and nothing else
// shared.
//
// **It is held by `std::shared_ptr` and the callback holds one of its own.**
// A chooser can be open when the window is closed, and then the answer arrives
// after the shell it was asked on behalf of has been destroyed -- a write to a
// dead mutex, from a thread nothing is waiting on, which is about as hard to
// diagnose as a crash gets. The shared pointer is what makes the late answer
// land somewhere harmless instead: the state outlives the shell by exactly as
// long as it takes the callback to run and let go.
//
// The *notes* are captured when the dialog opens rather than read when it
// closes. A chooser can sit open for a minute, and in that minute the reader
// can select another note, close a tab, or edit the one being exported -- so
// resolving the request afterwards would export whatever happened to be
// selected when they pressed Save, which is not what they asked for.
struct PdfExportState {
  enum class Phase {
    Idle,
    // The chooser is up. A second export request while one is open is
    // refused rather than queued: two native dialogs at once is a state the
    // portal does not really have, and the reader has not been shown the
    // first one's answer yet.
    Asking,
    // A path came back and the loop has not written it yet.
    Chosen,
    Cancelled,
  };

  std::atomic<Phase> phase {Phase::Idle};
  // Read and written under `lock` by both threads.
  std::mutex lock;
  std::string path;
  exporting::PdfRequest request;
  // What the status line says while the chooser is up and what it names when
  // the write finishes. Held here so the message does not have to be rebuilt
  // from a selection that may have moved on.
  std::string what;

  bool busy() const { return phase.load(std::memory_order_acquire) != Phase::Idle; }
};

}

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace micronotes::app {

// What one drawn frame cost, and what it drew.
struct FrameSample {
  // Wall time from the top of the draw to the return of the present.
  std::uint64_t elapsedNanos = 0;
  // The part of that the app spent building the frame, i.e. everything before
  // the present call. This is the number the app controls and the only one a
  // budget can be written against: with vsync on, `elapsedNanos` is pinned to
  // the refresh interval whatever the work was.
  std::uint64_t workNanos = 0;
  // Blocks the page walked, and the subset that survived the visibility test.
  // The gap between them is the per-frame work a scroll pays for content that
  // is nowhere near the viewport.
  std::size_t blocksVisited = 0;
  std::size_t blocksDrawn = 0;
  // Blocks the layout had to build from scratch this frame. On a scroll this
  // should be zero: nothing changed.
  std::size_t blocksRelaid = 0;
  // Text runs handed to the renderer.
  std::size_t runsDrawn = 0;
};

// Rolling frame statistics for the live app.
//
// Frame time is the one number that decides whether the app feels fast, and it
// was the one number nothing in this repo measured: the counters said how many
// frames were presented and the scope timers were aimed at library and index
// work, so a scroll that took 40 ms a frame produced instrumentation identical
// to one that took 2 ms. This is that missing instrument.
//
// Percentiles rather than an average, because an average frame time hides
// exactly the frames the user notices. A scroll at a mean of 6 ms with a p95 of
// 45 ms reads as janky, and the mean says it is fine.
//
// Work time, not wall time. `SDL_RenderPresent` blocks until the next vsync, so
// a frame that did 0.2 ms of work and one that did 7 ms of it both report the
// refresh interval -- which is how this instrument reported a steady p50 of
// 8.46 ms on a 120 Hz display and looked like it had found something. The
// percentiles below rank the work; the present wait is reported beside them as
// its own mean, because it is the display's number rather than the app's.
//
//   MICRONOTES_TRACE_FRAMES=1   print a rolling summary every kWindow frames.
//   MICRONOTES_TRACE_FRAMES=2   also print one line per frame (a firehose; the
//                               write happens inside the frame it reports on).
//
// Off by default, and cheap when off: recording a frame is a branch, a clock
// read the caller already took, and a push_back into a fixed-capacity vector.
class FrameTrace {
public:
  // Frames per rolling summary. Two seconds of a smooth scroll, which is long
  // enough for a p95 to mean something and short enough to catch a stall while
  // the user still remembers causing it.
  static constexpr std::size_t kWindow = 120;
  // A frame over this is one the user can see. 60 Hz leaves 16.7 ms; anything
  // past it has already missed a vsync.
  static constexpr double kBudgetMs = 16.7;

  // The process-wide instance the app records into. Reads the environment once.
  static FrameTrace& instance();

  // Instances are constructible: the accumulator is plain bookkeeping with no
  // SDL in it, so a test can drive one directly instead of having to launch a
  // window and hope the frames land where it can see them.
  FrameTrace() = default;

  bool enabled() const { return enabled_; }
  bool verbose() const { return verbose_; }
  // Force the modes on from code, for tests and for a harness that wants the
  // table without the developer exporting anything.
  void configure(bool enabled, bool verbose);

  void record(const FrameSample& sample);

  // Rolling counts since the last flush. For tests.
  std::size_t frames() const { return samples_.size(); }
  std::size_t overBudget() const { return overBudget_; }

  // Nth-percentile frame time in milliseconds over the current window, 0..100.
  // Returns 0 when no frames have been recorded.
  double percentileMs(double percentile) const;

  void write(std::FILE* out) const;
  // Whatever is left in the window at shutdown, so a session shorter than one
  // window still reports.
  void dumpOnce();

private:
  void flush();
  void reset();

  bool enabled_ = false;
  bool verbose_ = false;
  bool dumped_ = false;
  // Work nanos per frame; the present wait is aggregated rather than ranked.
  std::vector<std::uint64_t> samples_;
  std::size_t overBudget_ = 0;
  std::size_t blocksVisited_ = 0;
  std::size_t blocksDrawn_ = 0;
  std::size_t blocksRelaid_ = 0;
  std::size_t runsDrawn_ = 0;
  std::uint64_t maxNanos_ = 0;
  std::uint64_t totalNanos_ = 0;
  std::uint64_t totalPresentNanos_ = 0;
  std::uint64_t windows_ = 0;
};

// Arrange for the process-wide frame trace to report whatever is left in its
// window at exit, whichever way the app leaves.
void dumpFrameTraceAtExit();

// Times the frame it is declared in and reports it on destruction. Counts the
// frame, and counts it against the budget, whether or not tracing is armed --
// those are counters, which stay cheap enough to leave on.
//
// The per-frame content numbers are filled in by whoever knows them: the page
// view sets what it walked and drew before this goes out of scope.
class ScopedFrame {
public:
  ScopedFrame();
  ~ScopedFrame();

  ScopedFrame(const ScopedFrame&) = delete;
  ScopedFrame& operator=(const ScopedFrame&) = delete;

  // The frame currently being drawn, or nullptr outside one. How a surface deep
  // in the draw reports what it did without every layer between having to pass
  // a frame object down.
  static ScopedFrame* current();

  void addBlocks(std::size_t visited, std::size_t drawn, std::size_t relaid);
  void addRuns(std::size_t runs);

  // Called immediately before the present. Everything up to here is the app's
  // work; everything after it is the display's.
  void markWorkDone();

private:
  FrameSample sample_;
  std::uint64_t startNanos_ = 0;
  std::uint64_t workDoneNanos_ = 0;
  ScopedFrame* previous_ = nullptr;
};

}

#include "TestSupport.h"

#include "app/FrameTrace.h"

using micronotes::app::FrameSample;
using micronotes::app::FrameTrace;

namespace {

std::uint64_t nanos(double ms) {
  return static_cast<std::uint64_t>(ms * 1'000'000.0);
}

// A frame that was all work and never reached a present, which is what a
// sample with no work time marked means.
FrameSample frameOf(double ms) {
  FrameSample sample;
  sample.elapsedNanos = nanos(ms);
  return sample;
}

// A frame that spent `workMs` building and then waited `presentMs` on the
// display.
FrameSample frameOf(double workMs, double presentMs) {
  FrameSample sample;
  sample.workNanos = nanos(workMs);
  sample.elapsedNanos = nanos(workMs + presentMs);
  return sample;
}

}

// The trace ranks work, not wall time. With vsync on, the present blocks until
// the display is ready, so a frame that did 0.4 ms of work and one that did 7 ms
// of it both take a whole refresh interval -- and a trace that reported that
// would say every frame on a 120 Hz screen costs 8.3 ms, whatever the app did.
MICRONOTES_TEST(frame_trace_ranks_the_work_and_not_the_wait_for_the_display) {
  FrameTrace trace;
  trace.configure(true, false);
  for(int i = 0; i < 4; ++i) trace.record(frameOf(0.5, 7.8));
  MICRONOTES_REQUIRE(trace.percentileMs(50.0) == 0.5);
  MICRONOTES_REQUIRE(trace.overBudget() == 0);
}

// And a frame whose *work* overran is over budget even though the present it
// then skipped waiting for makes the wall time no worse.
MICRONOTES_TEST(frame_trace_counts_a_frame_whose_work_overran) {
  FrameTrace trace;
  trace.configure(true, false);
  trace.record(frameOf(20.0, 0.1));
  MICRONOTES_REQUIRE(trace.overBudget() == 1);
}

// A frame trace that is off has to stay off: it sits in the paint path, and a
// vector that grows once per frame for the life of a session is a leak.
MICRONOTES_TEST(frame_trace_records_nothing_while_it_is_off) {
  FrameTrace trace;
  trace.configure(false, false);
  for(int i = 0; i < 10; ++i) trace.record(frameOf(8.0));
  MICRONOTES_REQUIRE(trace.frames() == 0);
  MICRONOTES_REQUIRE(trace.percentileMs(95.0) == 0.0);
}

MICRONOTES_TEST(frame_trace_counts_frames_that_missed_the_budget) {
  FrameTrace trace;
  trace.configure(true, false);
  trace.record(frameOf(4.0));
  trace.record(frameOf(9.0));
  trace.record(frameOf(40.0));
  trace.record(frameOf(17.0));
  MICRONOTES_REQUIRE(trace.frames() == 4);
  // 16.7 ms is one vsync at 60 Hz: 40 and 17 missed it, 4 and 9 did not.
  MICRONOTES_REQUIRE(trace.overBudget() == 2);
}

// The reason this reports percentiles rather than a mean: a scroll that is
// smooth except for one 100 ms stall has a fine average and is not fine.
MICRONOTES_TEST(frame_trace_percentiles_surface_the_frames_a_mean_would_hide) {
  FrameTrace trace;
  trace.configure(true, false);
  for(int i = 0; i < 99; ++i) trace.record(frameOf(2.0));
  trace.record(frameOf(100.0));
  MICRONOTES_REQUIRE(trace.frames() == 100);
  MICRONOTES_REQUIRE(trace.percentileMs(50.0) == 2.0);
  MICRONOTES_REQUIRE(trace.percentileMs(100.0) == 100.0);
  MICRONOTES_REQUIRE(trace.overBudget() == 1);
}

MICRONOTES_TEST(frame_trace_percentile_is_zero_before_any_frame) {
  FrameTrace trace;
  trace.configure(true, false);
  MICRONOTES_REQUIRE(trace.percentileMs(50.0) == 0.0);
}

// The window is what bounds memory. Without the flush-and-reset the sample
// vector grows for as long as the app is open.
MICRONOTES_TEST(frame_trace_rolls_over_at_the_end_of_a_window) {
  FrameTrace trace;
  trace.configure(true, false);
  for(std::size_t i = 0; i < FrameTrace::kWindow; ++i) trace.record(frameOf(1.0));
  MICRONOTES_REQUIRE(trace.frames() == 0);
  MICRONOTES_REQUIRE(trace.overBudget() == 0);
  trace.record(frameOf(1.0));
  MICRONOTES_REQUIRE(trace.frames() == 1);
}

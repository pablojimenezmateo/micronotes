#include "app/FrameTrace.h"

#include "CoreAliases.h"
#include "core/perf/PerformanceCounters.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace micronotes::app {
namespace {

std::uint64_t nowNanos() {
  return static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

double toMs(std::uint64_t nanos) {
  return static_cast<double>(nanos) / 1'000'000.0;
}

// The frame being drawn on this thread. Frames are drawn on the event-loop
// thread only, but the pointer is thread-local anyway: a surface that ever
// measures itself off it should report nothing rather than corrupt the frame
// the main thread is in the middle of.
thread_local ScopedFrame* tCurrentFrame = nullptr;

}

FrameTrace& FrameTrace::instance() {
  static FrameTrace trace = [] {
    FrameTrace out;
    const char* value = std::getenv("MICRONOTES_TRACE_FRAMES");
    const std::string_view text = value ? value : "";
    out.enabled_ = !text.empty() && text != "0" && text != "false" && text != "off";
    out.verbose_ = text == "2" || text == "verbose";
    if(out.enabled_) out.samples_.reserve(kWindow);
    return out;
  }();
  return trace;
}

void FrameTrace::configure(bool enabled, bool verbose) {
  enabled_ = enabled;
  verbose_ = verbose;
  if(enabled_) samples_.reserve(kWindow);
}

void FrameTrace::record(const FrameSample& sample) {
  if(!enabled_) return;

  if(verbose_) {
    std::fprintf(stderr, "[frame] %7.2f ms | blocks %zu/%zu | relaid %zu | runs %zu\n",
                 toMs(sample.elapsedNanos), sample.blocksDrawn, sample.blocksVisited,
                 sample.blocksRelaid, sample.runsDrawn);
  }

  samples_.push_back(sample.elapsedNanos);
  totalNanos_ += sample.elapsedNanos;
  maxNanos_ = std::max(maxNanos_, sample.elapsedNanos);
  blocksVisited_ += sample.blocksVisited;
  blocksDrawn_ += sample.blocksDrawn;
  blocksRelaid_ += sample.blocksRelaid;
  runsDrawn_ += sample.runsDrawn;
  if(toMs(sample.elapsedNanos) > kBudgetMs) ++overBudget_;

  if(samples_.size() < kWindow) return;
  flush();
  reset();
}

double FrameTrace::percentileMs(double percentile) const {
  if(samples_.empty()) return 0.0;
  std::vector<std::uint64_t> sorted = samples_;
  std::sort(sorted.begin(), sorted.end());
  const double clamped = std::clamp(percentile, 0.0, 100.0);
  // Nearest-rank: with 120 samples there is no interpolation worth the
  // ambiguity, and the rank is the frame a reader can go and look at.
  const auto rank = static_cast<std::size_t>(
    std::ceil(clamped / 100.0 * static_cast<double>(sorted.size())));
  const std::size_t index = rank == 0 ? 0 : std::min(rank - 1, sorted.size() - 1);
  return toMs(sorted[index]);
}

void FrameTrace::write(std::FILE* out) const {
  if(!out || samples_.empty()) return;
  const auto frames = static_cast<double>(samples_.size());
  std::fprintf(out,
               "[frame] %zu frames | avg %6.2f ms | p50 %6.2f | p95 %6.2f | max %6.2f | "
               "%zu over %.1f ms | blocks %.0f/%.0f per frame | relaid %.1f | runs %.0f\n",
               samples_.size(), toMs(totalNanos_) / frames, percentileMs(50.0),
               percentileMs(95.0), toMs(maxNanos_), overBudget_, kBudgetMs,
               static_cast<double>(blocksDrawn_) / frames,
               static_cast<double>(blocksVisited_) / frames,
               static_cast<double>(blocksRelaid_) / frames,
               static_cast<double>(runsDrawn_) / frames);
  std::fflush(out);
}

void FrameTrace::dumpOnce() {
  if(!enabled_ || dumped_) return;
  dumped_ = true;
  if(samples_.empty()) return;
  std::fprintf(stderr, "[frame] final window (%llu complete windows before it):\n",
               static_cast<unsigned long long>(windows_));
  write(stderr);
}

void FrameTrace::flush() {
  ++windows_;
  write(stderr);
}

void FrameTrace::reset() {
  samples_.clear();
  overBudget_ = 0;
  blocksVisited_ = 0;
  blocksDrawn_ = 0;
  blocksRelaid_ = 0;
  runsDrawn_ = 0;
  maxNanos_ = 0;
  totalNanos_ = 0;
}

void dumpFrameTraceAtExit() {
  static const bool once = [] {
    // Same ordering care as the trace channels: construct the instance before
    // registering, so its teardown runs after this handler rather than before.
    FrameTrace::instance();
    return std::atexit([] { FrameTrace::instance().dumpOnce(); }) == 0;
  }();
  (void)once;
}

ScopedFrame::ScopedFrame() : startNanos_(nowNanos()), previous_(tCurrentFrame) {
  tCurrentFrame = this;
}

ScopedFrame::~ScopedFrame() {
  tCurrentFrame = previous_;
  sample_.elapsedNanos = nowNanos() - startNanos_;
  perf::addCounter(perf::CounterId::FramePresents);
  perf::addCounter(perf::CounterId::FrameDrawMicros, sample_.elapsedNanos / 1000);
  if(toMs(sample_.elapsedNanos) > FrameTrace::kBudgetMs) {
    perf::addCounter(perf::CounterId::FrameDrawsOverBudget);
  }
  FrameTrace::instance().record(sample_);
}

ScopedFrame* ScopedFrame::current() {
  return tCurrentFrame;
}

void ScopedFrame::addBlocks(std::size_t visited, std::size_t drawn, std::size_t relaid) {
  sample_.blocksVisited += visited;
  sample_.blocksDrawn += drawn;
  sample_.blocksRelaid += relaid;
}

void ScopedFrame::addRuns(std::size_t runs) {
  sample_.runsDrawn += runs;
}

}

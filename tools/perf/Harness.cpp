#include "perf/Harness.h"

#include "CoreAliases.h"
#include "core/perf/Perf.h"
#include "core/perf/PerformanceCounters.h"
#include "core/perf/TraceChannel.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <new>
#include <sstream>

namespace micronotes::perfharness {

thread_local std::uint64_t tAllocations = 0;
thread_local std::uint64_t tAllocatedBytes = 0;
thread_local std::uint64_t tLargestAllocation = 0;

volatile std::size_t sink = 0;

std::string heavyMarkdown(int seed, int sections) {
  std::ostringstream out;
  out << "# Perf Note " << seed << "\n\n";
  for(int section = 0; section < sections; ++section) {
    out << "## Section " << section << "\n\n";
    out << "This paragraph has searchable text, [a local link](note-" << section << ".md), ";
    out << "[a remote link](https://example.com/" << seed << "/" << section << "), ";
    out << "inline `code`, **strong text**, and raw https://example.com/raw/" << seed << "/" << section << ".\n\n";
    out << "![image " << section << "](.micronotes/attachments/perf-" << seed << "/image-" << section << ".png)\n\n";
    out << "- [x] completed item " << section << "\n";
    out << "- [ ] pending item " << section << "\n";
    out << "  - nested item with more searchable text\n\n";
    out << "| Left | Center | Right |\n|:-----|:------:|------:|\n";
    out << "| alpha " << section << " | beta " << section << " | gamma " << section << " |\n\n";
    out << "> [!NOTE]\n> A callout with enough text to exercise wrapping and inline spans.\n\n";
  }
  return out.str();
}

// The live surface has no fonts in the core library, so the budget runs against
// a fixed-advance stand-in. It exercises the same scan, cache and flow work.
doc::Metrics stubMetrics() {
  doc::Metrics metrics;
  metrics.measure = [](std::string_view value, const doc::RunStyle& style) {
    std::size_t glyphs = 0;
    for(const char c : value) {
      if((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++glyphs;
    }
    return static_cast<float>(glyphs) * style.size * 0.6f;
  };
  metrics.lineHeight = [](const doc::RunStyle& style) { return std::round(style.size * 1.5f); };
  return metrics;
}

// The harness measures whole operations itself and hands the medians to the
// same ranked table the scopes feed, so one table answers "what is slow" for
// both. The channel speaks milliseconds; the budgets below are in microseconds,
// which is the resolution a 2 ms budget needs.
void recordMicros(std::string_view name, std::uint64_t micros) {
  micronotes::perf::traceChannel().recordSample(name, static_cast<double>(micros) / 1000.0);
}

// Process CPU time, not wall clock.
//
// Wall clock includes every microsecond this process spent *descheduled*, which
// on a machine that is doing anything else is most of the number: the same
// keystroke measured 292 us at load 1 and 898 us at load 11 on this box, and
// neither figure was about the code. Nothing here waits on IO or on another
// thread, so CPU time is the operation's actual cost and it is what a budget
// can be written against on a shared machine. The clock is a syscall rather
// than a vDSO read, so it costs a few hundred nanoseconds -- irrelevant against
// the tens-to-thousands of microseconds every scenario below measures, and a
// far smaller error than the scheduler.
std::uint64_t cpuMicros() {
  timespec now {};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec) / 1000ull;
}

// ---------------------------------------------------------------------------
// The persistence lane.
//
// Autosave runs 1.2 s after the last keystroke and again every second while
// typing continues, and until this lane existed nothing measured it. Every
// other budget in this file is a layout or a paint, so a save could grow a
// whole-library tree walk and a whole-file read and the harness would stay
// green -- which is exactly what had happened.
//
// Timed on the **wall clock**, not on CLOCK_PROCESS_CPUTIME_ID like every
// scenario above. A durable write is two fsync barriers, and a barrier is time
// the process spends blocked rather than running: measured on CPU time the
// 1.1 ms this costs per save reads as about 60 us of work, which is a
// measurement of the wrong thing. The allocation counts beside the medians are
// still the machine-independent half.
std::uint64_t wallMicros() {
  timespec now {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec) / 1000ull;
}

// Ranked by self time, because that is the only ordering that answers "what
// should I look at first": an outer scope that merely contains an expensive one
// must not outrank the expensive one. The per-call and max columns separate
// "slow once" from "fast but called far too often" -- two problems with
// different fixes that a single total conflates.
void printSamples() {
  std::cout << "\n=== scope timings (ranked by self ms) ===\n";
  microcore::perf::traceChannel().write(stdout);
}

// The counters are the other half of the picture: timings say where the time
// went, counters say how many times the work happened at all. A scope that
// looks cheap per call but ran 40,000 times is invisible in the table above.
void printCounters() {
  std::cout << "\n";
  microcore::perf::writeCounters(stdout);
}

// Peak resident memory for the whole run, which is the only number here that is
// about the *ceiling* rather than about a rate. The per-scenario columns say how
// much a keystroke allocates and hands straight back; this says how much the
// process was holding at its worst moment, and a layout cache that keeps three
// generations of a document is visible in one and not the other.
//
// It covers the fixture as well as the layout -- a thousand notes on disk and an
// SQLite index are in here too -- so it is a number to watch move between runs
// rather than to attribute to any one part.
void printPeakMemory() {
  std::ifstream status("/proc/self/status");
  std::string line;
  while(std::getline(status, line)) {
    if(line.compare(0, 6, "VmHWM:") != 0) continue;
    const auto digits = line.find_first_of("0123456789");
    if(digits == std::string::npos) break;
    std::printf("\n=== peak resident memory ===\npeak_rss %.1f MB\n",
                std::strtod(line.c_str() + digits, nullptr) / 1024.0);
    return;
  }
}

}

// The global replacements themselves. They must sit in exactly one translation
// unit of the link, and this is it.

void* operator new(std::size_t size) {
  ++micronotes::perfharness::tAllocations;
  micronotes::perfharness::tAllocatedBytes += size;
  if(size > micronotes::perfharness::tLargestAllocation)
    micronotes::perfharness::tLargestAllocation = size;
  void* memory = std::malloc(size != 0 ? size : 1);
  if(memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  ++micronotes::perfharness::tAllocations;
  micronotes::perfharness::tAllocatedBytes += size;
  if(size > micronotes::perfharness::tLargestAllocation)
    micronotes::perfharness::tLargestAllocation = size;
  void* memory = std::aligned_alloc(static_cast<std::size_t>(alignment),
                                    ((size + static_cast<std::size_t>(alignment) - 1) /
                                     static_cast<std::size_t>(alignment)) *
                                      static_cast<std::size_t>(alignment));
  if(memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { std::free(memory); }

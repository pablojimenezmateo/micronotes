#include "TestSupport.h"

#include "core/perf/Perf.h"
#include "core/perf/TraceChannel.h"

#include <chrono>
#include <string>
#include <thread>

using microcore::perf::ScopeLabel;
using microcore::perf::TraceChannel;
using microcore::perf::TraceScope;

namespace {

// A channel armed from code rather than from the environment. Every test here
// gets its own, so none of them depends on how the process was launched. The
// channel itself is deliberately neither copyable nor movable -- it owns a slot
// in a thread-local table -- so it is held rather than returned.
struct ArmedChannel {
  TraceChannel channel {"test", nullptr, nullptr, nullptr};
  ArmedChannel() { channel.setAggregateEnabled(true); }
};

void burn(std::chrono::microseconds duration) {
  const auto until = std::chrono::steady_clock::now() + duration;
  while(std::chrono::steady_clock::now() < until) {}
}

const TraceChannel::Entry* find(const std::vector<TraceChannel::Entry>& entries,
                                std::string_view label) {
  for(const auto& entry : entries) {
    if(entry.label == label) return &entry;
  }
  return nullptr;
}

}

// The whole reason the tracer is off by default: a scope on a per-block path
// must cost nothing when nobody asked for numbers. If a disabled scope recorded
// anything it would also be allocating a label per call.
MICRONOTES_TEST(trace_channel_records_nothing_while_it_is_off) {
  TraceChannel channel("test", nullptr, nullptr, nullptr);
  MICRONOTES_REQUIRE(!channel.enabled());
  {
    const TraceScope scope(channel, "off.scope");
    burn(std::chrono::microseconds(200));
  }
  MICRONOTES_REQUIRE(channel.snapshot().empty());
}

MICRONOTES_TEST(trace_channel_aggregates_repeated_scopes_into_one_row) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  for(int i = 0; i < 5; ++i) {
    const TraceScope scope(channel, "hot.loop");
    burn(std::chrono::microseconds(100));
  }
  const auto entries = channel.snapshot();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().calls == 5);
  MICRONOTES_REQUIRE(entries.front().totalMs > 0.0);
  // max is one call's worth, so it cannot exceed the total of five of them.
  MICRONOTES_REQUIRE(entries.front().maxMs <= entries.front().totalMs);
}

// The column that makes the table worth reading: an outer scope that merely
// contains an expensive one must not outrank the expensive one. Ranking by
// total time alone puts every caller above its own hotspot.
MICRONOTES_TEST(trace_channel_charges_nested_time_to_the_inner_scope) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  {
    const TraceScope outer(channel, "outer");
    burn(std::chrono::microseconds(200));
    {
      const TraceScope inner(channel, "inner");
      burn(std::chrono::microseconds(3000));
    }
  }
  const auto entries = channel.snapshot();
  const auto* outer = find(entries, "outer");
  const auto* inner = find(entries, "inner");
  MICRONOTES_REQUIRE(outer && inner);
  // The outer scope contains the inner one, so its total is the larger...
  MICRONOTES_REQUIRE(outer->totalMs >= inner->totalMs);
  // ...and its self time is the smaller, which is what the ranking uses.
  MICRONOTES_REQUIRE(inner->selfMs > outer->selfMs);
  MICRONOTES_REQUIRE(entries.front().label == "inner");
}

// A background scope and a main-thread scope of the same cost are not the same
// problem, and the summary has to be able to tell them apart.
MICRONOTES_TEST(trace_channel_separates_main_thread_time_from_background_time) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  microcore::perf::markMainThread();
  {
    const TraceScope scope(channel, "on.main");
    burn(std::chrono::microseconds(500));
  }
  std::thread worker([&channel] {
    const TraceScope scope(channel, "off.main");
    burn(std::chrono::microseconds(500));
  });
  worker.join();

  const auto entries = channel.snapshot();
  const auto* main = find(entries, "on.main");
  const auto* background = find(entries, "off.main");
  MICRONOTES_REQUIRE(main && background);
  MICRONOTES_REQUIRE(main->mainSelfMs > 0.0);
  MICRONOTES_REQUIRE(background->mainSelfMs == 0.0);
  MICRONOTES_REQUIRE(background->selfMs > 0.0);
  MICRONOTES_REQUIRE(microcore::perf::mainThreadKnown());
}

// Nesting is per thread. Sharing one active-scope pointer across threads would
// let a background scope steal a main-thread scope's self time, which is the
// one number this table exists to report.
MICRONOTES_TEST(trace_channel_keeps_nesting_per_thread) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  {
    const TraceScope outer(channel, "parent");
    std::thread worker([&channel] {
      const TraceScope scope(channel, "sibling");
      burn(std::chrono::microseconds(2000));
    });
    worker.join();
  }
  const auto entries = channel.snapshot();
  const auto* parent = find(entries, "parent");
  const auto* sibling = find(entries, "sibling");
  MICRONOTES_REQUIRE(parent && sibling);
  // The worker's scope is not nested inside the parent, so none of its time was
  // subtracted from the parent's self time.
  MICRONOTES_REQUIRE(parent->selfMs >= parent->totalMs - 0.001);
}

// An externally measured number -- a median the harness computed, a cost that
// spans two callbacks -- belongs in the same ranked table as everything else.
MICRONOTES_TEST(trace_channel_folds_external_samples_into_the_same_table) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  channel.recordSample("computed.elsewhere", 12.5);
  const auto entries = channel.snapshot();
  MICRONOTES_REQUIRE(entries.size() == 1);
  MICRONOTES_REQUIRE(entries.front().calls == 1);
  MICRONOTES_REQUIRE(entries.front().totalMs == 12.5);
  // A leaf: self equals total, so it ranks on what it actually cost.
  MICRONOTES_REQUIRE(entries.front().selfMs == 12.5);
}

MICRONOTES_TEST(trace_channel_sample_is_dropped_while_aggregation_is_off) {
  TraceChannel channel("test", nullptr, nullptr, nullptr);
  channel.recordSample("ignored", 5.0);
  MICRONOTES_REQUIRE(channel.snapshot().empty());
}

// Labels can carry a note id or a block count, so an unbounded table is a leak
// proportional to session length. Past the cap the rest fold into one bucket
// rather than growing forever.
MICRONOTES_TEST(trace_channel_bounds_the_label_table) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  for(std::size_t i = 0; i < TraceChannel::kMaxLabels + 64; ++i) {
    channel.recordSample("label." + std::to_string(i), 0.1);
  }
  const auto entries = channel.snapshot();
  // kMaxLabels distinct labels, plus the one bucket everything after them folds
  // into -- and the bucket's call count is what says how much was folded away.
  MICRONOTES_REQUIRE(entries.size() == TraceChannel::kMaxLabels + 1);
  const auto* overflow = find(entries, "<label-overflow>");
  MICRONOTES_REQUIRE(overflow != nullptr);
  MICRONOTES_REQUIRE(overflow->calls == 64);
}

MICRONOTES_TEST(trace_channel_reset_clears_the_table_but_leaves_it_armed) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  channel.recordSample("before", 1.0);
  channel.resetAggregate();
  MICRONOTES_REQUIRE(channel.snapshot().empty());
  channel.recordSample("after", 1.0);
  MICRONOTES_REQUIRE(channel.snapshot().size() == 1);
}

// The label builder does the string work only when someone is listening. A
// missed guard at a call site is an allocation per call in production, which is
// exactly the cost the tracer is supposed to avoid paying.
MICRONOTES_TEST(scope_label_builds_nothing_while_the_channel_is_off) {
  TraceChannel off("test", nullptr, nullptr, nullptr);
  ScopeLabel label(off, "layout.update");
  label.field("note", "inbox.md").field("blocks", 9612);
  MICRONOTES_REQUIRE(label.view().empty());
}

MICRONOTES_TEST(scope_label_formats_its_fields_when_the_channel_is_on) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  ScopeLabel label(channel, "layout.update");
  label.field("note", "inbox.md").field("blocks", 9612);
  MICRONOTES_REQUIRE(label.view() == "layout.update(note=inbox.md,blocks=9612)");
  // Idempotent: reading it twice must not append a second closing paren.
  MICRONOTES_REQUIRE(label.view() == "layout.update(note=inbox.md,blocks=9612)");
}

MICRONOTES_TEST(scope_label_with_no_fields_is_just_its_base) {
  ArmedChannel armed;
  TraceChannel& channel = armed.channel;
  ScopeLabel label(channel, "page.draw");
  MICRONOTES_REQUIRE(label.view() == "page.draw");
}

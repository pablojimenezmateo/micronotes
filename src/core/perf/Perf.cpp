#include "core/perf/Perf.h"

#include "core/perf/PerformanceCounters.h"

#include <cstdlib>

namespace microcore::perf {

TraceChannel& traceChannel() {
  // Function-local so the environment is read on first use rather than during
  // static initialization of an arbitrary translation unit.
  static TraceChannel channel("perf", "MICROCORE_PERF_TRACE", "MICROCORE_PERF_SUMMARY",
                              "MICROCORE_PERF_TRACE_MIN_MS");
  return channel;
}

TraceChannel& startupChannel() {
  static TraceChannel channel("startup", "MICROCORE_STARTUP_TRACE", "MICROCORE_STARTUP_SUMMARY",
                              nullptr);
  return channel;
}

ScopeLabel::ScopeLabel(TraceChannel& channel, std::string_view base) {
  if(!channel.enabled()) return;
  enabled_ = true;
  text_.assign(base);
}

ScopeLabel& ScopeLabel::field(std::string_view key, std::string_view value) {
  if(!enabled_) return *this;
  text_ += open_ ? ',' : '(';
  open_ = true;
  text_.append(key);
  text_ += '=';
  text_.append(value);
  return *this;
}

ScopeLabel& ScopeLabel::field(std::string_view key, long long value) {
  if(!enabled_) return *this;
  const std::string text = std::to_string(value);
  return field(key, std::string_view(text));
}

std::string_view ScopeLabel::view() {
  if(open_) {
    text_ += ')';
    open_ = false;
  }
  return text_;
}

void dumpAtExit() {
  static const bool once = [] {
    // Touch both channels first: a function-local static registers its
    // destructor when it is constructed, and destructors run before atexit
    // handlers registered earlier. Constructing them here puts their teardown
    // after this handler instead of before it, so the handler still finds them
    // armed.
    traceChannel();
    startupChannel();
    return std::atexit([] { dumpAllOnce(); }) == 0;
  }();
  (void)once;
}

void dumpAllOnce() {
  startupChannel().dumpOnce();
  traceChannel().dumpOnce();
  dumpCountersOnce();
}

}

#pragma once

// micronotes-specific performance counters, injected into the shared core's
// counter table. src/core/perf/PerformanceCounters.h includes this file and
// concatenates MICROCORE_APP_PERF_COUNTERS onto its own list, which is what
// lets the core header stay byte-identical across every app that vendors it.
//
// Same rules as the core list: "<subsystem>.<event>", plural nouns count that
// noun, and every id declared here must be incremented somewhere in src/ or
// ArchitectureTests fails the build.

#define MICROCORE_APP_PERF_COUNTERS(X)                                                 \
  /* --- note library / index -------------------------------------------- */         \
  X(LibraryIndexRebuilds, "library.index_rebuilds")                                    \
  X(LibraryIndexFilesScanned, "library.index_files_scanned")                           \
  X(LibraryIndexRefreshCalls, "library.index_refresh_calls")                           \
  X(LibrarySearchCalls, "library.search_calls")                                        \
  /* --- text rendering --------------------------------------------------- */        \
  X(RenderTextCacheQueries, "render.text_cache_queries")                               \
  X(RenderTextCacheHits, "render.text_cache_hits")                                     \
  X(RenderTextCacheEvictions, "render.text_cache_evictions")                           \
  X(RenderTextRasterizations, "render.text_rasterizations")                            \
  X(RenderTextMeasureCalls, "render.text_measure_calls")                               \
  /* --- frame loop / input ------------------------------------------------ */       \
  X(FrameEventWakes, "frame.event_wakes")                                              \
  X(FramePresents, "frame.presents")                                                   \
  X(FrameRepaintsSkipped, "frame.repaints_skipped")                                    \
  X(InputWheelEvents, "input.wheel_events")                                            \
  X(InputKeyEvents, "input.key_events")                                                \
  X(InputTextEvents, "input.text_events")

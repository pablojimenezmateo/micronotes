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
  X(LibraryIndexFilesReread, "library.index_files_reread")                             \
  X(LibraryIndexRowsDeleted, "library.index_rows_deleted")                             \
  X(LibraryNoteFilesCalls, "library.note_files_calls")                                 \
  X(LibraryDirectoryEntriesVisited, "library.directory_entries_visited")               \
  X(LibrarySearchCalls, "library.search_calls")                                        \
  /* --- text rendering --------------------------------------------------- */        \
  X(RenderTextMeasureCalls, "render.text_measure_calls")                               \
  /* --- status bar --------------------------------------------------------- */     \
  /* Word counts actually walked, against those served from the memo. The walk */    \
  /* used to happen on every frame -- reused reads zero and counts tracks the   */    \
  /* frame count exactly when the memo is broken, which is how it was found.    */    \
  X(StatusWordCounts, "status.word_counts")                                            \
  X(StatusWordCountsReused, "status.word_counts_reused")                                            \
  /* --- frame loop / input ------------------------------------------------ */       \
  X(FrameEventWakes, "frame.event_wakes")                                              \
  X(FramePresents, "frame.presents")                                                   \
  X(FrameRepaintsSkipped, "frame.repaints_skipped")                                    \
  /* What the frames actually cost. presents alone says a frame happened, which */     \
  /* is the same number for a 2 ms frame and a 40 ms one -- so a scroll that     */    \
  /* stutters and one that does not produced identical instrumentation. Divide   */    \
  /* by presents for the mean; over_budget is the count that matters, because a  */    \
  /* mean frame time hides exactly the frames the user notices.                  */    \
  X(FrameDrawMicros, "frame.draw_micros")                                              \
  X(FrameDrawsOverBudget, "frame.draws_over_budget")                                   \
  X(InputWheelEvents, "input.wheel_events")                                            \
  X(InputKeyEvents, "input.key_events")                                                \
  X(InputTextEvents, "input.text_events")                                              \
  /* --- document layout ---------------------------------------------------- */      \
  /* The live surface re-lays the note out once per frame, so everything here is */    \
  /* per-frame cost. update_calls is the rate; the rest say what each call did.  */    \
  X(LayoutUpdateCalls, "layout.update_calls")                                          \
  /* Source bytes copied into the layout's own buffer, and bytes fed through the */    \
  /* per-block cache key hash. Both are O(document) per update and neither is    */    \
  /* visible in any timing: they are the reason a scroll of a 200 KB note costs  */    \
  /* the same as an edit to it. A frame that changed nothing should add zero to  */    \
  /* both, and today adds the whole note to each.                                */    \
  X(LayoutSourceBytesCopied, "layout.source_bytes_copied")                             \
  X(LayoutKeyBytesHashed, "layout.key_bytes_hashed")                                   \
  /* Blocks the update walked, and the subset it had to build because no cached  */    \
  /* layout matched. relaid/update is the number the incremental design exists   */    \
  /* to keep near zero; blocks_walked is what it still costs when it succeeds.   */    \
  X(LayoutBlocksWalked, "layout.blocks_walked")                                        \
  X(LayoutBlocksRelaid, "layout.blocks_relaid")                                        \
  X(LayoutCacheHits, "layout.cache_hits")                                              \
  X(LayoutCacheEvictions, "layout.cache_evictions")                                    \
  /* Updates whose source, geometry and reveal state were byte-for-byte what the */    \
  /* previous update already laid out -- i.e. work that produced the exact same  */    \
  /* answer as last frame. This is the counter that names the scroll problem: on */    \
  /* a pure scroll it should equal the frame count, and every one of those calls */    \
  /* is a whole-document rescan whose result was already in hand.                */    \
  X(LayoutUnchangedUpdates, "layout.unchanged_updates")                                \
  /* Blocks the scanner produced, and visual lines flattened for caret and hit    */   \
  /* testing. flat_lines is rebuilt from scratch per update, so it scales with    */   \
  /* the document rather than with the viewport.                                  */   \
  X(LayoutBlocksScanned, "layout.blocks_scanned")                                      \
  X(LayoutFlatLinesBuilt, "layout.flat_lines_built")                                   \
  /* Fold predicate calls. Answered per block per update, and each answer that is */   \
  /* not the cheap early-out builds a fold key string.                            */   \
  X(LayoutFoldQueries, "layout.fold_queries")                                          \
  /* --- live page surface --------------------------------------------------- */     \
  /* The draw walks every block in the document and tests each against the        */   \
  /* viewport, so blocks_visited scales with the note and blocks_drawn with the   */   \
  /* window. Their ratio is how much of each frame is spent deciding not to draw  */   \
  /* something -- invisible to a profiler, which just reports a hot loop.         */   \
  X(PageDrawCalls, "page.draw_calls")                                                  \
  X(PageBlocksVisited, "page.blocks_visited")                                          \
  X(PageBlocksDrawn, "page.blocks_drawn")                                              \
  X(PageRunsDrawn, "page.runs_drawn")                                                  \
  /* The three decoration passes, each of which is its own full walk of the       */   \
  /* block list on top of the draw's. Counted separately so a fix that gives one  */   \
  /* of them a visible range is visible here rather than lost in a total.         */   \
  X(PageDecorationBlocksVisited, "page.decoration_blocks_visited")                     \
  X(PageCodeChromeBlocksVisited, "page.code_chrome_blocks_visited")                    \
  X(PageFoldControlBlocksVisited, "page.fold_control_blocks_visited")                  \
  /* Bytes the find highlighter scans. It runs std::string::find over the whole    */  \
  /* note on every frame a query is open, so this is document size times frame     */  \
  /* rate -- and it is zero when nothing is being searched, which is why it needs  */  \
  /* its own row rather than folding into the draw.                                */  \
  X(PageFindScanBytes, "page.find_scan_bytes")

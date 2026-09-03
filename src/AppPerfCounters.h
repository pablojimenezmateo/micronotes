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
  /* --- crash recovery ----------------------------------------------------- */      \
  /* Recovery copies posted against recovery copies actually written. A post is  */    \
  /* a memcpy and a notify; a write is two `fsync` barriers, which measured      */    \
  /* 8.7 ms median on this machine and used to happen on the UI thread on every  */    \
  /* keystroke. posts is the typing rate, writes is what the disk saw, and the   */    \
  /* gap between them is the mailbox coalescing a burst into one write of the    */    \
  /* newest text. writes climbing to meet posts means the coalescing stopped     */    \
  /* working and every character is waiting for a barrier again.                 */    \
  X(RecoveryPosts, "recovery.posts")                                                   \
  X(RecoveryWrites, "recovery.writes")                                                 \
  /* --- text rendering --------------------------------------------------- */        \
  /* Measurements asked for, and the ones that had to be shaped. Shaping a run  */     \
  /* costs about a microsecond, and laying out a whole note asks for one per    */     \
  /* word: measure_calls is the demand, cache_hits is how much of it repeats.   */     \
  /* A hit rate that collapses is the signal that the cache is too small or that */    \
  /* something is measuring strings nobody measures twice.                      */     \
  X(RenderTextMeasureCalls, "render.text_measure_calls")                               \
  X(RenderTextMeasureCacheHits, "render.text_measure_cache_hits")                      \
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
  /*                                                                            */    \
  /* draw_micros is the WORK: everything before SDL_RenderPresent. present_micros */   \
  /* is the block inside it, which with vsync on is the refresh interval minus   */    \
  /* the work and so belongs to the display rather than the app. Summing them    */    \
  /* was how a steady 0.4 ms frame reported 8.4 ms on a 120 Hz screen and looked */    \
  /* like a finding.                                                             */    \
  X(FrameDrawMicros, "frame.draw_micros")                                              \
  X(FramePresentMicros, "frame.present_micros")                                        \
  X(FrameDrawsOverBudget, "frame.draws_over_budget")                                   \
  X(InputWheelEvents, "input.wheel_events")                                            \
  X(InputKeyEvents, "input.key_events")                                                \
  X(InputTextEvents, "input.text_events")                                              \
  /* --- shell surfaces ------------------------------------------------------ */      \
  /* The sidebar rebuilds its row list on every frame, and the tree rebuilds     */    \
  /* every row under every open folder to do it -- a path relativisation, a map  */    \
  /* key and a TreeRow per note in the library, whatever the viewport shows.     */    \
  /* rows_built over presents is how many rows a frame pays for; rows_drawn is   */    \
  /* how many it uses.                                                           */    \
  X(SidebarRowsBuilt, "sidebar.rows_built")                                            \
  X(SidebarRowsDrawn, "sidebar.rows_drawn")                                            \
  X(SidebarRowsReused, "sidebar.rows_reused")                                          \
  X(TreeRowsBuilt, "tree.rows_built")                                                  \
  /* Text the shell had to shorten to fit. Each call used to pop one byte at a   */    \
  /* time and re-measure the whole string, so a long title cost dozens of        */    \
  /* shaping passes; measures is what says whether that is still true.           */    \
  X(ShellEllipsizeCalls, "shell.ellipsize_calls")                                      \
  X(ShellEllipsizeMeasures, "shell.ellipsize_measures")                                \
  /* --- document layout ---------------------------------------------------- */      \
  /* The live surface re-lays the note out once per frame, so everything here is */    \
  /* per-frame cost. update_calls is the rate; the rest say what each call did.  */    \
  X(LayoutUpdateCalls, "layout.update_calls")                                          \
  /* Source bytes copied into the layout's own buffer, and bytes fed through the */    \
  /* per-block cache key hash. Both are O(document) per update and neither is    */    \
  /* visible in any timing: they are the reason a scroll of a 200 KB note costs  */    \
  /* the same as an edit to it. A frame that changed nothing should add zero to  */    \
  /* both, and today adds the whole note to each.                                */    \
  /*                                                                             */    \
  /* source_bytes_copied is the bytes of the edit, not of the note: the layout    */    \
  /* keeps its own copy of the buffer, and it is patched over the span the        */    \
  /* prefix/suffix comparison says moved. It used to be the whole note per        */    \
  /* keystroke. bytes_moved is the memmove that an insertion or a deletion drags  */    \
  /* the rest of the buffer through, which is the part that is still O(document)  */    \
  /* -- and is zero for an edit that replaces as many bytes as it removes.        */    \
  /* Bytes `matchEdges` proved unchanged to locate an edit: the prefix that     */    \
  /* matched from the start plus the suffix that matched from the end. On a      */    \
  /* one-character edit in the middle of a note those two sum to about the whole */    \
  /* buffer, which is the price of a caller that hands over a buffer and no      */    \
  /* account of what it did to it. Read against source_bytes_copied: the ratio   */    \
  /* is how much of the note was read to find how little of it moved.            */    \
  X(LayoutEditBytesMatched, "layout.edit_bytes_matched")                                \
  X(LayoutSourceBytesCopied, "layout.source_bytes_copied")                             \
  X(LayoutSourceBytesMoved, "layout.source_bytes_moved")                               \
  X(LayoutKeyBytesHashed, "layout.key_bytes_hashed")                                   \
  /* Blocks the update looked at the *entry* of -- flags, key, cached layout --   */    \
  /* and the subset it had to build because no cached layout matched. relaid per  */    \
  /* update is the number the incremental design exists to keep near zero.        */    \
  /*                                                                             */    \
  /* blocks_walked used to be the block count on every update, because the        */    \
  /* placement was rebuilt from the front each time. It is now the size of the    */    \
  /* edit: a keystroke walks a handful of blocks whatever the note's length, and  */    \
  /* a walked count that tracks layout.blocks again means the patch stopped       */    \
  /* applying and every update is rebuilding the document.                        */    \
  X(LayoutBlocksWalked, "layout.blocks_walked")                                        \
  X(LayoutBlocksRelaid, "layout.blocks_relaid")                                        \
  /* Blocks whose entry was already right and only had to be moved down the page. */    \
  /* A block below an edit that changed a line count is one of these: no flags, no */   \
  /* key, no probe, just a float and an integer added to its position. Zero when   */   \
  /* the edit left its own block the same height, which is most keystrokes.        */   \
  X(LayoutBlocksShifted, "layout.blocks_shifted")                                      \
  /* Which of the two placement paths ran. A patch keeps the standing arrays and   */   \
  /* rewrites the ranges the call can have moved; a rebuild starts from an empty    */  \
  /* page. Rebuilds are the first update, a resize, and new metrics -- so more than */  \
  /* a handful of them outside those means a precondition of the patch is failing.  */  \
  X(LayoutPlacementPatches, "layout.placement_patches")                                \
  X(LayoutPlacementRebuilds, "layout.placement_rebuilds")                              \
  /* Blocks the placement examined and found already correct: the block carried  */    \
  /* over from the standing layout and its flags came out the same, so its key,   */    \
  /* its map entry and the layout behind it all stood. A key is a pure function   */    \
  /* of a block's bytes and fields, never of its offsets, so an edit invalidates  */    \
  /* only the blocks it overlaps -- however far it shifted everything below.      */    \
  /*                                                                             */    \
  /* This used to be most of the document on every update, because the placement  */    \
  /* walked all of it and this was the count that said the walk was cheap. It is  */    \
  /* small now for the better reason: the walk does not reach those blocks at     */    \
  /* all, so what is left here is the margin the dirty ranges are padded with.    */    \
  X(LayoutBlocksKeyReused, "layout.blocks_key_reused")                                 \
  X(LayoutCacheHits, "layout.cache_hits")                                              \
  X(LayoutCacheEvictions, "layout.cache_evictions")                                    \
  /* Times the cache was swept, so evictions/sweeps says how much one sweep     */    \
  /* frees -- which is the frame a window drag stutters on. The ceiling trades   */   \
  /* the two against each other: a lower one sweeps more often and frees less     */  \
  /* each time, for the same total and a smaller spike.                           */  \
  X(LayoutCacheSweeps, "layout.cache_sweeps")                                        \
  /* Updates whose source, geometry and reveal state were byte-for-byte what the */    \
  /* previous update already laid out -- i.e. work that produced the exact same  */    \
  /* answer as last frame. This is the counter that names the scroll problem: on */    \
  /* a pure scroll it should equal the frame count, and every one of those calls */    \
  /* is a whole-document rescan whose result was already in hand.                */    \
  X(LayoutUnchangedUpdates, "layout.unchanged_updates")                                \
  /* Blocks the scanner produced, and visual rows the document wraps into. The  */    \
  /* rows used to be materialised as one record each and scanned linearly; now    */   \
  /* only their per-block prefix sum exists, so this is the size of the index     */   \
  /* rather than the work of building it -- and a row count that starts tracking  */   \
  /* the probe count below is the sign it went back to being a list.              */   \
  X(LayoutBlocksScanned, "layout.blocks_scanned")                                      \
  /* Blocks the partial rescan actually re-derived. The scan resumes just above  */    \
  /* the edit and stops as soon as it agrees with the previous scan again, so     */   \
  /* this is the size of the edit: a couple of blocks per keystroke against the    */  \
  /* ten thousand in a 200 KB note. It reads zero on the paths that scan the whole */  \
  /* buffer -- the first open of a note, and a note replaced wholesale.            */  \
  X(LayoutBlocksRescanned, "layout.blocks_rescanned")                                  \
  X(LayoutVisualRows, "layout.visual_rows")                                            \
  /* Row-index lookups -- where a click landed, which row the caret is on -- and  */   \
  /* the binary-search steps they took. probes/queries should sit near log2 of    */   \
  /* visual_rows (about 14 on a 460 KB note). It was 13,536: both readers of the  */   \
  /* flat line table walked it from the front, so every click and every up-arrow  */   \
  /* paid a pass over every row in the document.                                  */   \
  X(LayoutRowIndexQueries, "layout.row_index_queries")                                 \
  X(LayoutRowIndexProbes, "layout.row_index_probes")                                   \
  /* Fold predicate calls. Answered per block per update, and each answer that is */   \
  /* not the cheap early-out builds a fold key string.                            */   \
  X(LayoutFoldQueries, "layout.fold_queries")                                          \
  /* Updates that resolved no folds at all, because the caller offered no fold    */   \
  /* predicate and the standing resolution was already empty. A note with nothing */   \
  /* collapsed -- which is most notes, most of the time -- then costs no fold     */   \
  /* work on an edit rather than a per-block predicate call and a memcmp of the   */   \
  /* result against itself. Read against update_calls.                            */   \
  X(LayoutFoldResolutionsSkipped, "layout.fold_resolutions_skipped")                   \
  /* Inline markup work inside a relaid block: spans the inline scanner found,  */     \
  /* and content bytes given a per-byte attribute slot to hold their formatting. */    \
  /* attr_bytes is the one to watch -- it is a heap allocation and a zero fill    */   \
  /* per block, sized to the block, so a full layout allocates and clears the     */   \
  /* whole document however little of it is marked up.                            */   \
  X(LayoutInlineSpans, "layout.inline_spans")                                          \
  X(LayoutAttrBytes, "layout.attr_bytes")                                              \
  /* Relaid blocks the inline scanner found no markup in, which take the tokenizer */  \
  /* path that skips the attribute table entirely. plain_blocks against            */  \
  /* blocks_relaid says how much of a document is ordinary prose -- and it is four */  \
  /* blocks in five, which is why the scan's own buffers are held by the caller:    */  \
  /* the byte mask was allocated and zero-filled by every one of these for a scan   */  \
  /* that then found nothing to write in it.                                        */  \
  X(LayoutPlainBlocks, "layout.plain_blocks")                                          \
  /* Relaid blocks whose content holds not one byte that can begin an inline      */   \
  /* construct, so the scan answers "nothing here" from a single table-driven     */   \
  /* pass and never allocates, masks or sorts. Read against plain_blocks: the     */   \
  /* difference is the blocks that carry a `*` or a `[` which turned out not to   */   \
  /* mark anything up, and those are the only ones still paying the full scan for */   \
  /* an empty answer.                                                             */   \
  X(LayoutInlineScanRejects, "layout.inline_scan_rejects")                             \
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
  /* Bytes the find highlighter scans, and matches it draws. The scan used to run  */  \
  /* over the whole note on every frame a query was open -- document size times    */  \
  /* frame rate -- and then built selection rects for every match in the file,     */  \
  /* including the ones nowhere near the window. Now the match list is found once  */  \
  /* per (query, revision) and only the matches inside the visible band are drawn, */  \
  /* so scan_bytes counts one pass per edit and highlights_drawn counts the window */  \
  /* rather than the note. Both are zero when nothing is being searched.           */  \
  X(PageFindScanBytes, "page.find_scan_bytes")                                         \
  X(PageFindHighlightsDrawn, "page.find_highlights_drawn")

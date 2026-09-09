#pragma once

// micronotes-specific performance counters, injected into the core's counter
// table. src/core/perf/PerformanceCounters.h includes this file and
// concatenates MICROCORE_APP_PERF_COUNTERS onto its own list, which is what
// keeps the core header free of app-specific rows.
//
// Same rules as the core list: "<subsystem>.<event>", plural nouns count that
// noun, and every id declared here must be incremented somewhere in src/ or
// ArchitectureTests fails the build.

#define MICROCORE_APP_PERF_COUNTERS(X)                                                 \
  /* --- note library / index -------------------------------------------- */         \
  X(LibraryIndexRebuilds, "library.index_rebuilds")                                    \
  X(LibraryIndexFilesScanned, "library.index_files_scanned")                           \
  X(LibraryIndexRefreshCalls, "library.index_refresh_calls")                           \
  /* Refreshes of one named file against refreshes that had to go and look.     */   \
  /* A save knows which file it wrote, so it costs a stat and -- only if the    */   \
  /* stat moved -- one read and one transaction. index_refresh_calls is the     */   \
  /* other kind: a recursive walk, a stat per note and a read of every row in   */   \
  /* the table, which is what discovering the change costs when nobody said     */   \
  /* what it was. Refresh calls climbing with the typing rate means a save has  */   \
  /* gone back to rescanning the library once a second.                         */   \
  X(LibraryIndexFileRefreshCalls, "library.index_file_refresh_calls")                  \
  X(LibraryIndexFilesReread, "library.index_files_reread")                             \
  X(LibraryIndexRowsDeleted, "library.index_rows_deleted")                             \
  X(LibraryNoteFilesCalls, "library.note_files_calls")                                 \
  X(LibraryDirectoryEntriesVisited, "library.directory_entries_visited")               \
  /* Entries seen under a `files/` directory -- the companion files beside a   */   \
  /* notebook's notes. They ride the same walk as the notes, so this against  */   \
  /* directory_entries_visited says how much of the tree is files rather than */   \
  /* notes; and files_dir_refreshes counts the watcher re-walking one such     */   \
  /* directory on its own, which is what a PDF dropped into a folder should   */   \
  /* cost -- not an index_refresh_call. The two climbing together means the   */   \
  /* narrow path has fallen back to the wide one.                              */   \
  X(LibraryCompanionEntriesVisited, "library.companion_entries_visited")               \
  X(LibraryFilesDirRefreshes, "library.files_dir_refreshes")                           \
  /* Note-list rows read back out of the index instead of off the disk. The    */   \
  /* sidebar's list needs an id, a path, a title, tags and an icon per note,   */   \
  /* and the refresh has just read every changed file and written all five to  */   \
  /* SQLite -- so the list is one statement. It used to be a second recursive  */   \
  /* walk of the library plus an open and a front-matter parse of every note   */   \
  /* in it, microseconds after the refresh read the same files for the same    */   \
  /* fields. Read against note_files_calls: that should now be one walk per    */   \
  /* refresh, and zero per note list.                                          */   \
  X(LibraryNoteRowsSelected, "library.note_rows_selected")                             \
  X(LibrarySearchCalls, "library.search_calls")                                        \
  /* Reads of the open note's front matter off the disk. Everything that wants  */   \
  /* the note's title, tags or icon reads a record instead, so this should be   */   \
  /* one per note opened -- not one per asker, and above all not one per        */   \
  /* library revision. It used to be the latter: the page header, the           */   \
  /* right-hand panel and the save itself each re-read and re-parsed the whole  */   \
  /* note on every autosave. This climbing with the typing rate means something */   \
  /* is asking a question about the open note through the wrong door.            */   \
  X(AppStateOpenNoteReads, "app_state.open_note_reads")                                \
  /* Saves that found the file rewritten under the buffer since it was read.    */   \
  /* Each one filed the version that was on disk beside the note rather than    */   \
  /* overwriting it, so this is the count of external changes rescued. Nonzero  */   \
  /* with no sync daemon or second editor in play means micronotes has lost     */   \
  /* track of a file it wrote itself.                                            */   \
  X(AppStateSaveConflicts, "app_state.save_conflicts")                                 \
  /* --- the library watcher ----------------------------------------------- */      \
  /* Note files the watcher named and the app re-indexed, against the times it  */   \
  /* could not name them and the whole library had to be re-read. A rescan is   */   \
  /* the kernel's event queue overflowing, a watched directory being moved      */   \
  /* away, or a tree bigger than the watch budget -- all real, all rare. If     */   \
  /* rescans stop being rare the watcher has stopped paying for itself, because */   \
  /* the point of naming paths is that one changed file costs one file's work.  */   \
  X(WatcherPathsApplied, "watcher.paths_applied")                                     \
  X(WatcherRescans, "watcher.rescans")                                                \
  /* --- crash recovery ----------------------------------------------------- */      \
  /* Recovery copies posted against recovery copies actually written. A post is  */    \
  /* a memcpy and a notify; a write is two `fsync` barriers, which measured      */    \
  /* 1.1 ms median on ext4 here -- 8.7 ms with the disk busy -- and used to run   */    \
  /* on the UI thread on every keystroke. posts is the typing rate, writes is    */    \
  /* what the disk saw, and the gap between them is the mailbox coalescing a     */    \
  /* burst of typing into one write of the newest text. writes climbing to meet  */    \
  /* posts means the coalescing stopped working and every character is waiting   */    \
  /* for a barrier again.                                                         */    \
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
  /* --- frame loop / input ------------------------------------------------ */       \
  X(FrameEventWakes, "frame.event_wakes")                                              \
  /* How often the frame stopped or started waiting for the display. Two per */  \
  /* resize drag: off when it begins, on when it settles. A number that grows */  \
  /* with the frame count means something is calling a resize a frame.        */  \
  X(FramePacingChanges, "frame.pacing_changes")                                          \
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
  /* Visible-band lookups over the row list, and the binary-search steps they  */    \
  /* took. Both readers of the list share them: the draw asks once a frame and  */    \
  /* the hit test asks once per mouse-motion event, which is the hundred-a-     */    \
  /* second one. Each used to walk every row -- 400 float compares to draw      */    \
  /* thirteen on a 400-note library, and O(library) per frame as the library    */    \
  /* grows. probes/queries should sit near 2*log2(rows).                        */    \
  X(SidebarRowRangeQueries, "sidebar.row_range_queries")                               \
  X(SidebarRowRangeProbes, "sidebar.row_range_probes")                                 \
  X(SidebarRowsReused, "sidebar.rows_reused")                                          \
  /* Search snippets trimmed to the sidebar's column. One is ~0.25 ms: it       */    \
  /* measures the whole matching line and then bisects, and every probe is a    */    \
  /* string nothing has measured before, so the measure cache cannot help. This */    \
  /* is the counter that says whether the trimming is costing the viewport or   */    \
  /* the result list -- against rows_drawn it should be a few per row for the   */    \
  /* first frame of a query and zero thereafter, and against a 200-result       */    \
  /* query's 600 lines it must never be all of them in one frame.               */    \
  X(SidebarSnippetsTrimmed, "sidebar.snippets_trimmed")                                \
  /* Shaping passes those trimmings paid for. Read against snippets_trimmed:    */   \
  /* it was around eighteen a snippet, because both bisections started from the */   \
  /* whole line and worked down. The full-line measurement the early-out already*/   \
  /* takes gives an advance per byte, so the fitting length is a division and   */   \
  /* the search only has to confirm it -- three or four probes a snippet, near  */   \
  /* the answer's own length rather than the line's.                            */   \
  X(SidebarSnippetMeasures, "sidebar.snippet_measures")                                \
  X(TreeRowsBuilt, "tree.rows_built")                                                  \
  /* The right panel's two memos: derivations performed against derivations   */    \
  /* served from the cache. Every one of its three views was rebuilt per frame, */    \
  /* and each was expensive differently -- the outline scanned the whole note   */    \
  /* for headings, the backlinks ran a SQLite query, and the tags read the note */    \
  /* back off disk. Read against frame.presents: outline_builds should track    */    \
  /* the typing rate and library_builds should be near zero, and either of them */    \
  /* tracking the frame count means its memo key has stopped discriminating and */    \
  /* the panel is costing more per frame than the note beside it does.          */    \
  X(RightPanelOutlineBuilds, "right_panel.outline_builds")                             \
  X(RightPanelOutlineReused, "right_panel.outline_reused")                             \
  /* Where a rebuild got its block partition. The whole cost of an outline      */    \
  /* rebuild is in the scan, so these two split "rebuilt" into the cheap case   */    \
  /* and the expensive one -- without them a build is a build and the borrow    */    \
  /* could stop working with nothing to show it.                                */    \
  X(RightPanelOutlineBlocksBorrowed, "right_panel.outline_blocks_borrowed")            \
  X(RightPanelOutlineScans, "right_panel.outline_scans")                              \
  X(RightPanelLibraryBuilds, "right_panel.library_builds")                             \
  X(RightPanelLibraryReused, "right_panel.library_reused")                             \
  /* The palette's filter: runs against calls served from the standing answer,  */    \
  /* and items scored, which is the actual work. Filtering is a fuzzy score over */    \
  /* every item plus a sort, and it used to run from scratch at every call site  */    \
  /* -- the layout, the draw, each arrow key, each wheel notch -- so one frame   */    \
  /* of an open "Go to note" scored the whole library at least twice. Read       */    \
  /* items_scored against frame.presents: with a palette open and nothing being  */    \
  /* typed it should stay flat, and it climbing with the frame count means the   */    \
  /* query key has stopped discriminating.                                       */    \
  X(OverlayFilterRuns, "overlay.filter_runs")                                          \
  X(OverlayFilterReused, "overlay.filter_reused")                                      \
  X(OverlayFilterItemsScored, "overlay.filter_items_scored")                           \
  /* Text the shell had to shorten to fit. Each call used to pop one byte at a   */    \
  /* time and re-measure the whole string, so a long title cost dozens of        */    \
  /* shaping passes; measures is what says whether that is still true.           */    \
  X(ShellEllipsizeCalls, "shell.ellipsize_calls")                                      \
  X(ShellEllipsizeMeasures, "shell.ellipsize_measures")                                \
  /* The menu bar's geometry. Laid out by the paint, by the hit test and by the  */    \
  /* cursor-shape pass, so a single pointer motion over the window asks three    */    \
  /* times -- which is why one function answers the whole bar rather than three   */   \
  /* answering a third of it each. `label_measures` is the one that matters: the */    \
  /* labels are static and the table is fixed, so the memo behind them should    */    \
  /* miss once at startup and again only when the text size moves. A count that  */    \
  /* tracks `layouts` means the probe stopped discriminating and every motion    */    \
  /* event is re-shaping six strings for nothing.                                */    \
  X(MenuBarLayouts, "menu.bar_layouts")                                                \
  X(MenuBarLabelMeasures, "menu.bar_label_measures")                                   \
  X(MenuPopupLayouts, "menu.popup_layouts")                                            \
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
  /* ...and how many of those the comparison actually had to read. Without a     */    \
  /* caller's span the two are the same number, and the gap between them is the  */    \
  /* note the caller saved being read. This is the one to watch: matched is the   */    \
  /* window's size, which a claim establishes without touching a byte.           */    \
  X(LayoutEditBytesCompared, "layout.edit_bytes_compared")                             \
  /* Updates whose caller said where it had edited, against updates that had to  */    \
  /* find out by comparing. edit_bytes_matched read fourteen bytes for every one */    \
  /* that moved -- two memcmp passes summing to the length of the note, to       */    \
  /* locate one typed character -- and a claim turns that into the length of the */    \
  /* edit. spans_compared should be the updates a claim cannot cover: the first   */    \
  /* layout of a note, an unstamped caller, and a frame that handled two         */    \
  /* keystrokes at once.                                                          */    \
  X(LayoutEditSpansUsed, "layout.edit_spans_used")                                     \
  X(LayoutEditSpansCompared, "layout.edit_spans_compared")                             \
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
  /* Caret rectangles asked for, and the binary-search steps they took. The     */   \
  /* caret is drawn once a frame and its block used to be walked row by row and  */   \
  /* run by run, which is free for a paragraph and 7.1 us for a caret at the end */   \
  /* of a 4,000-line fence -- one `SourceBlock` holding thousands of rows. Both  */   \
  /* the run and its owning line are found by partition point now, so probes     */   \
  /* should sit near log2(runs) + log2(rows) rather than near their sum.         */   \
  X(LayoutCaretQueries, "layout.caret_queries")                                        \
  X(LayoutCaretProbes, "layout.caret_probes")                                          \
  /* Fold predicate calls. Answered per block per update, and each answer that is */   \
  /* not the cheap early-out builds a fold key string.                            */   \
  X(LayoutFoldQueries, "layout.fold_queries")                                          \
  /* Updates that resolved no folds at all, because the caller offered no fold    */   \
  /* predicate and the standing resolution was already empty. A note with nothing */   \
  /* collapsed -- which is most notes, most of the time -- then costs no fold     */   \
  /* work on an edit rather than a per-block predicate call and a memcmp of the   */   \
  /* result against itself. Read against update_calls.                            */   \
  X(LayoutFoldResolutionsSkipped, "layout.fold_resolutions_skipped")                   \
  /* Blocks a resolution actually walked. The resolution resumes at the seam of  */    \
  /* an edit rather than restarting at the top of the note, so this against      */    \
  /* layout.blocks is what the resumption is worth on a note that does have a    */    \
  /* fold in it -- the case fold_resolutions_skipped cannot help.                */    \
  X(LayoutFoldBlocksResolved, "layout.fold_blocks_resolved")                           \
  /* Images whose box the layout asked the renderer for. One per picture per   */      \
  /* relaid block, so on an idle frame it should be zero: a note full of       */      \
  /* photographs that measures them every frame is measuring the disk.         */      \
  X(LayoutImagesMeasured, "layout.images_measured")                                    \
  /* Image targets turned into a file on disk, and the times that answer came   */     \
  /* out of the memo instead. Resolving one canonicalises the library root and  */     \
  /* the candidate -- a stat per path component, twice -- so on a note full of  */     \
  /* pictures `resolved` should be the number of distinct targets in it and     */     \
  /* nothing like the number of times they are laid out.                        */     \
  X(ImagePathsResolved, "image.paths_resolved")                                        \
  X(ImagePathsReused, "image.paths_reused")                                            \
  /* The texture behind a picture: decoded, served from the cache, or dropped    */    \
  /* to stay inside the byte budget. `loaded` should be the number of distinct   */    \
  /* pictures a session has shown; if it tracks the frame count instead, the     */    \
  /* budget is thrashing and every eviction is a decode away from being one.     */    \
  X(ImageTexturesLoaded, "image.textures_loaded")                                      \
  X(ImageTextureCacheHits, "image.texture_cache_hits")                                 \
  X(ImageTexturesEvicted, "image.textures_evicted")                                    \
  /* The md4c parse of a block the live scanner does not model: served from the */     \
  /* cache, dropped by a sweep, and how often a sweep ran. Read `reused` against */     \
  /* `markdown.parse_calls` -- a note whose tables are re-parsed on every        */    \
  /* relayout has a cache that is being defeated rather than one that is small.  */    \
  X(ComplexParsesReused, "complex.parses_reused")                                      \
  X(ComplexParsesEvicted, "complex.parses_evicted")                                    \
  X(ComplexCacheSweeps, "complex.cache_sweeps")                                        \
  /* Tokens a block is staged into before it is flowed into runs. Read against  */     \
  /* the `layout.block.stage` timer: the staging vector exists only to be read   */    \
  /* once, in order, by one consumer, so this is the size of the buffer a        */    \
  /* streaming tokenizer would not build.                                        */    \
  X(LayoutTokensStaged, "layout.tokens_staged")                                        \
  /* What the line breaker itself does, which nothing counted -- it is the      */     \
  /* innermost loop of laying a document out and the only instrument on it was  */     \
  /* a timer. `flow_measures` is how many times it asked the font for a width,  */     \
  /* which is where a flow change's cost actually lands: shaping dominates, so   */    \
  /* measuring the same run twice is the regression this catches and a count is  */    \
  /* the only thing that shows it. Roughly one per token emitted; if it climbs   */    \
  /* toward two, something is measuring on both the fits-it and the emits-it     */    \
  /* path again, which is exactly what held-back spaces used to do.              */     \
  X(LayoutFlowMeasures, "layout.flow_measures")                                        \
  /* Words broken mid-word, because even one token did not fit the column. Very */     \
  /* nearly zero on prose -- a URL in a narrow pane is the honest case -- so a   */    \
  /* non-trivial number here is a column being computed too narrow somewhere,    */    \
  /* which otherwise shows up only as text that looks subtly wrong.              */     \
  X(LayoutFlowWordSplits, "layout.flow_word_splits")                                    \
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
  /* Find matches the page builds a selection rect for. The highlighter used to    */  \
  /* build one for every match in the file, however far off screen, on top of a     */  \
  /* `std::string::find` pass over the whole note per frame -- so an open find bar   */  \
  /* cost document size times frame rate. The scan is gone from here entirely (the  */  \
  /* shell owns one match list, see `search.text_scans`) and only the matches       */  \
  /* inside the visible band get a rect, so this counts the window rather than the  */  \
  /* note. Zero when nothing is being searched.                                     */  \
  X(PageFindHighlightsDrawn, "page.find_highlights_drawn")                             \
  /* --- the status bar ------------------------------------------------------- */    \
  /* Passes over the note the bar's two derived readouts cost: the caret's line   */   \
  /* and column, and the size of a selection. Both are memoised on the buffer's   */   \
  /* revision and the offsets, so scans should track caret moves rather than      */   \
  /* frames -- the bar is repainted by a scroll, a hover and the caret's own      */   \
  /* blink, and none of those move either answer. A regression to per-frame shows */   \
  /* up here as scans climbing with time rather than with typing.                 */   \
  X(StatusTextScans, "status.text_scans")                                              \
  X(StatusTextScanBytes, "status.text_scan_bytes")

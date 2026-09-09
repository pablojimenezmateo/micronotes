# Tasks

## 1. The rule and the walk

- [x] 1.1 `kFilesDirName`, `isFilesDir`, `insideFilesDir`, `filesRootOf` in
      `src/library/Library.{h,cpp}`.
- [x] 1.2 `Library::walk` third output, classified by iterator depth; nothing
      under a files directory reaches the note or directory lists.
- [x] 1.3 `Library::walkFilesDir` for one directory; `moveCompanion` and
      `deleteCompanion` primitives.
- [x] 1.4 `LibraryIndex::companions()`, `OrganizationService::companions()`,
      `NoteCatalog::companions()` / `searchCompanions()`.
- [x] 1.5 Counters `library.companion_entries_visited` and
      `library.files_dir_refreshes`.

## 2. Catalog writes and guards

- [x] 2.1 `NoteCatalog::renameCompanion`, `moveCompanion`, `deleteCompanion`,
      `createCompanionFolder`, each ending in `refreshFilesDir`.
- [x] 2.2 `createNote`, `moveNote`, `createFolder`, `renameFolder` refuse a
      files area; `AppState::moveFolderInto` refuses a files-area parent.

## 3. Tree and sidebar

- [x] 3.1 `TreeRowKind::FilesFolder` / `File`, `TreeRow::file`, and the order:
      sub-notebooks, `files/`, notes.
- [x] 3.2 `ui::drawFileGlyph`; the two kinds drawn in the sidebar.
- [x] 3.3 Activation: click or Enter opens through `ui.launcher`; the cursor
      never does; a files directory only unfolds.
- [x] 3.4 Search: name matches under a "N files" caption as `File` rows.
- [x] 3.5 Drag: `SidebarDrag::file`, `SidebarDropTarget::filesDir`, and the
      release handler's refusals for notes and notebooks.
- [x] 3.6 Menus and prompts: file menu, files-folder menu, rename, new folder,
      move palette, delete confirm; `companionTarget` carries the row.

## 4. Watcher

- [x] 4.1 `applyWatchedChanges` routes paths under a files directory to
      `refreshFilesDir`, once per directory.

## 5. Docs and tests

- [x] 5.1 `docs/library-format.md` Companion Files section.
- [x] 5.2 Tests: the walk's classification, the rule, trash round trip, move
      refusals, tree order, name-only search, click-vs-cursor, drop targets,
      the FILES caption, the narrow watcher refresh, the catalog guards.

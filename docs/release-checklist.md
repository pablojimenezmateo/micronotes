# Release checklist

Cutting a micronotes release tag (`vX.Y.Z`). Linux x86_64 is the only validated
host; release notes say so if that ever changes.

## One-command driver

`tools/release.sh <version>` performs the whole procedure below. It is
**local-only by default** -- bump, build, test gate, package, clean-system
verification, checksum, GPG-sign, then stop -- and
`tools/release.sh <version> --publish` additionally commits, tags, pushes, and
creates the GitHub release. The numbered steps are the canonical reference the
script implements; run them by hand only if you need to deviate.

To ship a curated changelog in a single `--publish` run instead of the
auto-drafted `<!-- TODO -->` section, write the section **body** to a file and
pass `--changelog-file <file>`. The script inserts it under a generated
`## [version] - date` header, so the version and the date stay authoritative
rather than being curated into disagreeing with the artifact:

```sh
tools/release.sh 0.7.0 --publish --changelog-file notes.md
```

## Standard release procedure

When a release is requested ("do a release", "cut a release", "release vX.Y.Z"),
perform **all** of these in order. None is optional -- every published release
carries the `.deb`, its checksum and both signatures, so there is a packaged
install path that a user can verify.

1. **Bump the version.** `project(micronotes VERSION ...)` in `CMakeLists.txt`
   is the single source of truth; refresh the `vX.Y.Z` and
   `micronotes_X.Y.Z_amd64.deb` references in `README.md` to match.
2. **Update `CHANGELOG.md`.** A new dated section for the version, with grouped
   changes derived from `git log <previous-tag>..HEAD`.
3. **Check doc/version consistency.** `tools/check-doc-versions.sh` must pass.
   It is a hard gate in the driver, run both after the bump and again
   immediately before publishing.
4. **Build.** `cmake -S . -B build-release-package -DCMAKE_BUILD_TYPE=Release`
   and build it. Confirm the version reached the artifact:
   `./build-release-package/bin/micronotes --version`.
   **No LTO.** It was measured and is a mixed result rather than a win
   (`docs/performance.md`, "The tenth pass"); the hot per-token, per-block and
   per-frame units live in headers instead. Turning it on for a release would
   ship codegen no perf run has measured.
5. **Test gate.** `tools/run-checks.sh release` against that tree, so what is
   tested is what gets signed rather than a Debug tree that merely shares a
   commit.
6. **Package.** `cpack -G DEB` from the build tree, producing
   `micronotes_X.Y.Z_amd64.deb`.
7. **Verify the package starts on a clean system.**
   `scripts/ci/verify-deb-runtime.sh <deb>` -- see the gate below. The driver
   runs this *before* signing and refuses to sign a package that fails it.
8. **Checksum.** `sha256sum micronotes_X.Y.Z_amd64.deb > micronotes_X.Y.Z_amd64.deb.sha256`.
9. **GPG-sign.** Sign the package and its checksum with the maintainer release
   key (`pablojimenezmateo@gmail.com`, fingerprint
   `0E32 39B7 1B0F 9598 B71A FB7B 6D33 9CCB FC51 5D70`):
   `gpg --detach-sign --armor micronotes_X.Y.Z_amd64.deb` and the same for the
   `.sha256`. The exported public key ships with every release as
   `micronotes-signing-key.asc`.
10. **Commit and tag.** Commit the version/changelog changes, create a
    **signed** annotated tag (`git tag -s vX.Y.Z`), push `main` and the tag.
11. **Create the GitHub release** and attach the `.deb`, the `.sha256`, both
    `.asc` signatures, and the public key.
12. **Verify.** `gh release view vX.Y.Z --json assets` lists all five. Round-trip
    the signature locally:
    `gpg --verify micronotes_X.Y.Z_amd64.deb.asc micronotes_X.Y.Z_amd64.deb`.

## The clean-system gate

**The single most important item here**, and the one a green test suite says
nothing about.

micronotes links `libSDL3.so.0` and `libSDL3_ttf.so.0`. No Debian-family distro
packages SDL3, so it is a from-source install under `/usr/local` on every
machine that can build this project -- and `dpkg-shlibdeps`, which is what
`CPACK_DEBIAN_PACKAGE_SHLIBDEPS` delegates to, can only emit a `Depends:` for a
library an installed `.deb` owns. It maps those two sonames to nothing and says
nothing about it.

Without the runtime-library bundling block in `CMakeLists.txt`, the result is a
package that `apt install ./micronotes.deb` accepts -- every listed dependency
genuinely is satisfied -- whose binary then dies in the loader on any machine
but the one that built it. microide shipped exactly that package for twenty
releases, because it built, tested and signed the artifact without ever
installing it and launching it.

So: `scripts/ci/verify-deb-runtime.sh` extracts the package, checks that every
`NEEDED` soname resolves from either the package's own `$ORIGIN` RUNPATH or a
stock system directory (**`/usr/local` deliberately excluded**), and then
launches the packaged binary with the ld.so cache inhibited and the library path
restricted to stock directories. Never sign a package built some other way to
skip it.

## Pre-tag engineering

- [ ] `tools/run-checks.sh tests` green
- [ ] `tools/run-checks.sh clang-build` green (second compiler, `-Werror`)
- [ ] `tools/run-checks.sh perf` green -- no scenario over budget
- [ ] `tools/run-checks.sh asan` / `ubsan` / `tsan` green
- [ ] `tools/check-doc-versions.sh` green
- [ ] **The built `.deb` starts on a clean system** (above)
- [ ] `micronotes --version` reports the version being cut
- [ ] Docs that changed this cycle regenerated or updated: `docs/build.md`,
      `docs/library-format.md`, `docs/markdown-elements.md`
- [ ] `docs/tech-debt.md` reflects what actually shipped -- debts closed this
      cycle removed, debts opened recorded

## Tag and artifacts

- [ ] `CHANGELOG.md` has the version, the date, and grouped changes
- [ ] Signed annotated tag (`git tag -s vX.Y.Z`) on the release commit
- [ ] Release notes stating scope, known limitations, and the verify command
- [ ] SHA256 checksum for the published `.deb`
- [ ] Detached GPG signatures (`.asc`) for the `.deb` and its checksum, plus
      `micronotes-signing-key.asc`
- [ ] Build-from-source instructions reachable (`docs/build.md`)

## Tested workflows matrix

Mark each row **pass** / **fail** / **n/a** on the release candidate build.
These are the paths that touch a user's files, so a regression here is data
loss rather than an annoyance.

| # | Workflow | Steps (abbreviated) | Result |
|---|----------|---------------------|--------|
| 1 | Open a library | Start on a folder of `.md` files | |
| 2 | Create and save a note | New note, type, autosave settles | |
| 3 | Rename a note | Rename from the header; file on disk follows | |
| 4 | External edit | Change a note in another editor; both versions kept | |
| 5 | Delete and restore | Delete to the library trash, then undo | |
| 6 | Attachments | Attach a file; it lands beside the note | |
| 7 | Search the library | `Ctrl+Shift+F`, open a result, query carried in | |
| 8 | Tags | Tag a note, filter by it, recolour the tag | |
| 9 | Export PDF | Export a note; the PDF opens | |
| 10 | Views | `Ctrl+2` / `Ctrl+3` / `Ctrl+4` round-trip cleanly | |
| 11 | Restart | Session, tabs and pane arrangement come back | |

## Crash and data-loss reporting

Publish in the release notes:

1. Open an issue with distro, version (`micronotes --version`), and steps.
2. State whether data loss involved unsaved buffers, an external edit, or the
   recovery store.
3. Note that notes are plain files: the library folder is the record, and a
   copy of it before reporting costs nothing.

## Post-tag

- [ ] `docs/tech-debt.md` updated if a debt shipped closed
- [ ] Archive any OpenSpec change that shipped with this tag (move it under
      `openspec/changes/archive/`)

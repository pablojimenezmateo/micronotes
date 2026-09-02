## Context

micronotes reads and writes ordinary Markdown files in one local folder. That
is the premise, and it means other programs -- an editor, a sync client, a
script, `git checkout` -- legitimately touch those files while the application
is running. Today micronotes does not notice: the SQLite index and the sidebar
reflect whatever was true at the last `Ctrl+R`.

Interface colours have the same shape of problem from the other side: they are
literals in `src/ui/Theme.cpp`, so the only way to change them is to rebuild.

The wider `microide-port` plan these two came from is otherwise built; see
`proposal.md` for what landed where. The two decisions below are the ones that
survived it, plus the concurrency decision that governs whether the conditional
third capability is ever started.

## Goals / Non-Goals

**Goals**

- Detect external changes to the library without the user asking.
- Never lose unsaved edits to a reconciliation, under any interleaving.
- Let a user supply interface colours as data.
- Keep idle CPU at zero. An app that watches a folder must not spin to do it.

**Non-Goals**

- Merging. A conflict is reported, not resolved: micronotes is not a
  three-way merge tool and a wrong merge of someone's notes is worse than a
  message.
- Executable themes. A theme file is colour data. Anything that can run is a
  plugin, and `product-vision` excludes plugins.
- Concurrency for its own sake. The executor is undertaken only against
  measurements from `src/core/perf/`, not on the assumption that it is needed.

## Decisions

### The file watcher ships synchronously first

V1 reuses the existing idle path: feed the watcher's next-poll delay into the
`SDL_WaitEventTimeout` call the main loop already makes, and poll when the wait expires. No threads.
Idle CPU stays at zero because the loop still blocks. V2, after Phase 3, moves the tree walk onto
the background executor and wires the wake callback to the checked SDL wake path.

*Alternatives considered:* port microide's threaded watcher wholesale in Phase 1 — rejected, it
drags the entire concurrency spine forward into a phase that does not otherwise need it. Native
inotify from the start — rejected for V1; polling at 750 ms is imperceptible for notes and the
native backend is the larger part of the port.

### Reconciliation policy is the design work, and it is new

microide has no reconciliation policy worth copying: its buffers hold code, with different stakes
and a git safety net. The policy defined in `external-change-reconciliation` is written for notes:
silent reload when nothing is at risk, caret-preserving reload when the buffer is clean, and an
absolute refusal to overwrite unsaved edits. An open note deleted on disk keeps its buffer and can
be written back, because losing an open note to an external `rm` is not an acceptable outcome for a
notes application.

Conflicts are reported on the existing status line rather than through a notification system.
micronotes has no toast infrastructure, this is the only conflict class that needs reporting, and
adding a notification service to carry one message would be the wrong trade.

*Alternative considered:* prompt modally on conflict. Rejected — a modal that appears because a sync
client touched a file interrupts writing, which is the one thing the application exists to protect.


### Concurrency is conditional, and its wake path is the risky part

The executor defaults to a single worker with FIFO semantics and cancellation, which matches how
micronotes will use it: a superseded search should be cancelled, not raced. Results return through a
mailbox whose per-key coalescing replaces a stale queued result rather than queueing behind it.

The wake path deserves more care than it appears to. A bare `SDL_PushEvent` is fire-and-forget, and
micronotes blocks in `SDL_WaitEventTimeout`, so a rejected push strands a completed result until
something unrelated wakes the loop. The checked wake latches an "owed" bit that the idle wait
consumes to shorten its next timeout, and a failed event-type registration at startup degrades the
loop to a bounded wait rather than an indefinite block.

Posted closures carry extracted data, never a live handle into worker-owned state. This is stated as
a rule because it is the failure mode that sanitizers catch late and reviews catch never.

*Alternative considered:* skip Phase 3 entirely. This remains a live option — see Open Questions. It
is the only item that makes the codebase materially harder to reason about, and its benefit is
entirely a function of library size.

### Themes derive into the existing struct rather than replacing it

The hard part of theming — every drawn colour coming from a named semantic role — is already done.
The design adds a parser and a derivation layer in front of `Theme`, and changes no call site. The
built-in light and dark palettes become theme definitions run through the same derivation, so a
built-in is not a special case and the shared path is exercised by default rather than only by users
who install a theme.

Contrast policy differs by origin: floors are a hard test failure for built-ins and a warning for
user themes. Rejecting a user's theme for a contrast ratio would be the application overruling
someone about their own screen; reporting which role failed gives them what they need without it.

*Alternative considered:* port microide's file format unchanged, including its syntax-highlight
group names. Rejected — those groups describe code tokens. The role mapping to micronotes' UI
surfaces is genuinely new work and is the substance of this item.


## Risks / Trade-offs

**A polling watcher can miss a change that lands inside its interval.** It
compares state rather than consuming events, so it converges: a change missed
at one poll is seen at the next. What it cannot do is order two changes within
one interval, which is why reconciliation is written against the file's
observed state and not against a change log.

**Reconciliation touches the one thing the app must never lose.** Every path
that reloads a buffer is gated on the buffer being clean. The dirty case has
exactly one behaviour -- keep the edits, say so on the status line -- and that
is a rule to test rather than a judgement to make per site.

**A user theme can produce something unreadable.** Contrast floors are checked
and reported rather than enforced, for user themes. This is a deliberate
asymmetry with built-ins, which fail their tests: the application should not
overrule someone about their own screen, but it also should not ship a palette
nobody can read.

## Open Questions

- Poll interval. 750 ms is proposed as imperceptible for notes without being
  costly; it is a number to measure, not a conclusion.
- Whether a native `inotify` backend is worth it after V1 ships, or whether
  polling is simply sufficient at the scale of a notes library.
- Whether the conditional concurrency work is ever started. The measurement
  that would decide it now exists; the decision does not.
- Where user theme files are discovered from, and whether a theme names its
  base (`include: dark`) or restates every role.

# Tech debt

Known debt, each with a stable id so a comment in the code can point at it and a
commit message can close it. One entry per thing, and every entry says what it
costs today and why it has not been paid — an entry with no cost and no reason
is a preference, not debt, and does not belong here.

Performance debt that is part of a measured narrative lives in
`docs/performance.md` under its `### Open:` headings; those are cross-referenced
here rather than duplicated, because that file carries the numbers and the
history that make them make sense.

**Adding an entry:** take the next free number, never reuse one. Numbers up to
TD-36 have been used. Closing an entry means deleting it and saying so in the
commit; a register of things that turned out to be fine is a register nobody
reads.

---

## Nothing is open

The register is empty, and that is a claim rather than an oversight: every entry
it held has been closed by a commit that says so, and `docs/performance.md` has
no `### Open:` heading left either.

It will not stay empty, and the thing to resist when it stops being empty is
writing down a *preference*. An entry earns its place by saying what it costs
today -- with a number, if the cost is one that has a number -- and why that has
not been paid. Something that fails either half is not debt; it is either a
thing to do now or a thing nobody has to know about.


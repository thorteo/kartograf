# 0005. Per-component snapshot stores

**Status:** Accepted — amends 0004
**Date:** 2026-09-21

## Context

ADR 0004 said all shared mutable state would live in `RaidState` and nowhere else.
That has not held, and pretending otherwise would make the ADR a liability: a future
reader would trust a rule the code already breaks.

Since 0004, several components have grown their own cross-thread state, each for a
good reason: `TileStore` and `SvgMapStore` (worker threads rasterise and decode,
the render thread uploads), `TarkovDataRepository` (downloads in the background), and
`QuestProgressStore` (the log watcher writes, the UI reads and also writes manual
corrections). Folding these into `RaidState` would couple unrelated lifetimes — raid
state resets on every map load, quest progress must survive forever.

## Decision

Keep the *principle* of 0004 and drop the *location* rule.

Each component that crosses threads:

1. owns its state behind **exactly one mutex**;
2. publishes to readers as an immutable snapshot (`shared_ptr<const T>`) or copies
   values out under the lock — readers never hold a reference into guarded state;
3. **never calls into another component while holding its own lock**, so no lock
   ordering exists to get wrong;
4. invalidates in-flight background work with a generation counter rather than by
   cancelling it, so a result for a superseded request is discarded, never applied.

`RaidState` remains the single home for raid state specifically.

## Consequences

- Each store is independently testable under ThreadSanitizer; the ones in `src/core`
  are (`RaidState`, `QuestProgressStore`).
- Rule 3 is the one to police in review. It is what keeps several locks as safe as one.

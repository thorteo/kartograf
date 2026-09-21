# 0004. Single guarded state, snapshot to renderer

**Status:** Accepted, amended by [0005](0005-per-component-snapshot-stores.md)
**Date:** 2026-09-21

## Context

`0001` chose C++, accepting that the compiler will not catch data races. This
application is inherently concurrent:

- a watcher thread tailing game log files
- a watcher thread observing the screenshot directory
- a small pool fetching map tiles and API data over HTTP
- the render thread

A data race here does not produce a subtle wrong pixel. It produces a crash or garbage
on screen **in the middle of a raid**, which is the worst possible moment and the
hardest case to reproduce afterwards. This was the strongest argument against C++
during stack selection, so the design has to answer it deliberately.

## Decision

**One owner, one lock, immutable snapshots.**

1. All shared mutable state lives in a single object, `core/game/RaidState`. There are
   no other cross-thread mutable fields anywhere in the application.
2. `RaidState` is guarded by one mutex. No nested locks, so no lock ordering to get
   wrong.
3. Worker threads never mutate `RaidState` directly. They push immutable event objects
   onto a queue.
4. A single consumer drains the queue and applies events to `RaidState`. It is the only
   writer.
5. The render thread never locks for the duration of a frame. It takes a cheap
   copy-on-write snapshot (`std::shared_ptr<const RaidStateSnapshot>`) once per frame
   and reads only that. A frame therefore always sees one coherent state, never a torn
   mix.

Rules that follow, and are enforced in review:

- No `static` or global mutable state. None.
- Anything crossing a thread boundary is either immutable or passed by value.
- Worker threads own their resources and do not share them.
- No raw `std::thread` that outlives its owner; threads are joined deterministically on
  shutdown, and every watcher supports cancellation.
- If a new piece of shared state seems necessary, it goes into `RaidState`. If it
  genuinely cannot, that needs a superseding ADR.

## Options rejected

**Lock-free structures everywhere.** Higher ceiling, far more ways to be subtly wrong.
The contention here is trivial — a handful of events per minute. Not worth it.

**A lock per subsystem.** Invites lock-ordering bugs, which are precisely the class of
failure this ADR exists to prevent.

**Everything on one thread with an event loop.** Tempting, and it would eliminate the
problem entirely. Rejected because blocking file and network I/O would stall rendering,
and `ReadDirectoryChangesW` is much simpler to use from a dedicated thread.

## Consequences

- Snapshotting costs an allocation per frame at worst. Given a reactive repaint model
  where idle frames are not drawn at all, this is irrelevant.
- The constraint is simple enough to actually hold over time, which matters more than
  squeezing out the last few percent.
- Build with sanitisers where the toolchain allows. ThreadSanitizer on the Linux core
  build is the cheapest insurance available, and it is a real reason to keep
  `src/core/` portable.

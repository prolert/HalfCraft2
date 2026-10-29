# Coordinator: running the digging plan

How the plan for digging through Half-Life's walls was carried out. The plan and its spec
(`docs/superpowers/plans/2026-10-17-dig-through-walls.md`, `docs/superpowers/specs/2026-10-17-dig-through-walls-design.md`)
are kept locally and aren't in the repository.

The process is `superpowers:subagent-driven-development`:
- a fresh implementer for each task;
- a reviewer after each task;
- a final review of the whole branch at the end.

The coordinator (the main session) writes and edits no code. It hands out tasks, settles disputes and keeps the log.

## Where things are

| What | Where |
|---|---|
| Progress and decision log (`Ruling: …`) | `.superpowers/sdd/2026-10-17-dig-through-walls/progress.md` |
| Pre-flight check of the plan | `.superpowers/sdd/2026-10-17-dig-through-walls/preflight.md` |
| Brief for task N (its text from the plan) | `.superpowers/sdd/2026-10-17-dig-through-walls/task-N-brief.md` |
| Implementer's report for task N | `.superpowers/sdd/2026-10-17-dig-through-walls/task-N-report.md` |
| Review packages (diffs) | `.superpowers/sdd/2026-10-17-dig-through-walls/review-<base>..<head>.diff` |

The `.superpowers/sdd/` folder isn't committed.

## Models

Name the model explicitly every time an agent is started. Without it, the agent takes the main session's model.

- Hard tasks go to **Opus 5.5** (`opus`).
- Easy tasks go to **Sonnet 5.5** (`sonnet`) or **Fable 5.1** (`fable`).

| Role | Model |
|---|---|
| Implementer, hard task | Opus 5.5 |
| Implementer, easy task | Sonnet 5.5 |
| Review, hard task | Opus 5.5 |
| Review, easy task | Sonnet 5.5 |
| Re-review after fixes (only the fixes and the findings) | Fable 5.1 (small diff) or Sonnet 5.5 |
| Fixes, rounds 1–3 | the task implementer's model |
| Fixes, rounds 4–5 (escalation) | Opus 5.5 |
| Pre-flight check, final review of the whole branch | Opus 5.5 |
| Mechanics (collecting diffs, checking the log, short lookups) | Fable 5.1 |

A **hard** task does at least one of these:
- edits engine code (server.dll, client.dll, SDK patches, shaders);
- edits Fabric mixins or the Minecraft world's lifecycle;
- runs the games to check its work.

An **easy** task is plain code with unit tests against given signatures, or a schema change.

| Task | What | Class | Implementer | Reviewer |
|---|---|---|---|---|
| 1 | Environment and the shader path check (spike) | hard | Opus 5.5 | Opus 5.5 |
| 2 | C++ core: tests and cutting cubes out of brushes | easy | Sonnet 5.5 | Sonnet 5.5 |
| 3 | C++ core: BSP tree, void, mask layout | easy | Sonnet 5.5 | Sonnet 5.5 |
| 4 | Protocol | easy | Sonnet 5.5 | Sonnet 5.5 |
| 5 | server.dll: the map's BSP tree and the void in the stream | hard | Opus 5.5 | Opus 5.5 |
| 6 | server.dll: brush materials and the dig flag | hard | Opus 5.5 | Opus 5.5 |
| 7 | The hole bridge and cutting collision | hard | Opus 5.5 | Opus 5.5 |
| 8 | Visibility through holes (PVS, `r_novis`) | hard | Opus 5.5 | Opus 5.5 |
| 9 | The shader DLL and the hole mask | hard | Opus 5.5 | Opus 5.5 |
| 10 | Minecraft: the link to Half-Life for digging | easy | Sonnet 5.5 | Sonnet 5.5 |
| 11 | Minecraft: storing dug cells and revealing | hard | Opus 5.5 | Opus 5.5 |
| 12 | Minecraft client: breaking Half-Life's walls | hard | Opus 5.5 | Opus 5.5 |
| 13 | Minecraft client: hole walls and `kRenDug` | hard | Opus 5.5 | Opus 5.5 |
| 14 | In-game check | hard | Opus 5.5 | Opus 5.5 |

An easy task whose implementer returned `BLOCKED` or got stuck on fixes moves to Opus 5.5.

## One task's cycle

1. Record BASE (`git rev-parse HEAD`) and a `Task N: dispatched` line in the log.
2. **The implementer:**
   - reads only the brief, the plan's header (Global Constraints) and the decisions passed at dispatch;
   - writes a full report to `task-N-report.md` and commits;
   - answers briefly: status, commits, tests, doubts.

   It starts no subagents.
3. **The reviewer** builds the `BASE..HEAD` diff with the `review-package` script. It gives two verdicts, brief
   compliance and quality (Critical / Important / Minor), plus a list of what can't be checked from the diff.
4. **Fixes:**
   - Critical and Important findings get up to 5 rounds, each followed by a re-review of the fixes only.
   - Minor findings go into the log as parked, and the final review deals with them.
   - A finding that disputes the plan's text first gets a coordinator decision (`Ruling:` in the log).
5. After 5 rounds the coordinator settles the open findings itself: it either keeps a finding with a ruling or
   makes the minimal fix that the following tasks depend on.
6. Write `Task N: complete (commits <base>..<head>, review clean | K parked)` in the log, then start the next task.

Tasks run strictly one at a time, in order 1–14. There is one working copy and one test session, and
`make format`, `make lint` and `make build` touch everything at once. The coordinator starts each task as its own
workflow (implementer → review → fixes), so that it makes the decisions between tasks itself.

## When to stop

Stop in four cases only:
- an irreversible or destructive action;
- anything security-related;
- an action outside this working copy that you normally ask about (merge, push, publishing);
- a plan so broken that every way forward is a guess.

The coordinator decides everything else itself and records the decision in the log with the cost of being wrong.
One special case: if task 1 shows that no shader variant works, tasks 2–14 don't start.

## If everything broke off

After a crash, a limit or a context compaction, trust the log and `git log`, not memory.

- Don't rerun a task that has a `Task N: complete` line.
- Continue a task that broke off midway: through SendMessage if its agent is still alive, otherwise with a fresh
  implementer given the brief and the report.
- Close any running games and the test session with the procedure from the plan's header before building again.

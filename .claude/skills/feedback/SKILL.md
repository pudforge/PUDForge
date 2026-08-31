---
name: feedback
description: Work the user feedback that has been accepted on pudforge/PUDForge - fix what can be fixed, ship it, and close only what actually shipped. Use when asked to look at issues, feedback, or what users have reported.
---

# Working the feedback queue

Feedback reaches `pudforge/PUDForge` as an issue from the Discord bot, labelled
`feedback` `triage`, and `bug` or `idea`. **`triage` is not a queue of work.** It
becomes work when Kalle adds `accepted`, and that gate exists because he is the
one who decides what PUDForge should be.

```powershell
./scripts/feedback.ps1            # the three lists
./scripts/feedback.ps1 -Number 12 # one issue, with its comments
./scripts/feedback.ps1 -Ready     # just the numbers, to loop over
```

## The rule about closing

**Close an issue only when a release carrying the fix is published.** Not when
the code is written, not when the tests pass, not when it is pushed. Until the
exe is downloadable the reporter's problem is still there.

If you cannot do it, say so on the issue and leave it open. An issue closed
without a fix is worse than one left open: it tells the reporter they were heard
and then quietly loses the thing. Never close one to tidy the list.

## Doing one

1. **Read it, then reproduce it.** The reporter's words are a claim about
   behaviour. Find where that behaviour lives before believing the diagnosis in
   the report - a report says what somebody saw, which is often not what
   happened.

2. **Measure before asserting.** This repo settles questions against the corpus
   (`reference/war2_ref`, 357 real maps) and against the specification, not
   against what a name suggests. Two rules about movement bits and one about the
   active/passive flag were wrong on plausible names alone. If a report is about
   what the game does, find the evidence; if the evidence and the report
   disagree, say so on the issue rather than picking one.

3. **Fix it in the right place.** Map rules live in `PUDForgeCore/`; a decision
   that is judgement rather than derivation goes in `overrides/`, one file per
   decision, with the evidence at the top. Editor behaviour goes in
   `PUDForgeWin/Editor.*`, which is Win32-free and therefore the only part with
   real tests.

4. **Test it so it stays fixed.** A fix without a test is a fix that comes back.
   Corpus assertions split shipped maps from community ones - a claim about the
   game asserted over community maps measures the community.

5. **Ship it.** `scripts/prep-release.ps1` bumps the version, opens the changelog
   section and proves the build. The changelog bullet is what the reporter
   reads: short, ASD-STE100, about what is different for them, not about the
   cause.

6. **Close it, naming the version.** A comment saying what changed and which
   release carries it, then close. `gh issue close N --repo pudforge/PUDForge
   --comment "..."`.

## When you cannot

Comment on the issue with what you found and why it stopped, add `blocked`, and
leave it open. Worth its own comment rather than silence:

- **Cannot reproduce** - say what you tried, and ask for the map or the version.
  A map attached to the issue usually settles it.
- **The evidence disagrees** - the corpus or the specification says the current
  behaviour is right. Say which, with the numbers, and let Kalle rule.
- **It needs a decision, not a fix** - the report is really a request to change
  what PUDForge should do. That is Kalle's, not yours.
- **It needs something you do not have** - the game's artwork, an account, a
  machine.

## Several at once

Group them into one release when they are small and unrelated; a release per
one-line fix is noise in the changelog and in the tag list. Close each issue
separately, naming the same version.

Do not batch a risky change with safe ones. If one of them is a rule about how
maps are read, it ships alone, so that a regression has one suspect.

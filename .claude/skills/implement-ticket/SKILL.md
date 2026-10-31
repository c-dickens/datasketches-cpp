---
name: implement-ticket
description: Implement one Linear ticket of the MinHash + LSH milestone (titles start with "MH-") in this fork, from reading the ticket to an open PR with evidence. Use when asked to implement, work on or pick up a CD-nnn or MH-n ticket.
---

# Implement a ticket

1. **Read the ticket** with the Linear tools: the description, the comments and the "blocked by" relations.
   - Stop if a blocker is not Done.
   - The ticket's **Decided** section is binding. Do not change the names, signatures or constants that it fixes.
2. **Restate "Done when"** as a checklist in your first message. If an item is unclear, or a public API, hashing, test-vector or serialized-format choice is open, ask the owner in the chat. In an unattended run, comment the question on the ticket and stop.
3. **Branch** from `origin/minhash/main`: `git fetch origin minhash/main && git switch -c <branch> origin/minhash/main`.
4. **Plan** the change as a list of files to create or modify. Keep one PR to about 300 changed lines, unless the ticket allows more.
5. **Write the tests first** where the ticket names tests. Show that they fail before the change, where that is possible.
6. **Implement.** Follow `CLAUDE.md`. Load these skills when they apply:
   - `change-hash-or-format` for hashing, band keys, test vectors or the serialized format;
   - `write-statistical-test` for any test of a probability or an estimate.
7. **Run every check** in the Commands section of `CLAUDE.md` that applies. Fix every failure that your change causes. Never skip, disable or loosen a test to make it pass.
8. **Review your own diff** with the `review-minhash` checklist (`git diff origin/minhash/main...HEAD`). Fix each finding.
9. **Open the PR** into `minhash/main`:
   - title: `MH-n: <ticket title>`;
   - body: a summary of 2 or 3 sentences; the "Done when" list, with each item ticked and its evidence (the command and the key lines of output); your assumptions; a link to the ticket.
10. **Update Linear:** comment the PR link on the ticket, and set the ticket to In Review.
11. **Handle the review.** The Claude review posts a comment that starts with `## Verdict`. Fix each valid Blocker and Major finding with the smallest change. Reply to every finding with what you changed, or why you did not change it. Push to the same branch.
12. **Never merge.** The owner merges.

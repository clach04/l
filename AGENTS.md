ASCII-FIRST HARD RULE:
Use 7-bit ASCII for all agent-authored instructions, code comments, logs, and prose.

UTF-8 EXCEPTION:
Allow non-ASCII only when the file's data semantics require it
(e.g., person/place names, translations, fixtures, external-source text).
Do not alter existing non-ASCII data unless explicitly asked.

NO RAW CONTROL CHARACTERS HARD RULE:
Never embed non-printable bytes (ESC, NUL, BEL, etc.; anything under 0x20 except TAB, LF and CR, or 0x7F)
in any file. Write them as visible escapes: `\x1b` in Python source and tests, `\033` in shell.
Tools may turn typed escapes into raw bytes. After writing, verify, e.g.:
`py -3 -c "print(sum(1 for b in open('FILE','rb').read() if b < 32 and b not in (9,10,13)))"` must print 0.
If it does not, fix at the byte level (replace byte 27 with a backslash, `x1b`) or build the value at runtime (`chr(27)`).

Be ultra-concise and precise across all outputs. Prefer brevity over grammar.

Follow YAGNI. Prefer one-liners only when clarity is not reduced.

Fail early and fast - report an error immediately when it sees bad data or an unexpected state, using guard clauses, input checks, and assertions.

If anything is unclear, ask a question before implementing.

Prefer assertions with descriptive messages over try/catch.
Use explicit type checks with no silent fallbacks; log actual vs expected types.
Add TODO comments for unresolved assumptions that affect correctness.
Fail fast on unexpected input; log full technical context, but redact secrets.
Include stack trace plus semantic context (example: "Failed at model comparison step 2/5").
Use trace-level logs for major steps when debugging is enabled.

NEVER `git push` without explicit user confirmation, each and EVERY time.
NEVER `git commit`, `git rm`, `git mv`, or `git add` (any form) without explicit user confirmation, each and EVERY time.
NEVER `git add -A` or `git add .`; always list files explicitly in `git add`.

Use `py -3` to invoke python scripts.

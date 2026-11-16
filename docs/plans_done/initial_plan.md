# l.py initial plan

Glossary: see CONTEXT.md. This file is the agreed spec plus build steps.
No ADR written: decisions are easy to reverse (flags), not a hard trade-off.

## Agreed decisions

| Area         | Decision                                                                                               |
|--------------|--------------------------------------------------------------------------------------------------------|
| Targets      | Trailing slash = directory (error if not). Existing dir or file is listed. Else wildcard. None = "."   |
| Matching     | fnmatch on names, platform-native case. `-i` / `--ignore-case`. `--case-sensitive` undoes it           |
| Recursion    | `-R` / `--recursive` flat, relative paths, sorted across tree. Directory links not descended           |
| Grouped      | `--by-dir` grouped listing, implies `-R`. Header per dir, per-dir summary plus grand total             |
| Sort         | `--sort=time\|name\|size`, default time, oldest first, name tie-break. `-r` reverses                   |
| Dir first    | `--group-directories-first`, long-only. Ignored under flat `-R`                                        |
| Line         | `YYYY-MM-DD HH:MM  size name`. Size right-aligned. `<DIR>` for directories                             |
| Time         | Local mtime (lstat), minute precision. Out of range prints `????-??-?? ??:??`                          |
| Size         | `-h` copies `ls -lh` (1024, K M G T P, 1 decimal under 10, round up). No commas                        |
| Links        | `name -> target` as stored. lstat. Broken links listed. Not followed                                   |
| Hidden       | Everything shown except "." and ".."                                                                   |
| Summary      | On when stdout is a tty or `-s`. Files, dirs, bytes, bytes free. Off under `--names` unless `-s`       |
| Names only   | `--names`: bare names, one per line, links without `->`                                                |
| Separators   | Input accepts `/` and `\` on Windows. Output native. `--posix` forces `/` in names                     |
| Encoding     | Never crash on names. Unprintable chars use backslashreplace. `--utf8` forces UTF-8 bytes to stdout    |
| Lossy names  | Closing stderr note `l.py: N names shown lossy, copy/paste unsafe` (+ `(try --utf8)` if not on)        |
| Errors       | stderr messages. Missing literal: exit 2. No wildcard match: exit 1. Other failures: continue, exit 2  |
| Error line   | If any errors, final stderr line `l.py: N errors (see above)`. Lossy note is last                      |
| Env          | `L_OPTS` default flags, shell-split, prepended to argv                                                 |
| Code         | One stdlib-only file `l.py`. Python 2.7 and 3. `listdir` + `lstat`. Tests in `test_l.py` (unittest)    |
| Flags        | argparse `add_help=False`; `--help` only so `-h` is human sizes                                        |
| Deferred     | Color, C port, `--si`, atime/ctime, case-sensitive short flag                                          |

## Build steps

 1. Pure functions: format_size, format_time, sort keys, wildcard match, line formatting.
 2. Target resolution and listing (non-recursive), then `-R` walk via `os.walk(onerror=...)`.
 3. Links, hidden handling, `--by-dir`, `--names`, `--posix`.
 4. Output layer: backslashreplace, `--utf8`, lossy counting.
 5. Summary with free space (`shutil.disk_usage` / `os.statvfs` / ctypes).
 6. Error and exit-code handling, `L_OPTS`.
 7. `test_l.py`; run with `py -3` and `py -2`.

## Known limits (v1)

  * Wide East Asian characters may misalign columns.
  * Python 2 on Windows: link targets are not shown (no `os.readlink`).
  * Windows 2.7 link display not tested.

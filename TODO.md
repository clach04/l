# TODO

## Not verified

The test suite has only been run on Windows. `py -3` skips 1 (POSIX exec
bit); `py -2` skips 3 (that one, the Windows hidden attribute which needs
py3, and symlinks, which py2 on Windows lacks). POSIX had a quick manual
check on Linux only (looked fine).

  * POSIX: the test suite, the exec-bit color test, `--utf8` and
    backslashreplace output, `--quote=sh` output have not been run or
    checked specifically.
  * Windows links: only covered by the symlink unit test (py3), not checked
    by eye. Junction display is untested.
  * `--quote` output has not been pasted into a real shell (sh, cmd,
    PowerShell).
  * C version: built and tested on Windows (MinGW gcc 13) only. The POSIX
    branch passed a syntax check against a shim, never built or run. Never
    built with MSVC or clang. Color was compared byte for byte with `l.py`
    when piped, but the Windows console mode switch (ANSI on a real console)
    and color on a real Linux tty have not been checked.
  * Color: not checked whether the optional `colorama` path is actually used
    on Windows (modern consoles may not need it).

## Known limits

  * Wide East Asian characters can misalign columns.
  * Python 2 on Windows has no `os.readlink`, so link targets are not shown.
  * Color: `-> target` is not colored; other `LS_COLORS` keys
    (`or`, `mi`, `so`, ...) are ignored.
  * Python 2 on Windows: the hidden attribute is not seen, only dotfiles.

## Deferred

  * Startup time (`python3 -X importtime`, Linux, importtime.txt): `site`
    dominates (~80 ms of ~150 ms, outside this program; try `-S` or
    `-I` to check). Of ours, `argparse` is the largest (~15 ms, and it pulls
    in `shutil`, `re`, `gettext`). Done: `colorama` (~10 ms) is now imported
    only for a Windows tty (confirmed gone in importtime2.txt). `shutil`
    is still imported (~6-14 ms) by argparse itself: creating a parser calls
    `shutil.get_terminal_size()`, which loads bz2, lzma and zlib. So
    argparse really costs ~20-30 ms. Next, if it matters: hand-rolled
    option parsing instead of argparse (would also drop `re` and
    `gettext`).

  * Revisit footer format. Now `7 files | 4 dirs | 53,875 bytes | N bytes
    free`. Idea: `key:value` (`files:7 dirs:4 size:53,875 free:N`). The
    "bytes" label is misleading: it is the cumulative size of the listed
    files, so something like `size`/`used` fits better. Or `dir`-style
    aligned two lines.

  * C version (`l.c`): Unicode case folding, mtime sub-second ordering on
    non-Linux POSIX, Windows paths longer than 260 characters.
  * `--si` (base 1000 sizes).
  * atime / ctime.
  * Short flag for case-sensitive matching.

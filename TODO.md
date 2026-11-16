# TODO

## Not verified (initial build)

  * `py -2` skipped 1 of 27 tests. Not checked which one; most likely the
    symlink test.
  * Link display (`name -> target`) is untested on Windows and not proven
    anywhere.
  * `--utf8` and backslashreplace output on POSIX are untested.
  * Windows junction display is untested.
  * Summary footer on a real tty is untested. Tests run piped, so only `-s`
    is covered.

  * Color: the optional `colorama` path is untested (not installed here), as
    is color on a real Windows console.


## Known limits

  * Wide East Asian characters can misalign columns.
  * Python 2 on Windows has no `os.readlink`, so link targets are not shown.
  * Color: `-> target` is not colored; other `LS_COLORS` keys
    (`or`, `mi`, `so`, ...) are ignored.
  * Python 2 on Windows: the hidden attribute is not seen, only dotfiles.

## Deferred

  * Revisit footer format. Now `7 files | 4 dirs | 53,875 bytes | N bytes
    free`. Idea: `key:value` (`files:7 dirs:4 size:53,875 free:N`). The
    "bytes" label is misleading: it is the cumulative size of the listed
    files, so something like `size`/`used` fits better. Or `dir`-style
    aligned two lines.

  * C version.
  * `--si` (base 1000 sizes).
  * atime / ctime.
  * Short flag for case-sensitive matching.

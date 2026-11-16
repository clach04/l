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
  * Color: only the name is colored (not `-> target`); other `LS_COLORS` keys
    (`or`, `mi`, `so`, ...) are ignored.
  * Python 2 on Windows: the hidden attribute is not seen, only dotfiles.

## Deferred

  * C version.
  * `--si` (base 1000 sizes).
  * atime / ctime.
  * Short flag for case-sensitive matching.

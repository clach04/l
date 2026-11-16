# l.py

ls/dir-like directory lister with fd-style usability. One stdlib-only file,
Python 2.7 and 3.

Default output is like `ls -altr` / `dir /od`: oldest first, newest at the
bottom.

```
2026-11-16 08:48  1468 AGENTS.md
```

## Usage

```
l.py [options] [TARGET ...]
```

A TARGET is:

  * a directory (trailing slash forces this): its entries are listed
  * a file: that file is listed
  * a wildcard (`*`, `?`, `[...]`): matched against names, not recursive
    unless `-R`
  * anything else that does not exist: fd-style substring match, so
    `l.py foo` means `*foo*` (`src/foo` means `src/*foo*`)
  * omitted: the current directory

An existing file or directory always wins over a substring match.

Examples:

```
l.py                  # current directory
l.py -h src/          # human sizes
l.py "*.py"           # wildcard, this directory only
l.py -R "*.py"        # all .py files below, flat relative paths
l.py --by-dir         # like -R, entries of a directory kept together
l.py --names -R | xargs ...
```

## Options

| Option                      | Meaning                                                  |
|-----------------------------|----------------------------------------------------------|
| `-R`, `--recursive`         | Recurse. Flat list of relative paths, sorted across tree |
| `--by-dir`                  | Like `-R`; each directory's entries stay together        |
| `-r`, `--reverse`           | Reverse sort                                             |
| `--sort time\|name\|size`   | Sort key, default `time`. Name is the tie-break          |
| `--group-directories-first` | Directories first (ignored by flat `-R`)                 |
| `-h`                        | Human sizes like `ls -lh` (`1.5K`)                       |
| `--commas`                  | Comma-group digits in the size column (`-h` wins)        |
| `-i`, `--ignore-case`       | Case-insensitive wildcards                               |
| `--case-sensitive`          | Case-sensitive wildcards (default is platform native)    |
| `--no-hidden`               | Hide dotfiles and Windows hidden entries                 |
| `--hidden`                  | Show hidden entries (default); undoes `--no-hidden`      |
| `--skip=A,B`                | `-R` does not enter these dirs; replaces defaults        |
| `--skip-also=A,B`           | Add names to the skip list                               |
| `--color=WHEN`              | `auto` (tty only), `always`, `never`. Uses `LS_COLORS`   |
| `-s`, `--summary`           | Always print summary (default: only on a tty)            |
| `--names`                   | Names only, one per line                                 |
| `-q`, `--quote`             | Quote names for copy/paste (`sh`; `c` on Windows)        |
| `--quote-style=sh\|c`       | `sh`: `'a b'`. `c`: `"a b"`. Implies `--quote`           |
| `--posix`                   | Print `/` in names on Windows                            |
| `--utf8`                    | Force UTF-8 output                                       |
| `--help`                    | Help (`-h` is human sizes)                               |

`L_OPTS` holds default options, e.g. `L_OPTS=-h`. Command-line options win.

## Behavior

  * Hidden entries are shown. `.` and `..` are not. `--no-hidden` hides
    dotfiles and, on Windows (Python 3), entries with the hidden attribute.
    Under `-R` hidden directories are not entered either.
  * Under `-R`, directories on the skip list are listed but not entered
    (`.git/` is shown, its contents are not). Default list: `.git`, `.hg`,
    `.svn`, `.bzr`, `CVS`, `__pycache__`, `.pytest_cache`, `.mypy_cache`,
    `.ruff_cache`, `.tox`, `.venv`, `node_modules`. `--skip=a,b` replaces
    it (`--skip=` disables), `--skip-also=a,b` adds to it. Names may use
    wildcards, with the platform case rule. Name a skipped directory as the
    Target to look inside, e.g. `l.py -R .git`.
  * Directory names end with a path separator (`sub/`).
  * Links show as `name -> target` and are never followed.
  * Color (experimental) has three zones: timestamp (`da`, dim), size number
    (`sn`, yellow), `<DIR>` marker (`dm`, blue), then the name. `LS_COLORS`
    is read for `di`, `ln`, `ex`, `fi`, `*.ext`, plus `da`, `sn` (eza keys)
    and `dm` (ours) over a built-in default (dirs bold blue, links cyan,
    executables green). An empty value (`sn=`) turns a zone off. Padding is
    never colored. `auto` needs a tty and honors `NO_COLOR`.
    `--color=always` ignores `NO_COLOR`. On Windows, `colorama` is used if
    installed.
  * `--quote` quotes the name only (not `-> target`), and only when needed.
    `sh`: single quotes, `$'..'` escapes for control or undecodable bytes.
    `c`: double quotes with C escapes. On Windows `\` is the path separator
    and stays literal in `c` style.
  * Times are local modification time. Bad times show `????-??-?? ??:??`.
  * Unprintable characters print as `\xe9` style escapes. A closing note on
    stderr counts such names. Use `--utf8` for copy/paste-safe output.
  * Errors go to stderr, with a closing `N errors` line. Exit code: 1 for no
    wildcard match, 2 for other errors.

## Aliases

Unix shell

    alias l="py -3 l.py"
    export L_OPTS="--color always"


Windows CMD.exe

    doskey l=py -3 l.py $*
    doskey l=py -3 C:\full\path\l.py $*

## Tests

```bash
py -3 test_l.py
py -2 test_l.py
```

See [TODO.md](TODO.md) for known gaps and [CONTEXT.md](CONTEXT.md) for the
glossary. The agreed spec is in `docs/plans_done/initial_plan.md`.

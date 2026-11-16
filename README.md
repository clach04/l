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
| `-i`, `--ignore-case`       | Case-insensitive wildcards                               |
| `--case-sensitive`          | Case-sensitive wildcards (default is platform native)    |
| `--no-hidden`               | Hide dotfiles and Windows hidden entries                 |
| `--hidden`                  | Show hidden entries (default); undoes `--no-hidden`      |
| `-s`, `--summary`           | Always print summary (default: only on a tty)            |
| `--names`                   | Names only, one per line                                 |
| `--posix`                   | Print `/` in names on Windows                            |
| `--utf8`                    | Force UTF-8 output                                       |
| `--help`                    | Help (`-h` is human sizes)                               |

`L_OPTS` holds default options, e.g. `L_OPTS=-h`. Command-line options win.

## Behavior

  * Hidden entries are shown. `.` and `..` are not. `--no-hidden` hides
    dotfiles and, on Windows (Python 3), entries with the hidden attribute.
    Under `-R` hidden directories are not entered either.
  * Under `-R`, version control directories (`.git`, `.hg`, `.svn`, `.bzr`,
    `CVS`) are listed but not entered. Name one as the Target to look
    inside, e.g. `l.py -R .git`.
  * Directory names end with a path separator (`sub/`).
  * Links show as `name -> target` and are never followed.
  * Times are local modification time. Bad times show `????-??-?? ??:??`.
  * Unprintable characters print as `\xe9` style escapes. A closing note on
    stderr counts such names. Use `--utf8` for copy/paste-safe output.
  * Errors go to stderr, with a closing `N errors` line. Exit code: 1 for no
    wildcard match, 2 for other errors.

## Aliases

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

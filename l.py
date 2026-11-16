#!/usr/bin/env python
# -*- coding: ascii -*-
"""l.py - ls/dir-like lister with fd-style usability.

Python 2.7 and 3, stdlib only. See CONTEXT.md and initial_plan.md.
"""
from __future__ import print_function

import argparse
import fnmatch
import math
import os
import re
import shlex
import shutil
import stat
import sys
import time

PY3 = sys.version_info >= (3,)
IS_WIN = os.name == 'nt'
text_type = type(u'')
UNKNOWN_TIME = '????-??-?? ??:??'
DIR_MARK = '<DIR>'
DOT = u'.' if (not PY3 and IS_WIN) else '.'  # py2 Windows needs unicode paths
IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003  # Windows junction
ENV_OPTS = 'L_OPTS'
FILE_ATTRIBUTE_HIDDEN = 0x2
# version control directories: listed under -R, never entered
VCS_DIRS = frozenset(os.path.normcase(n)
                     for n in ('.git', '.hg', '.svn', '.bzr', 'CVS'))

if PY3:
    _LONE_SURROGATE = re.compile(u'[\ud800-\udfff]')


# ---------------------------------------------------------------- pure funcs

def format_size(n, human=False):
    """Byte count to str. human: like ls -lh (1024, K M G T P, round up)."""
    assert n >= 0, 'negative size %r' % (n,)
    if not human or n < 1024:
        return str(n)
    v = float(n)
    for unit in 'KMGTP':
        v /= 1024.0
        if v < 10:
            r = math.ceil(v * 10) / 10.0
            if r < 10:
                return '%.1f%s' % (r, unit)
        r = int(math.ceil(v))
        if r < 1024 or unit == 'P':
            return '%d%s' % (r, unit)
    raise AssertionError('unreachable')


def format_time(mtime):
    """Local ISO-like minute time, or UNKNOWN_TIME when out of range."""
    try:
        return time.strftime('%Y-%m-%d %H:%M', time.localtime(mtime))
    except (ValueError, OverflowError, OSError):
        return UNKNOWN_TIME


def is_hidden(name, st):
    """Dotfile on any platform, or Windows hidden attribute (py3 lstat)."""
    return (name.startswith('.')
            or bool(getattr(st, 'st_file_attributes', 0) & FILE_ATTRIBUTE_HIDDEN))


def is_vcs_dir(name):
    return os.path.normcase(name) in VCS_DIRS


def has_magic(s):
    return any(c in s for c in '*?[')


def make_matcher(pattern, ignore_case):
    """ignore_case: True force insensitive, False force sensitive,
    None platform native (fnmatch.fnmatch)."""
    if ignore_case is None:
        return lambda name: fnmatch.fnmatch(name, pattern)
    if ignore_case:
        low = pattern.lower()
        return lambda name: fnmatch.fnmatchcase(name.lower(), low)
    return lambda name: fnmatch.fnmatchcase(name, pattern)


class Entry(object):
    """One listed file, directory or link."""
    __slots__ = ('name', 'is_dir', 'is_link', 'size', 'mtime', 'target')

    def __init__(self, name, is_dir, is_link, size, mtime, target):
        self.name = name  # relative path as shown
        self.is_dir = is_dir
        self.is_link = is_link
        self.size = size
        self.mtime = mtime
        self.target = target  # link text as stored, or None


def sort_entries(entries, key, reverse, dirs_first):
    """Sort in place. key: time|name|size. Name is the tie-break."""
    def label(e):
        return os.path.normcase(e.name)

    def k(e):
        if key == 'time':
            return (e.mtime, label(e))
        if key == 'size':
            return (0 if e.is_dir else e.size, label(e))
        assert key == 'name', 'bad sort key %r' % (key,)
        return (label(e),)

    entries.sort(key=k, reverse=reverse)
    if dirs_first:
        entries.sort(key=lambda e: 0 if e.is_dir else 1)  # stable
    return entries


def format_entry(e, width, args):
    """One output line (native str type; see Output)."""
    size = DIR_MARK if e.is_dir else format_size(e.size, args.human)
    name = e.name
    if e.is_dir:
        name += os.sep
    if args.posix and IS_WIN:
        name = name.replace('\\', '/')
    if args.names:
        return name
    if e.target is not None:
        name = name + ' -> ' + e.target
    return '%s  %s %s' % (format_time(e.mtime), size.rjust(width), name)


def size_width(entries, args):
    return max([len(DIR_MARK if e.is_dir else format_size(e.size, args.human))
                for e in entries] or [0])


def summary_line(files, dirs, nbytes, free, human):
    def b(n):
        return format_size(n, human) if human else '%d bytes' % n
    parts = ['%d files' % files, '%d dirs' % dirs, b(nbytes)]
    if free is not None:
        parts.append(b(free) + ' free')
    return ', '.join(parts)


# ---------------------------------------------------------------- platform

def free_bytes(path):
    """Free bytes on path's volume, or None when unavailable."""
    try:
        if hasattr(shutil, 'disk_usage'):
            return shutil.disk_usage(path).free
        if hasattr(os, 'statvfs'):
            st = os.statvfs(path)
            return st.f_bavail * st.f_frsize
        if IS_WIN:
            import ctypes
            free = ctypes.c_ulonglong(0)
            if not isinstance(path, text_type):
                path = path.decode(sys.getfilesystemencoding())
            ok = ctypes.windll.kernel32.GetDiskFreeSpaceExW(
                ctypes.c_wchar_p(path), None, None, ctypes.byref(free))
            return free.value if ok else None
    except (OSError, IOError, ValueError, AttributeError, UnicodeError):
        pass
    return None


def make_entry(name, full, st):
    mode = st.st_mode
    is_link = stat.S_ISLNK(mode) or (
        getattr(st, 'st_reparse_tag', 0) == IO_REPARSE_TAG_MOUNT_POINT)
    target = None
    if is_link:
        try:
            target = os.readlink(full)
        except (OSError, AttributeError, ValueError):
            target = None  # e.g. py2 on Windows has no os.readlink
    return Entry(name, stat.S_ISDIR(mode) and not is_link, is_link,
                 st.st_size, st.st_mtime, target)


# ---------------------------------------------------------------- output

class Status(object):
    """Collects errors, exit code and lossy-name count."""

    def __init__(self):
        self.errors = 0
        self.code = 0
        self.lossy = 0

    def error(self, msg, code=2):
        self.errors += 1
        self.code = max(self.code, code)
        err_write('l.py: ' + msg)


def err_write(msg):
    sys.stdout.flush()
    if not PY3 and isinstance(msg, text_type):
        msg = msg.encode(getattr(sys.stderr, 'encoding', None) or 'ascii',
                         'backslashreplace')
    sys.stderr.write(msg + '\n')  # py3 stderr already backslashreplaces
    sys.stderr.flush()


class Output(object):
    """stdout writer. Default: backslashreplace for unprintable chars.
    utf8: UTF-8 bytes (raw filesystem bytes on POSIX)."""

    def __init__(self, utf8, status):
        self.utf8 = utf8
        self.status = status

    def write(self, line):
        if PY3:
            self._write3(line)
        else:
            self._write2(line)

    def _write3(self, line):
        if self.utf8:
            if IS_WIN:
                try:
                    data = line.encode('utf-8')
                except UnicodeEncodeError:
                    line = _LONE_SURROGATE.sub(chr(0xFFFD), line)
                    self.status.lossy += 1
                    data = line.encode('utf-8')
            else:
                data = os.fsencode(line)  # original bytes, lossless
            sys.stdout.buffer.write(data + b'\n')
            return
        enc = sys.stdout.encoding or 'ascii'
        try:
            line.encode(enc)
        except UnicodeEncodeError:
            line = line.encode(enc, 'backslashreplace').decode(enc)
            self.status.lossy += 1
        sys.stdout.write(line + '\n')

    def _write2(self, line):
        if isinstance(line, bytes):  # POSIX py2: names are raw bytes
            data = line
        elif self.utf8:
            data = line.encode('utf-8')
        else:
            enc = getattr(sys.stdout, 'encoding', None) or 'ascii'
            try:
                data = line.encode(enc)
            except UnicodeEncodeError:
                data = line.encode(enc, 'backslashreplace')
                self.status.lossy += 1
        sys.stdout.write(data + b'\n')


# ---------------------------------------------------------------- targets

class Spec(object):
    """Resolved Target: kind dir|file|glob."""

    def __init__(self, kind, arg, root, pattern, prefix):
        self.kind = kind
        self.arg = arg
        self.root = root  # directory to list (dir, glob)
        self.pattern = pattern
        self.prefix = prefix  # prepended to entry names


def resolve_target(arg, multi):
    """Return (Spec, None) or (None, error message)."""
    forced_dir = arg.endswith('/') or (IS_WIN and arg.endswith('\\'))
    if forced_dir:
        if not os.path.isdir(arg):
            return None, '%s: not a directory' % arg
        return Spec('dir', arg, arg, None, arg if multi else ''), None
    if os.path.isdir(arg):
        return Spec('dir', arg, arg, None, arg if multi else ''), None
    if os.path.lexists(arg):
        return Spec('file', arg, None, None, ''), None
    dirpart, base = os.path.split(arg)
    if has_magic(dirpart):
        return None, '%s: wildcards in directory part not supported' % arg
    root = dirpart or DOT
    if not os.path.isdir(root):
        return None, '%s: not found' % root
    if not has_magic(base):
        base = '*' + base + '*'  # fd-style implicit substring match
    return Spec('glob', arg, root, base, dirpart), None


def collect(spec, args, ignore_case, status):
    """Return list of per-directory entry lists for one Spec, in directory
    name order (depth first)."""
    if spec.kind == 'file':
        try:
            st = os.lstat(spec.arg)
        except OSError as e:
            status.error('%s: %s' % (spec.arg, e.strerror or e))
            return []
        return [[make_entry(spec.arg, spec.arg, st)]]
    matcher = make_matcher(spec.pattern, ignore_case) if spec.pattern else None
    groups = []
    stack = ['']  # relative directory paths still to list
    while stack:
        rel_dir = stack.pop()
        path = os.path.join(spec.root, rel_dir) if rel_dir else spec.root
        try:
            names = os.listdir(path)
        except OSError as e:
            status.error('%s: %s' % (path, e.strerror or e))
            continue
        entries = []
        subdirs = []
        for n in names:
            full = os.path.join(path, n)
            try:
                st = os.lstat(full)
            except OSError as e:
                status.error('%s: %s' % (full, e.strerror or e))
                continue
            if args.no_hidden and is_hidden(n, st):
                continue  # neither listed nor descended
            rel = os.path.join(rel_dir, n) if rel_dir else n
            e = make_entry(os.path.join(spec.prefix, rel) if spec.prefix
                           else rel, full, st)
            if matcher is None or matcher(n):
                entries.append(e)
            # VCS dirs met during the walk are listed but not entered; a
            # VCS dir given as the Target is the walk root, so it is entered
            if args.recursive and e.is_dir and not is_vcs_dir(n):
                subdirs.append((n, rel))
        groups.append(entries)
        subdirs.sort(key=lambda s: os.path.normcase(s[0]), reverse=True)
        stack.extend(rel for _, rel in subdirs)  # reversed: pop = name order
    return groups


# ---------------------------------------------------------------- main

def build_parser():
    p = argparse.ArgumentParser(
        prog='l.py', add_help=False,
        description='List files like ls -altr / dir /od. TARGET is a '
                    'directory, a file, or a wildcard (not recursive '
                    'unless -R). Default options may be set in %s.'
                    % ENV_OPTS)
    p.add_argument('--help', action='help', help='show this help')
    p.add_argument('targets', nargs='*', metavar='TARGET')
    p.add_argument('-R', '--recursive', action='store_true',
                   help='recurse; flat list of relative paths')
    p.add_argument('--by-dir', action='store_true',
                   help='like -R, but keep entries of each directory together '
                        '(implies -R)')
    p.add_argument('-r', '--reverse', action='store_true',
                   help='reverse sort order')
    p.add_argument('--sort', choices=('time', 'name', 'size'),
                   default='time', help='sort key (default: time, oldest '
                                        'first)')
    p.add_argument('--group-directories-first', action='store_true',
                   help='directories first (ignored by flat -R)')
    p.add_argument('-h', '--human-readable', dest='human',
                   action='store_true', help='human readable sizes')
    p.add_argument('-i', '--ignore-case', dest='ignore_case',
                   action='store_const', const=True, default=None,
                   help='case-insensitive wildcard matching')
    p.add_argument('--case-sensitive', dest='ignore_case',
                   action='store_const', const=False,
                   help='case-sensitive wildcard matching')
    p.add_argument('--no-hidden', dest='no_hidden', action='store_const',
                   const=True, default=False,
                   help='hide dotfiles and Windows hidden entries')
    p.add_argument('--hidden', dest='no_hidden', action='store_const',
                   const=False, help='show hidden entries (default)')
    p.add_argument('-s', '--summary', action='store_true',
                   help='always print summary (default: only on a tty)')
    p.add_argument('--names', action='store_true',
                   help='names only, one per line')
    p.add_argument('--posix', action='store_true',
                   help='use / in printed names on Windows')
    p.add_argument('--utf8', action='store_true',
                   help='force UTF-8 output')
    return p


def get_argv(argv):
    """L_OPTS prepended to argv (command line wins). Py2 Windows: unicode."""
    env = os.environ.get(ENV_OPTS, '')
    items = shlex.split(env) if env.strip() else []
    items += list(argv)
    if not PY3 and IS_WIN:
        enc = sys.getfilesystemencoding()
        items = [a.decode(enc) if isinstance(a, bytes) else a for a in items]
    return items


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    args = build_parser().parse_args(get_argv(argv))
    if args.by_dir:
        args.recursive = True
    status = Status()
    out = Output(args.utf8, status)
    targets = args.targets or [DOT]
    multi = len(targets) > 1
    show_summary = args.summary or (sys.stdout.isatty() and not args.names)

    all_groups = []
    free_path = None
    for t in targets:
        spec, msg = resolve_target(t, multi)
        if spec is None:
            status.error(msg)
            continue
        if free_path is None:
            free_path = spec.root or os.path.dirname(spec.arg) or DOT
        groups = collect(spec, args, args.ignore_case, status)
        if spec.pattern and not any(groups):
            status.error('%s: no match' % spec.arg, code=1)
        all_groups.extend(groups)

    free = free_bytes(free_path) if (show_summary and free_path) else None

    entries = []
    if args.by_dir:
        # flat lines; entries of one directory stay adjacent
        dirs_first = args.group_directories_first
        for group in all_groups:
            sort_entries(group, args.sort, args.reverse, dirs_first)
            entries.extend(group)
    else:
        for group in all_groups:
            entries.extend(group)
        # dirs-first is per directory, so it does not apply to flat -R
        dirs_first = args.group_directories_first and not args.recursive
        sort_entries(entries, args.sort, args.reverse, dirs_first)
    counts = render(entries, args, out)
    if show_summary:
        out.write('')  # blank line separates the footer from the entries
        out.write(summary_line(counts[0], counts[1], counts[2], free,
                               args.human))

    if status.errors:
        err_write('l.py: %d error%s (see above)'
                  % (status.errors, '' if status.errors == 1 else 's'))
    if status.lossy:
        err_write('l.py: %d names shown lossy, copy/paste unsafe%s'
                  % (status.lossy, '' if args.utf8 else ' (try --utf8)'))
    sys.stdout.flush()
    return status.code


def render(entries, args, out):
    """Write entry lines. Returns [files, dirs, bytes]."""
    width = size_width(entries, args)
    files = dirs = nbytes = 0
    for e in entries:
        out.write(format_entry(e, width, args))
        if e.is_dir:
            dirs += 1
        else:
            files += 1
            nbytes += e.size
    return [files, dirs, nbytes]


if __name__ == '__main__':
    sys.exit(main())

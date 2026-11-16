# -*- coding: ascii -*-
"""Tests for l.py. Run: py -3 test_l.py ; py -2 test_l.py"""
from __future__ import print_function

import os
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import l  # noqa: E402


def run(args, cwd=None, env=None):
    """Run l.py in a subprocess. Returns (code, stdout, stderr) as str."""
    e = dict(os.environ)
    e.pop('L_OPTS', None)
    if env:
        e.update(env)
    p = subprocess.Popen([sys.executable, os.path.join(HERE, 'l.py')] + args,
                         cwd=cwd, env=e, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    o, er = p.communicate()
    return p.returncode, o.decode('utf-8', 'replace'), er.decode('utf-8', 'replace')


class PureTests(unittest.TestCase):
    def test_size_plain(self):
        self.assertEqual(l.format_size(1468), '1468')
        self.assertEqual(l.format_size(0, True), '0')
        self.assertEqual(l.format_size(999, True), '999')

    def test_size_human(self):
        f = lambda n: l.format_size(n, True)  # noqa: E731
        self.assertEqual(f(1024), '1.0K')
        self.assertEqual(f(1025), '1.1K')
        self.assertEqual(f(1468), '1.5K')
        self.assertEqual(f(10240), '10K')
        self.assertEqual(f(10241), '11K')
        self.assertEqual(f(1048575), '1.0M')
        self.assertEqual(f(1024 ** 2 * 123), '123M')

    def test_time(self):
        s = l.format_time(time.time())
        self.assertEqual(len(s), 16)
        self.assertEqual(l.format_time(1e30), l.UNKNOWN_TIME)
        self.assertEqual(l.format_time(-1e30), l.UNKNOWN_TIME)

    def test_matcher(self):
        m = l.make_matcher('*.PY', True)
        self.assertTrue(m('a.py'))
        m = l.make_matcher('*.PY', False)
        self.assertFalse(m('a.py'))
        self.assertTrue(m('a.PY'))
        m = l.make_matcher('a?.txt', False)
        self.assertTrue(m('ab.txt'))
        self.assertFalse(m('abc.txt'))

    def test_sort(self):
        def E(n, d, s, t):
            return l.Entry(n, n, d, False, s, t, None)
        es = [E('b', False, 5, 3), E('a', False, 9, 1), E('d', True, 0, 2)]
        names = lambda: [e.name for e in es]  # noqa: E731
        l.sort_entries(es, 'time', False, False, False)
        self.assertEqual(names(), ['a', 'd', 'b'])
        l.sort_entries(es, 'time', True, False, False)
        self.assertEqual(names(), ['b', 'd', 'a'])
        l.sort_entries(es, 'size', False, False, False)
        self.assertEqual(names(), ['d', 'b', 'a'])
        l.sort_entries(es, 'name', False, True, False)
        self.assertEqual(names(), ['d', 'a', 'b'])
        l.sort_entries(es, 'name', True, True, False)
        self.assertEqual(names(), ['d', 'b', 'a'])

    def test_summary(self):
        self.assertEqual(l.summary_line(3, 2, 1468, 2048, False),
                         '3 files, 2 dirs, 1468 bytes, 2048 bytes free')
        self.assertEqual(l.summary_line(1, 0, 2048, None, True),
                         '1 files, 0 dirs, 2.0K')


class CliTests(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        self.mk('old.txt', 'x' * 1500, 1000000000)
        self.mk('new.py', 'y', 1600000000)
        self.mk('.hidden', '', 1300000000)
        os.mkdir(os.path.join(self.d, 'sub'))
        self.mk(os.path.join('sub', 'deep.py'), 'zz', 1500000000)

    def mk(self, name, data, mtime):
        p = os.path.join(self.d, name)
        with open(p, 'w') as f:
            f.write(data)
        os.utime(p, (mtime, mtime))

    def names(self, out):
        return [ln.split(None, 3)[-1] for ln in out.splitlines() if ln]

    def test_default_oldest_first_all_shown(self):
        code, out, err = run([], cwd=self.d)
        self.assertEqual(code, 0, err)
        order = [ln.split()[-1] for ln in out.splitlines()]
        # mtimes: old 1.0e9, deep n/a, .hidden 1.3e9, new 1.6e9, sub = now
        self.assertEqual(order[:3], ['old.txt', '.hidden', 'new.py'])
        self.assertEqual(set(order), set(['old.txt', 'new.py', '.hidden', 'sub']))

    def test_line_format(self):
        code, out, err = run(['old.txt'], cwd=self.d)
        self.assertEqual(code, 0, err)
        line = out.strip()
        self.assertTrue(line.endswith(' 1500 old.txt'), line)
        self.assertEqual(line[10], ' ')
        self.assertEqual(line[13], ':')

    def test_human(self):
        code, out, err = run(['-h', 'old.txt'], cwd=self.d)
        self.assertTrue(out.strip().endswith(' 1.5K old.txt'), out)

    def test_dir_mark_and_dirs_first(self):
        code, out, err = run(['--group-directories-first'], cwd=self.d)
        first = out.splitlines()[0]
        self.assertIn('<DIR>', first)
        self.assertTrue(first.endswith(' sub'), first)

    def test_reverse(self):
        code, out, err = run(['-r'], cwd=self.d)
        self.assertEqual(out.splitlines()[-1].split()[-1], 'old.txt')

    def test_wildcard_not_recursive(self):
        code, out, err = run(['*.py'], cwd=self.d)
        self.assertEqual(self.names(out), ['new.py'])

    def test_wildcard_recursive_flat(self):
        code, out, err = run(['-R', '*.py'], cwd=self.d)
        self.assertEqual(self.names(out),
                         [os.path.join('sub', 'deep.py'), 'new.py'])

    def test_posix_names(self):
        code, out, err = run(['-R', '--posix', '*.py'], cwd=self.d)
        self.assertEqual(self.names(out), ['sub/deep.py', 'new.py'])

    def test_names_only(self):
        code, out, err = run(['--names', '*.py'], cwd=self.d)
        self.assertEqual(out.splitlines(), ['new.py'])

    def test_dir_target(self):
        code, out, err = run(['sub'], cwd=self.d)
        self.assertEqual(self.names(out), ['deep.py'])
        code, out, err = run(['sub/'], cwd=self.d)
        self.assertEqual(self.names(out), ['deep.py'])

    def test_dir_target_slash_on_file_is_error(self):
        code, out, err = run(['new.py/'], cwd=self.d)
        self.assertEqual(code, 2)
        self.assertIn('not a directory', err)
        self.assertIn('1 error', err)

    def test_missing_literal(self):
        code, out, err = run(['nope.txt'], cwd=self.d)
        self.assertEqual(code, 2)
        self.assertIn('not found', err)

    def test_no_match(self):
        code, out, err = run(['*.zzz'], cwd=self.d)
        self.assertEqual(code, 1)
        self.assertIn('no match', err)

    def test_ignore_case(self):
        code, out, err = run(['--case-sensitive', 'NEW.*'], cwd=self.d)
        self.assertEqual(code, 1)
        code, out, err = run(['-i', 'NEW.*'], cwd=self.d)
        self.assertEqual(self.names(out), ['new.py'])

    def test_l_opts_env(self):
        code, out, err = run(['old.txt'], cwd=self.d, env={'L_OPTS': '-h'})
        self.assertTrue(out.strip().endswith(' 1.5K old.txt'), out)

    def test_by_dir(self):
        code, out, err = run(['--by-dir', '--names'], cwd=self.d)
        lines = out.splitlines()
        self.assertEqual(lines[0], '.:')
        self.assertIn(os.path.join('.', 'sub') + ':', lines)
        self.assertIn('deep.py', lines)

    def test_summary_flag(self):
        code, out, err = run(['-s', '*.py'], cwd=self.d)
        last = out.splitlines()[-1]
        self.assertTrue(last.startswith('1 files, 0 dirs, 1 bytes'), last)

    def test_no_summary_when_piped(self):
        code, out, err = run(['*.py'], cwd=self.d)
        self.assertEqual(len(out.splitlines()), 1)

    def test_symlink(self):
        link = os.path.join(self.d, 'lnk')
        try:
            os.symlink('new.py', link)
        except (OSError, AttributeError, NotImplementedError):
            self.skipTest('symlinks unavailable')
        if not hasattr(os, 'readlink'):
            self.skipTest('no readlink')
        code, out, err = run(['lnk'], cwd=self.d)
        self.assertTrue(out.strip().endswith(' lnk -> new.py'), out)

    def test_non_ascii_name_never_crashes(self):
        name = u'caf%c.txt' % 0xe9
        try:
            p = os.path.join(self.d if PY3 else self.d.decode(
                sys.getfilesystemencoding() or 'ascii'), name)
            open(p, 'w').close()
        except (UnicodeError, OSError, IOError):
            self.skipTest('cannot create non-ascii name')
        code, out, err = run(['--utf8', '*.txt'], cwd=self.d)
        self.assertIn(u'caf%c.txt' % 0xe9, out)
        env = {'PYTHONIOENCODING': 'ascii'}
        code, out, err = run(['*.txt'], cwd=self.d, env=env)
        self.assertEqual(code, 0, err)
        if sys.version_info >= (3,):
            self.assertIn('\\xe9', out)
            self.assertIn('shown lossy', err)

    def test_bad_flag(self):
        code, out, err = run(['--nope'], cwd=self.d)
        self.assertEqual(code, 2)


PY3 = sys.version_info >= (3,)

if __name__ == '__main__':
    unittest.main()

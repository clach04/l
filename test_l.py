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
            return l.Entry(n, d, False, s, t, None)
        es = [E('b', False, 5, 3), E('a', False, 9, 1), E('d', True, 0, 2)]
        names = lambda: [e.name for e in es]  # noqa: E731
        l.sort_entries(es, 'time', False, False)
        self.assertEqual(names(), ['a', 'd', 'b'])
        l.sort_entries(es, 'time', True, False)
        self.assertEqual(names(), ['b', 'd', 'a'])
        l.sort_entries(es, 'size', False, False)
        self.assertEqual(names(), ['d', 'b', 'a'])
        l.sort_entries(es, 'name', False, True)
        self.assertEqual(names(), ['d', 'a', 'b'])
        l.sort_entries(es, 'name', True, True)
        self.assertEqual(names(), ['d', 'b', 'a'])

    def test_summary(self):
        self.assertEqual(l.summary_line(3, 2, 1468, 2048, False),
                         '3 files | 2 dirs | 1,468 bytes | 2,048 bytes free')
        self.assertEqual(l.summary_line(1234, 2, 745045913600, 53630, False),
                         '1,234 files | 2 dirs | 745,045,913,600 bytes | '
                         '53,630 bytes free')
        self.assertEqual(l.summary_line(1, 0, 2048, None, True),
                         '1 files | 0 dirs | 2.0K')


class ColorTests(unittest.TestCase):
    def E(self, name, is_dir=False, is_link=False, mode=0o644):
        return l.Entry(name, is_dir, is_link, 1, 0, None, mode)

    def test_palette_defaults(self):
        p = l.Palette(None)
        self.assertEqual(p.style(self.E('d', is_dir=True)), '01;34')
        self.assertEqual(p.style(self.E('l', is_link=True)), '01;36')
        self.assertEqual(p.style(self.E('f')), '')

    def test_palette_ls_colors(self):
        p = l.Palette('di=01;35:*.tar=01;31:*.tar.gz=01;32:rs=0:or=40;31:'
                      'ln=:bad=\x1b[1m:*.x=\x1b[1m')
        self.assertEqual(p.style(self.E('d', is_dir=True)), '01;35')
        self.assertEqual(p.style(self.E('a.tar')), '01;31')
        self.assertEqual(p.style(self.E('a.tar.gz')), '01;32')
        self.assertEqual(p.style(self.E('l', is_link=True)), '')  # ln= empty
        self.assertEqual(p.style(self.E('a.x')), '')  # escape rejected
        self.assertEqual(p.types.get('rs'), None)  # unsupported key ignored

    @unittest.skipIf(os.name == 'nt', 'POSIX exec bit')
    def test_palette_executable_beats_glob(self):
        p = l.Palette('*.sh=0;33')
        self.assertEqual(p.style(self.E('a.sh', mode=0o755)), '01;32')
        self.assertEqual(p.style(self.E('a.sh')), '0;33')

    def test_mintty_pipe_name(self):
        bs = chr(92)
        f = l.is_mintty_pipe_name
        self.assertTrue(f(bs + 'msys-dd50a72ab4668b33-pty0-to-master'))
        self.assertTrue(f(bs + 'cygwin-0123456789abcdef-pty12-from-master'))
        self.assertTrue(f(bs + 'msys-1888ae32e00d56aa-pty1-to-master-nat'))
        self.assertFalse(f(bs + 'msys-dd50a72ab4668b33-pty0-pipe'))  # plain pipe
        self.assertFalse(f('msys-dd50a72ab4668b33-pty0-to-master'))  # no leading         self.assertFalse(f(''))

    def test_mintty_pty_false_for_pipe_and_file(self):
        import io
        self.assertFalse(l.is_mintty_pty(io.BytesIO()))  # no fileno
        with open(__file__, 'rb') as f:
            self.assertFalse(l.is_mintty_pty(f))  # real file

    def test_skip_matcher(self):
        m = l.make_skip_matcher(['.git', 'node_modules', '*.egg-info', 'b?d'])
        self.assertTrue(m('.git'))
        self.assertTrue(m('node_modules'))
        self.assertTrue(m('pkg.egg-info'))
        self.assertTrue(m('bad'))
        self.assertFalse(m('git'))
        self.assertFalse(m('node_modules2'))
        self.assertFalse(l.make_skip_matcher([])('.git'))
        self.assertEqual(m('.GIT'), os.name == 'nt')  # platform case rule

    def test_color_enabled(self):
        f = l.color_enabled
        self.assertTrue(f('always', {'NO_COLOR': '1'}, False, False, True))
        self.assertFalse(f('never', {}, True, True, False))
        self.assertFalse(f('auto', {}, False, True, False))  # not a tty
        self.assertFalse(f('auto', {'NO_COLOR': '1'}, True, True, False))
        self.assertTrue(f('auto', {}, True, False, False))  # POSIX tty
        self.assertFalse(f('auto', {}, True, False, True))  # bare Windows
        self.assertTrue(f('auto', {}, True, True, True))  # colorama
        self.assertTrue(f('auto', {'WT_SESSION': 'x'}, True, False, True))


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
        self.assertEqual(set(order), set(['old.txt', 'new.py', '.hidden', 'sub' + os.sep]))

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
        self.assertTrue(first.endswith(' sub' + os.sep), first)

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

    def test_missing_literal_is_no_match(self):
        code, out, err = run(['nope.txt'], cwd=self.d)
        self.assertEqual(code, 1)
        self.assertIn('no match', err)

    def test_implicit_substring(self):
        code, out, err = run(['ew'], cwd=self.d)
        self.assertEqual(self.names(out), ['new.py'])
        code, out, err = run(['-R', 'deep'], cwd=self.d)
        self.assertEqual(self.names(out), [os.path.join('sub', 'deep.py')])
        code, out, err = run(['sub/ee'], cwd=self.d)
        self.assertEqual(self.names(out), ['sub' + os.sep + 'deep.py'])

    def test_missing_dir_part(self):
        code, out, err = run(['nodir/x'], cwd=self.d)
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
        s = os.sep
        deep = 'sub' + s + 'deep.py'
        code, out, err = run(['-R', '--names'], cwd=self.d)
        # flat -R: one global time order, directories interleaved
        self.assertEqual(out.splitlines(),
                         ['old.txt', '.hidden', deep, 'new.py', 'sub' + s])
        code, out, err = run(['--by-dir', '--names'], cwd=self.d)
        # same flat lines, but one directory's entries stay together
        self.assertEqual(out.splitlines(),
                         ['old.txt', '.hidden', 'new.py', 'sub' + s, deep])
        self.assertNotIn(':', out)

    def test_summary_flag(self):
        code, out, err = run(['-s', '*.py'], cwd=self.d)
        lines = out.splitlines()
        self.assertTrue(lines[-1].startswith('1 files | 0 dirs | 1 bytes'), out)
        self.assertEqual(lines[-2], '')  # blank line before the footer

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

    def test_no_hidden(self):
        code, out, err = run(['--no-hidden', '--names'], cwd=self.d)
        self.assertNotIn('.hidden', out.splitlines())
        code, out, err = run(['--no-hidden', '--hidden', '--names'],
                             cwd=self.d)
        self.assertIn('.hidden', out.splitlines())

    def test_no_hidden_skips_hidden_dirs_under_recursion(self):
        os.makedirs(os.path.join(self.d, '.dot', 'in'))
        self.mk(os.path.join('.dot', 'in', 'f.txt'), 'q', 1400000000)
        code, out, err = run(['-R', '--names'], cwd=self.d)
        self.assertIn(os.path.join('.dot', 'in', 'f.txt'), out.splitlines())
        code, out, err = run(['-R', '--no-hidden', '--names'], cwd=self.d)
        self.assertFalse([n for n in out.splitlines() if 'dot' in n], out)

    def test_no_hidden_windows_attribute(self):
        if not (IS_WIN and PY3):
            self.skipTest('Windows py3 only')
        import ctypes
        p = os.path.join(self.d, 'attr.txt')
        open(p, 'w').close()
        ctypes.windll.kernel32.SetFileAttributesW(p, 2)
        code, out, err = run(['--names', 'attr'], cwd=self.d)
        self.assertEqual(out.splitlines(), ['attr.txt'])
        code, out, err = run(['--no-hidden', '--names', 'attr'], cwd=self.d)
        self.assertEqual(code, 1)

    def test_vcs_dirs_listed_not_entered(self):
        s = os.sep
        os.makedirs(os.path.join(self.d, '.git', 'hooks'))
        self.mk(os.path.join('.git', 'HEAD'), 'ref', 1400000000)
        os.makedirs(os.path.join(self.d, 'sub', '.svn'))
        self.mk(os.path.join('sub', '.svn', 'entries'), 'e', 1400000000)
        code, out, err = run(['-R', '--names'], cwd=self.d)
        names = out.splitlines()
        self.assertIn('.git' + s, names)
        self.assertIn('sub' + s + '.svn' + s, names)
        self.assertFalse([n for n in names if 'HEAD' in n or 'entries' in n
                          or 'hooks' in n], names)
        self.assertIn('sub' + s + 'deep.py', names)

    def test_vcs_dir_as_target_is_entered(self):
        s = os.sep
        os.makedirs(os.path.join(self.d, '.git', 'hooks'))
        self.mk(os.path.join('.git', 'HEAD'), 'ref', 1400000000)
        os.makedirs(os.path.join(self.d, '.git', 'x', '.git'))
        for target in ('.git/', '.git'):
            code, out, err = run(['-R', '--names', target], cwd=self.d)
            names = out.splitlines()
            self.assertIn('HEAD', names)
            self.assertIn('hooks' + s, names)
            self.assertIn('x' + s + '.git' + s, names)  # nested not entered
        code, out, err = run(['--names', '.git/hooks'], cwd=self.d)
        self.assertEqual(code, 0, err)

    def make_pycache(self):
        os.makedirs(os.path.join(self.d, '__pycache__'))
        self.mk(os.path.join('__pycache__', 'x.pyc'), 'c', 1400000000)
        os.makedirs(os.path.join(self.d, '.git'))
        self.mk(os.path.join('.git', 'HEAD'), 'ref', 1400000000)

    def rnames(self, args, env=None):
        code, out, err = run(['-R', '--names'] + args, cwd=self.d, env=env)
        self.assertEqual(code, 0, err)
        return out.splitlines()

    def test_skip_default_includes_pycache(self):
        self.make_pycache()
        names = self.rnames([])
        self.assertIn('__pycache__' + os.sep, names)
        self.assertNotIn(os.path.join('__pycache__', 'x.pyc'), names)

    def test_skip_replaces_defaults(self):
        self.make_pycache()
        names = self.rnames(['--skip=nomatch'])
        self.assertIn(os.path.join('__pycache__', 'x.pyc'), names)
        self.assertIn(os.path.join('.git', 'HEAD'), names)

    def test_skip_empty_disables(self):
        self.make_pycache()
        names = self.rnames(['--skip='])
        self.assertIn(os.path.join('__pycache__', 'x.pyc'), names)

    def test_skip_replace_with_list_and_wildcard(self):
        self.make_pycache()
        names = self.rnames(['--skip=.git,__py*'])
        self.assertNotIn(os.path.join('__pycache__', 'x.pyc'), names)
        self.assertNotIn(os.path.join('.git', 'HEAD'), names)
        self.assertIn(os.path.join('sub', 'deep.py'), names)

    def test_skip_also_appends(self):
        self.make_pycache()
        names = self.rnames(['--skip-also=sub,nothing'])
        self.assertNotIn(os.path.join('sub', 'deep.py'), names)
        self.assertIn('sub' + os.sep, names)
        self.assertNotIn(os.path.join('.git', 'HEAD'), names)  # default kept

    def test_skip_via_env_and_target_exempt(self):
        self.make_pycache()
        names = self.rnames([], env={'L_OPTS': '--skip-also=sub'})
        self.assertNotIn(os.path.join('sub', 'deep.py'), names)
        code, out, err = run(['-R', '--names', 'sub', '--skip-also=sub'],
                             cwd=self.d)
        self.assertEqual(out.splitlines(), ['deep.py'])

    def test_color_never_and_auto_piped(self):
        code, out, err = run(['--names', 'new'], cwd=self.d)
        self.assertEqual(out.splitlines(), ['new.py'])  # auto, piped: plain
        code, out, err = run(['--color=never', '--names', 'new'], cwd=self.d)
        self.assertEqual(out.splitlines(), ['new.py'])

    def test_color_always_uses_ls_colors(self):
        env = {'LS_COLORS': 'di=01;35:*.py=0;33'}
        code, out, err = run(['--color=always', '--names'], cwd=self.d,
                             env=env)
        names = out.splitlines()
        self.assertIn('\x1b[0;33mnew.py\x1b[0m', names)
        self.assertIn('\x1b[01;35msub' + os.sep + '\x1b[0m', names)
        self.assertIn('old.txt', names)  # no matching style: plain

    def test_color_always_defaults_and_no_color_env(self):
        env = {'NO_COLOR': '1'}  # ignored by --color=always
        code, out, err = run(['--color=always', '--names', 'sub'], cwd=self.d,
                             env=env)
        self.assertEqual(out.splitlines(), ['deep.py'])
        code, out, err = run(['--color=always', '--names', '-R', 'su'],
                             cwd=self.d, env=env)
        self.assertIn('\x1b[01;34msub' + os.sep + '\x1b[0m', out.splitlines())

    def test_color_line_keeps_columns_plain(self):
        code, out, err = run(['--color=always', 'new.py'], cwd=self.d,
                             env={'LS_COLORS': '*.py=0;33'})
        line = out.strip()
        self.assertTrue(line.endswith(' 1 \x1b[0;33mnew.py\x1b[0m'), repr(line))
        self.assertEqual(line[13], ':')  # time column not colored

    def test_bad_flag(self):
        code, out, err = run(['--nope'], cwd=self.d)
        self.assertEqual(code, 2)


PY3 = sys.version_info >= (3,)
IS_WIN = os.name == 'nt'

if __name__ == '__main__':
    unittest.main()

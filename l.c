/*
 * l.c - ls/dir-like directory lister. C99, no libraries beyond the OS.
 * Port of the core of l.py (see README.md). Build: make
 * or, by hand:  cc -std=c99 -O2 -o l l.c
 *
 * Names are UTF-8 internally. On Windows the wide-char API is used and names
 * are converted; on POSIX names are raw bytes and pass through unchanged.
 *
 * Not ported: --utf8 (accepted and ignored; output is always UTF-8).
 * TODO: sub-second mtime sort is Linux only; other POSIX uses whole seconds.
 */
#ifdef _WIN32
#define IS_WIN 1
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <winioctl.h>
#include <io.h>
#include <fcntl.h>
#else
#define IS_WIN 0
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROG "l"
/* ?-? would start a trigraph, so every ? is escaped */
#define UNKNOWN_TIME "\?\?\?\?-\?\?-\?\? \?\?:\?\?"
#define DIR_MARK "<DIR>"
#if IS_WIN
#define SEP '\\'
#else
#define SEP '/'
#endif

typedef unsigned long long u64;

/* ------------------------------------------------------------ memory */

static void die(const char *msg)
{
    fprintf(stderr, PROG ": fatal: %s\n", msg);
    exit(2);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        die("out of memory");
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

typedef struct {
    char *p;
    size_t n, cap;
} Buf;

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (b->n + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 256;
        while (c < b->n + n + 1)
            c *= 2;
        b->p = xrealloc(b->p, c);
        b->cap = c;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = '\0';
}

static void buf_puts(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
static void buf_putc(Buf *b, char c) { buf_add(b, &c, 1); }
static const char *buf_str(const Buf *b) { return b->p ? b->p : ""; }

static void buf_free(Buf *b)
{
    free(b->p);
    b->p = NULL;
    b->n = b->cap = 0;
}

static char *vfmt(const char *fmt, va_list ap)
{
    va_list ap2;
    int n;
    char *s;
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0)
        die("format error");
    s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

/* ------------------------------------------------------------ Windows glue */

#if IS_WIN
static wchar_t *u2w(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w;
    if (n <= 0)
        die("cannot convert name from UTF-8");
    w = xmalloc((size_t)n * sizeof *w);
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *w2u(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s;
    if (n <= 0)
        die("cannot convert name to UTF-8");
    s = xmalloc((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

/* Text for the last OS error. Call right after the failing call. */
static char *os_error(void)
{
#if IS_WIN
    DWORD code = GetLastError();
    wchar_t *w = NULL;
    char *s;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER
                             | FORMAT_MESSAGE_FROM_SYSTEM
                             | FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, code, 0, (LPWSTR)&w, 0, NULL);
    if (!n || !w)
        return xstrdup("unknown error");
    while (n && (w[n - 1] == L'\r' || w[n - 1] == L'\n'
                 || w[n - 1] == L'.' || w[n - 1] == L' '))
        w[--n] = 0;
    s = w2u(w);
    LocalFree(w);
    return s;
#else
    return xstrdup(strerror(errno));
#endif
}

/* ------------------------------------------------------------ output */

static Buf g_out;
static int g_errors, g_code;

/* Write n bytes to fd 1 or 2. Windows console gets UTF-16. */
static void emit(int fd, const char *s, size_t n)
{
#if IS_WIN
    HANDLE h = GetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        while (n) {
            size_t k = n > 8192 ? 8192 : n;
            int wn;
            wchar_t *w;
            DWORD done;
            while (k > 1 && k < n && ((unsigned char)s[k] & 0xC0) == 0x80)
                k--; /* do not split a UTF-8 sequence */
            wn = MultiByteToWideChar(CP_UTF8, 0, s, (int)k, NULL, 0);
            w = xmalloc(((size_t)wn + 1) * sizeof *w);
            MultiByteToWideChar(CP_UTF8, 0, s, (int)k, w, wn);
            WriteConsoleW(h, w, (DWORD)wn, &done, NULL);
            free(w);
            s += k;
            n -= k;
        }
        return;
    }
#endif
    fwrite(s, 1, n, fd == 1 ? stdout : stderr);
}

static void out_flush(void)
{
    if (g_out.n)
        emit(1, g_out.p, g_out.n);
    g_out.n = 0;
    fflush(stdout);
}

static void out_line(const char *s)
{
    buf_puts(&g_out, s);
    buf_putc(&g_out, '\n');
    if (g_out.n > 65536)
        out_flush();
}

static void err_line(const char *msg)
{
    out_flush();
    emit(2, msg, strlen(msg));
    emit(2, "\n", 1);
    fflush(stderr);
}

static void status_error(int code, const char *fmt, ...)
{
    va_list ap;
    char *body, *line;
    size_t n;
    va_start(ap, fmt);
    body = vfmt(fmt, ap);
    va_end(ap);
    n = strlen(body) + sizeof(PROG ": ");
    line = xmalloc(n);
    snprintf(line, n, PROG ": %s", body);
    g_errors++;
    if (code > g_code)
        g_code = code;
    err_line(line);
    free(body);
    free(line);
}

/* ------------------------------------------------------------ text helpers */

static double dceil(double x)
{
    long long i = (long long)x;
    return x > (double)i ? (double)(i + 1) : (double)i;
}

/* Decimal digits, optionally comma grouped. out needs 32 bytes. */
static void fmt_u64(char *out, u64 n, int commas)
{
    char tmp[24];
    int i = 0, j = 0, k;
    do {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    for (k = i - 1; k >= 0; k--) {
        out[j++] = tmp[k];
        if (commas && k > 0 && k % 3 == 0)
            out[j++] = ',';
    }
    out[j] = '\0';
}

/* Like ls -lh: 1024 units K M G T P, round up. out needs 32 bytes. */
static void format_size(char *out, u64 n, int human, int commas)
{
    static const char units[] = "KMGTP";
    double v;
    int u;
    if (!human || n < 1024) {
        fmt_u64(out, n, commas);
        return;
    }
    v = (double)n;
    for (u = 0; u < 5; u++) {
        double r;
        v /= 1024.0;
        if (v < 10) {
            r = dceil(v * 10) / 10.0;
            if (r < 10) {
                snprintf(out, 32, "%.1f%c", r, units[u]);
                return;
            }
        }
        r = dceil(v);
        if (r < 1024 || u == 4) {
            snprintf(out, 32, "%d%c", (int)r, units[u]);
            return;
        }
    }
}

static void format_time(char *out, long long sec)
{
    time_t t = (time_t)sec;
    struct tm tmv, *tm;
    if ((long long)t != sec) {
        strcpy(out, UNKNOWN_TIME);
        return;
    }
#if IS_WIN
    tm = localtime(&t);
    if (tm)
        tmv = *tm;
#else
    tm = localtime_r(&t, &tmv);
#endif
    if (!tm || !strftime(out, 32, "%Y-%m-%d %H:%M", &tmv))
        strcpy(out, UNKNOWN_TIME);
}

/* ------------------------------------------------------------ UTF-8, wildcards */

/* Decode one UTF-8 char. Returns its length, or 0 when invalid. */
static int u8dec(const unsigned char *s, unsigned *cp)
{
    unsigned c = s[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xC2 && c <= 0xDF && (s[1] & 0xC0) == 0x80) {
        *cp = ((c & 0x1Fu) << 6) | (s[1] & 0x3Fu);
        return 2;
    }
    if (c >= 0xE0 && c <= 0xEF && (s[1] & 0xC0) == 0x80
        && (s[2] & 0xC0) == 0x80) {
        *cp = ((c & 0x0Fu) << 12) | ((s[1] & 0x3Fu) << 6) | (s[2] & 0x3Fu);
        if (*cp < 0x800 || (*cp >= 0xD800 && *cp <= 0xDFFF))
            return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4 && (s[1] & 0xC0) == 0x80
        && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *cp = ((c & 0x07u) << 18) | ((s[1] & 0x3Fu) << 12)
              | ((s[2] & 0x3Fu) << 6) | (s[3] & 0x3Fu);
        if (*cp < 0x10000 || *cp > 0x10FFFF)
            return 0;
        return 4;
    }
    return 0;
}

static int u8len(const char *s)
{
    unsigned cp;
    int n = u8dec((const unsigned char *)s, &cp);
    return n ? n : 1;
}

/* ASCII-only case folding (the Python version folds all of Unicode). */
static unsigned fold_cp(unsigned c, int ic)
{
    return (ic && c >= 'A' && c <= 'Z') ? c + 32 : c;
}

/* Match one char of s against the [..] class at p. Returns 1 or 0, or -1
 * when p is not a valid class (then '[' is a literal). *end = past ']'. */
static int bracket(const char *p, const char *s, int ic, const char **end)
{
    const unsigned char *q = (const unsigned char *)p + 1;
    unsigned sc, lo, hi;
    int neg = 0, ok = 0, first = 1, n;

    n = u8dec((const unsigned char *)s, &sc);
    if (!n)
        sc = 0x110000u + (unsigned char)*s; /* invalid byte: its own value */
    sc = fold_cp(sc, ic);
    if (*q == '!' || *q == '^') {
        neg = 1;
        q++;
    }
    for (;;) {
        if (*q == 0)
            return -1;
        if (*q == ']' && !first)
            break;
        first = 0;
        n = u8dec(q, &lo);
        if (!n) {
            lo = 0x110000u + *q;
            n = 1;
        }
        q += n;
        hi = lo;
        if (*q == '-' && q[1] != ']' && q[1] != 0) {
            q++;
            n = u8dec(q, &hi);
            if (!n) {
                hi = 0x110000u + *q;
                n = 1;
            }
            q += n;
        }
        lo = fold_cp(lo, ic);
        hi = fold_cp(hi, ic);
        if (sc >= lo && sc <= hi)
            ok = 1;
    }
    *end = (const char *)q + 1;
    return ok != neg;
}

/* fnmatch-style: * ? [seq] [!seq]. ic: ASCII case-insensitive. */
static int wild(const char *p, const char *s, int ic)
{
    const char *sp = NULL, *ss = NULL; /* position after the last '*' */
    while (*s) {
        if (*p == '*') {
            while (*p == '*')
                p++;
            if (!*p)
                return 1;
            sp = p;
            ss = s;
            continue;
        }
        if (*p == '?') {
            p++;
            s += u8len(s);
            continue;
        }
        if (*p == '[') {
            const char *end;
            int m = bracket(p, s, ic, &end);
            if (m == 1) {
                p = end;
                s += u8len(s);
                continue;
            }
            if (m == 0)
                goto backtrack;
            /* m == -1: literal '[' */
        }
        if (*p && fold_cp((unsigned char)*p, ic) == fold_cp((unsigned char)*s, ic)) {
            p++;
            s++;
            continue;
        }
    backtrack:
        if (!sp)
            return 0;
        ss += u8len(ss);
        s = ss;
        p = sp;
    }
    while (*p == '*')
        p++;
    return *p == 0;
}

static int has_magic(const char *s) { return strpbrk(s, "*?[") != NULL; }

static int streq_ic(const char *a, const char *b, int ic)
{
    for (; *a && *b; a++, b++)
        if (fold_cp((unsigned char)*a, ic) != fold_cp((unsigned char)*b, ic))
            return 0;
    return *a == *b;
}

/* ------------------------------------------------------------ quoting */

static int quote_safe_char(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || strchr("_@%+=:,./-", c))
        return 1;
    return IS_WIN && c == '\\'; /* Windows: separator, needs no quoting */
}

/* Escape one char (len bytes at s, cp or invalid) into out. style 1=sh, 2=c */
static void quote_escape(Buf *out, const char *s, int len, unsigned cp,
                         int valid, int style)
{
    char tmp[16];
    if (valid && cp == '\\') {
        /* Windows: backslash is the path separator, literal in c style */
        buf_puts(out, (style == 2 && IS_WIN) ? "\\" : "\\\\");
    } else if (valid && cp == (style == 1 ? '\'' : '"')) {
        buf_putc(out, '\\');
        buf_putc(out, (char)cp);
    } else if (valid && cp == '\n') {
        buf_puts(out, "\\n");
    } else if (valid && cp == '\t') {
        buf_puts(out, "\\t");
    } else if (valid && cp == '\r') {
        buf_puts(out, "\\r");
    } else if (!valid || cp < 32 || cp == 127) { /* byte escape */
        unsigned b = (unsigned char)s[0];
        if (style == 1)
            snprintf(tmp, sizeof tmp, "\\x%02x", b);
        else
            snprintf(tmp, sizeof tmp, "\\%03o", b);
        buf_puts(out, tmp);
    } else if (cp >= 0x80 && cp < 0xA0) { /* C1 control */
        snprintf(tmp, sizeof tmp, "\\u%04x", cp);
        buf_puts(out, tmp);
    } else {
        buf_add(out, s, (size_t)len);
    }
}

/* Quote name for pasting into a shell. style 1 = sh, 2 = c. */
static void quote_name(Buf *out, const char *name, int style)
{
    const char *s;
    int odd = 0, need = 0, n;
    unsigned cp;
    Buf esc = {0};

    for (s = name; *s; s += n) {
        n = u8dec((const unsigned char *)s, &cp);
        if (!n) {
            odd = 1;
            n = 1;
        } else if (cp < 32 || cp == 127 || (cp >= 0x80 && cp < 0xA0)) {
            odd = 1;
        } else if (cp < 0x80 && !quote_safe_char((unsigned char)cp)) {
            need = 1;
        }
    }
    if (!odd && !need) {
        buf_puts(out, name);
        return;
    }
    if (style == 1 && !odd) {
        buf_putc(out, '\'');
        for (s = name; *s; s++) {
            if (*s == '\'')
                buf_puts(out, "'\\''");
            else
                buf_putc(out, *s);
        }
        buf_putc(out, '\'');
        return;
    }
    for (s = name; *s; s += n) {
        n = u8dec((const unsigned char *)s, &cp);
        quote_escape(&esc, s, n ? n : 1, cp, n != 0, style);
        if (!n)
            n = 1;
    }
    buf_puts(out, style == 2 ? "\"" : "$'");
    buf_puts(out, buf_str(&esc));
    buf_puts(out, style == 2 ? "\"" : "'");
    buf_free(&esc);
}

/* ------------------------------------------------------------ paths */

static int ends_sep(const char *s, size_t n)
{
    if (!n)
        return 0;
    if (s[n - 1] == '/')
        return 1;
    return IS_WIN && (s[n - 1] == '\\' || s[n - 1] == ':');
}

static int is_sep(char c) { return c == '/' || (IS_WIN && c == '\\'); }

static char *path_join(const char *a, const char *b)
{
    size_t na = strlen(a), nb = strlen(b);
    char *r;
    if (!na)
        return xstrdup(b);
    r = xmalloc(na + nb + 2);
    memcpy(r, a, na);
    if (!ends_sep(a, na))
        r[na++] = SEP;
    memcpy(r + na, b, nb + 1);
    return r;
}

/* Like os.path.split: *dir and *base are new strings. */
static void path_split(const char *p, char **dir, char **base)
{
    size_t drive = (IS_WIN && p[0] && p[1] == ':') ? 2 : 0;
    size_t i = strlen(p), h;
    int all_sep = 1;
    while (i > drive && !is_sep(p[i - 1]))
        i--;
    *base = xstrdup(p + i);
    h = i;
    for (i = drive; i < h; i++)
        if (!is_sep(p[i]))
            all_sep = 0;
    if (h > drive && !all_sep)
        while (h > drive && is_sep(p[h - 1]))
            h--;
    *dir = memcpy(xmalloc(h + 1), p, h);
    (*dir)[h] = '\0';
}

/* ------------------------------------------------------------ entries */

typedef struct {
    char *name;   /* shown name (relative path), UTF-8 */
    char *key;    /* sort key: name, case folded on Windows */
    char *target; /* link text or NULL */
    u64 size;
    long long sec;
    long nsec;
    int is_dir, is_link, hidden, is_exec; /* is_exec: POSIX x bit */
} Entry;

typedef struct {
    Entry *v;
    size_t n, cap;
} EList;

static void elist_add(EList *l, const Entry *e)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = *e;
}

static char *fold_key(const char *name)
{
#if IS_WIN
    wchar_t *w = u2w(name);
    char *k;
    CharLowerBuffW(w, (DWORD)wcslen(w));
    k = w2u(w);
    free(w);
    return k;
#else
    return xstrdup(name);
#endif
}

#if IS_WIN

#define REPARSE_SYMLINK 0xA000000CUL
#define REPARSE_MOUNT 0xA0000003UL

/* Link text as stored (substitute name, "\??\" stripped), or NULL. */
static char *read_link(const char *path)
{
    wchar_t *wp = u2w(path);
    HANDLE h = CreateFileW(wp, 0, FILE_SHARE_READ | FILE_SHARE_WRITE
                           | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT
                           | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    unsigned char *buf;
    DWORD got = 0, tag;
    USHORT off, len;
    wchar_t *sub;
    char *r;
    size_t pathoff;
    free(wp);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    buf = xmalloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, buf,
                         MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &got, NULL)) {
        CloseHandle(h);
        free(buf);
        return NULL;
    }
    CloseHandle(h);
    memcpy(&tag, buf, sizeof tag);
    /* header 8 bytes, then 4 USHORTs; symlinks have 4 more bytes (flags) */
    if (tag == REPARSE_SYMLINK)
        pathoff = 20;
    else if (tag == REPARSE_MOUNT)
        pathoff = 16;
    else {
        free(buf);
        return NULL;
    }
    memcpy(&off, buf + 8, sizeof off);
    memcpy(&len, buf + 10, sizeof len);
    sub = xmalloc((size_t)len + sizeof(wchar_t));
    memcpy(sub, buf + pathoff + off, len);
    sub[len / sizeof(wchar_t)] = 0;
    free(buf);
    {
        wchar_t *s = sub;
        if (wcsncmp(s, L"\\??\\UNC\\", 8) == 0) {
            s += 6;
            *s = L'\\'; /* "\\server\share" */
        } else if (wcsncmp(s, L"\\??\\", 4) == 0) {
            s += 4;
        }
        r = w2u(s);
    }
    free(sub);
    return r;
}

/* full: path of the entry (used only to read a link target). */
static void fill_find(const WIN32_FIND_DATAW *fd, const char *full, Entry *e)
{
    ULARGE_INTEGER t;
    DWORD a = fd->dwFileAttributes;
    memset(e, 0, sizeof *e);
    e->name = w2u(fd->cFileName);
    e->is_link = (a & FILE_ATTRIBUTE_REPARSE_POINT)
                 && (fd->dwReserved0 == REPARSE_SYMLINK
                     || fd->dwReserved0 == REPARSE_MOUNT);
    e->is_dir = (a & FILE_ATTRIBUTE_DIRECTORY) && !e->is_link;
    e->size = ((u64)fd->nFileSizeHigh << 32) | fd->nFileSizeLow;
    t.LowPart = fd->ftLastWriteTime.dwLowDateTime;
    t.HighPart = fd->ftLastWriteTime.dwHighDateTime;
    e->sec = (long long)(t.QuadPart / 10000000ULL) - 11644473600LL;
    e->nsec = (long)((t.QuadPart % 10000000ULL) * 100);
    e->hidden = (a & FILE_ATTRIBUTE_HIDDEN) || e->name[0] == '.';
    if (e->is_link)
        e->target = read_link(full);
}

static int lexists(const char *path)
{
    wchar_t *w = u2w(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES;
}

static int is_dir(const char *path)
{
    wchar_t *w = u2w(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Entry for one path (lstat). 0 ok, -1 error with *errmsg. */
static int stat_path(const char *path, Entry *e, char **errmsg)
{
    wchar_t *w = u2w(path);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(w, &fd);
    free(w);
    if (h == INVALID_HANDLE_VALUE) {
        *errmsg = os_error();
        return -1;
    }
    FindClose(h);
    fill_find(&fd, path, e);
    return 0;
}

/* List a directory (no . or ..). Per-entry failures are reported. */
static int read_dir(const char *path, EList *out, char **errmsg)
{
    char *pat = path_join(path, "*");
    wchar_t *wp = u2w(pat);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wp, &fd);
    free(wp);
    free(pat);
    if (h == INVALID_HANDLE_VALUE) {
        *errmsg = os_error();
        return -1;
    }
    do {
        Entry e;
        char *name = w2u(fd.cFileName), *full;
        if (!strcmp(name, ".") || !strcmp(name, "..")) {
            free(name);
            continue;
        }
        full = path_join(path, name);
        fill_find(&fd, full, &e);
        free(name);
        free(full);
        elist_add(out, &e);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

static int free_bytes(const char *path, u64 *out)
{
    ULARGE_INTEGER avail, total, freeb;
    wchar_t *w = u2w(path);
    BOOL ok = GetDiskFreeSpaceExW(w, &avail, &total, &freeb);
    free(w);
    if (!ok)
        return 0;
    *out = freeb.QuadPart;
    return 1;
}

#else /* POSIX */

static int stat_entry(const char *full, const char *base, Entry *e)
{
    struct stat st;
    char buf[4096];
    ssize_t k;
    if (lstat(full, &st) != 0)
        return -1;
    memset(e, 0, sizeof *e);
    e->name = xstrdup(base);
    e->is_link = S_ISLNK(st.st_mode);
    e->is_dir = S_ISDIR(st.st_mode) && !e->is_link;
    e->size = st.st_size < 0 ? 0 : (u64)st.st_size;
    e->sec = (long long)st.st_mtime;
#if defined(__linux__)
    e->nsec = (long)st.st_mtim.tv_nsec;
#endif
    e->hidden = base[0] == '.';
    e->is_exec = (st.st_mode & 0111) != 0;
    if (e->is_link) {
        k = readlink(full, buf, sizeof buf - 1);
        if (k >= 0) {
            buf[k] = '\0';
            e->target = xstrdup(buf);
        }
    }
    return 0;
}

static int lexists(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int stat_path(const char *path, Entry *e, char **errmsg)
{
    if (stat_entry(path, path, e) != 0) {
        *errmsg = os_error();
        return -1;
    }
    return 0;
}

static int read_dir(const char *path, EList *out, char **errmsg)
{
    DIR *d = opendir(path);
    struct dirent *de;
    if (!d) {
        *errmsg = os_error();
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        const char *n = de->d_name;
        char *full;
        Entry e;
        if (n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0')))
            continue;
        full = path_join(path, n);
        if (stat_entry(full, n, &e) != 0) {
            char *m = os_error();
            status_error(2, "%s: %s", full, m);
            free(m);
        } else {
            elist_add(out, &e);
        }
        free(full);
    }
    closedir(d);
    return 0;
}

static int free_bytes(const char *path, u64 *out)
{
    struct statvfs sv;
    if (statvfs(path, &sv) != 0)
        return 0;
    *out = (u64)sv.f_bavail * (u64)sv.f_frsize;
    return 1;
}

#endif

/* ------------------------------------------------------------ options */

typedef struct {
    int recursive, by_dir, reverse, group_dirs, human, commas;
    int ignore_case; /* -1 native, 0 sensitive, 1 insensitive */
    int no_hidden, summary, names, posix;
    int quote; /* 0 off, 1 sh, 2 c */
    int sort;  /* 0 time, 1 name, 2 size */
    int color; /* 0 auto, 1 always, 2 never */
    const struct Palette *pal; /* NULL: no color */
    const char **targets;
    int ntargets;
    char **skip;       /* skip patterns */
    int nskip;
    int skip_given;    /* --skip replaces the default list */
} Args;

static const char *DEFAULT_SKIP[] = {
    ".git", ".hg", ".svn", ".bzr", "CVS", "__pycache__", ".pytest_cache",
    ".mypy_cache", ".ruff_cache", ".tox", ".venv", "node_modules", NULL};

static const char HELP[] =
    "usage: l [options] [TARGET ...]\n"
    "\n"
    "List files like ls -altr / dir /od. TARGET is a directory, a file, or a\n"
    "wildcard (not recursive unless -R). Anything else that does not exist\n"
    "is an implicit substring match (foo means *foo*).\n"
    "\n"
    "options:\n"
    "  --help                     show this help\n"
    "  -R, --recursive            recurse; flat list of relative paths\n"
    "  --by-dir                   like -R, keep each directory's entries together\n"
    "  -r, --reverse              reverse sort order\n"
    "  --sort {time,name,size}    sort key (default: time, oldest first)\n"
    "  --group-directories-first  directories first (ignored by flat -R)\n"
    "  -h, --human-readable       human readable sizes\n"
    "  --commas                   comma-group digits in the size column\n"
    "  -i, --ignore-case          case-insensitive wildcard matching\n"
    "  --case-sensitive           case-sensitive wildcard matching\n"
    "  --no-hidden                hide dotfiles and Windows hidden entries\n"
    "  --hidden                   show hidden entries (default)\n"
    "  --color WHEN               color names using LS_COLORS: auto (tty only,\n"
    "                             honors NO_COLOR), always, never\n"
    "  --skip A,B,C               -R lists but does not enter these dirs\n"
    "                             (wildcards ok); replaces the default list\n"
    "  --skip-also A,B,C          add names to the skip list\n"
    "  -s, --summary              always print summary (default: tty only)\n"
    "  --names                    names only, one per line\n"
    "  --posix                    use / in printed names on Windows\n"
    "  -q, --quote                quote names for copy/paste\n"
    "  --quote-style STYLE        sh or c; implies --quote\n"
    "  --utf8                     accepted; output is always UTF-8\n";

enum {
    O_R = 1, O_BYDIR, O_REV, O_SORT, O_GDF, O_HUMAN, O_COMMAS, O_IC, O_CS,
    O_NOHID, O_HID, O_COLOR, O_SKIP, O_SKIPALSO, O_SUMMARY, O_NAMES, O_POSIX,
    O_QUOTE, O_QSTYLE, O_UTF8, O_HELP
};

typedef struct {
    const char *name;
    int id;
    int value; /* takes an argument */
} LongOpt;

static const LongOpt LONG_OPTS[] = {
    {"help", O_HELP, 0}, {"recursive", O_R, 0}, {"by-dir", O_BYDIR, 0},
    {"reverse", O_REV, 0}, {"sort", O_SORT, 1},
    {"group-directories-first", O_GDF, 0}, {"human-readable", O_HUMAN, 0},
    {"commas", O_COMMAS, 0}, {"ignore-case", O_IC, 0},
    {"case-sensitive", O_CS, 0}, {"no-hidden", O_NOHID, 0},
    {"hidden", O_HID, 0}, {"color", O_COLOR, 1}, {"skip", O_SKIP, 1},
    {"skip-also", O_SKIPALSO, 1}, {"summary", O_SUMMARY, 0},
    {"names", O_NAMES, 0}, {"posix", O_POSIX, 0}, {"quote", O_QUOTE, 0},
    {"quote-style", O_QSTYLE, 1}, {"utf8", O_UTF8, 0}, {NULL, 0, 0}};

static void usage_error(const char *fmt, ...)
{
    va_list ap;
    char *body;
    va_start(ap, fmt);
    body = vfmt(fmt, ap);
    va_end(ap);
    fprintf(stderr, "usage: l [options] [TARGET ...]\n" PROG ": error: %s\n",
            body);
    exit(2);
}

static void add_skip(Args *a, const char *csv)
{
    const char *s = csv;
    while (*s) {
        const char *e = strchr(s, ',');
        size_t n = e ? (size_t)(e - s) : strlen(s);
        if (n) {
            a->skip = xrealloc(a->skip, (size_t)(a->nskip + 1) * sizeof *a->skip);
            a->skip[a->nskip] = memcpy(xmalloc(n + 1), s, n);
            a->skip[a->nskip++][n] = '\0';
        }
        s += n;
        if (*s == ',')
            s++;
    }
}

static void apply_opt(Args *a, int id, const char *name, const char *val)
{
    switch (id) {
    case O_R: a->recursive = 1; break;
    case O_BYDIR: a->by_dir = 1; break;
    case O_REV: a->reverse = 1; break;
    case O_GDF: a->group_dirs = 1; break;
    case O_HUMAN: a->human = 1; break;
    case O_COMMAS: a->commas = 1; break;
    case O_IC: a->ignore_case = 1; break;
    case O_CS: a->ignore_case = 0; break;
    case O_NOHID: a->no_hidden = 1; break;
    case O_HID: a->no_hidden = 0; break;
    case O_SUMMARY: a->summary = 1; break;
    case O_NAMES: a->names = 1; break;
    case O_POSIX: a->posix = 1; break;
    case O_UTF8: break;
    case O_QUOTE: a->quote = IS_WIN ? 2 : 1; break;
    case O_HELP:
        fputs(HELP, stdout);
        exit(0);
    case O_SORT:
        if (!strcmp(val, "time")) a->sort = 0;
        else if (!strcmp(val, "name")) a->sort = 1;
        else if (!strcmp(val, "size")) a->sort = 2;
        else usage_error("argument --sort: invalid choice: '%s' (choose from "
                         "'time', 'name', 'size')", val);
        break;
    case O_COLOR:
        if (strcmp(val, "auto") && strcmp(val, "always") && strcmp(val, "never"))
            usage_error("argument --color: invalid choice: '%s' (choose from "
                        "'auto', 'always', 'never')", val);
        a->color = !strcmp(val, "always") ? 1 : !strcmp(val, "never") ? 2 : 0;
        break;
    case O_QSTYLE:
        if (!strcmp(val, "sh")) a->quote = 1;
        else if (!strcmp(val, "c")) a->quote = 2;
        else usage_error("argument --quote-style: invalid choice: '%s' "
                         "(choose from 'sh', 'c')", val);
        break;
    case O_SKIP: /* replaces the default list; the last --skip wins */
        a->skip_given = 1;
        a->nskip = 0;
        add_skip(a, val);
        break;
    default:
        usage_error("internal: option %s", name);
    }
}

static void parse_args(Args *a, int argc, char **argv)
{
    int i, only_pos = 0;
    /* --skip-also values are collected apart and appended last */
    char **also = NULL;
    int nalso = 0;

    a->ignore_case = -1;
    for (i = 1; i < argc; i++) {
        const char *s = argv[i];
        if (only_pos || s[0] != '-' || s[1] == '\0') {
            a->targets = xrealloc((void *)a->targets,
                                  (size_t)(a->ntargets + 1) * sizeof *a->targets);
            a->targets[a->ntargets++] = s;
            continue;
        }
        if (!strcmp(s, "--")) {
            only_pos = 1;
            continue;
        }
        if (s[1] == '-') {
            const char *name = s + 2, *eq = strchr(name, '=');
            size_t nlen = eq ? (size_t)(eq - name) : strlen(name);
            const LongOpt *hit = NULL, *o;
            int ambiguous = 0;
            const char *val = NULL;
            for (o = LONG_OPTS; o->name; o++) {
                if (strlen(o->name) == nlen && !strncmp(o->name, name, nlen)) {
                    hit = o;
                    ambiguous = 0;
                    break;
                }
                if (!strncmp(o->name, name, nlen)) {
                    if (hit)
                        ambiguous = 1;
                    hit = o;
                }
            }
            if (!hit || !nlen)
                usage_error("unrecognized arguments: %s", s);
            if (ambiguous)
                usage_error("ambiguous option: --%.*s", (int)nlen, name);
            if (hit->value) {
                if (eq)
                    val = eq + 1;
                else if (i + 1 < argc)
                    val = argv[++i];
                else
                    usage_error("argument --%s: expected one argument",
                                hit->name);
            } else if (eq) {
                usage_error("argument --%s: ignored explicit argument '%s'",
                            hit->name, eq + 1);
            }
            if (hit->id == O_SKIPALSO) {
                /* collected apart, appended after --skip / defaults */
                Args tmp;
                memset(&tmp, 0, sizeof tmp);
                add_skip(&tmp, val);
                also = xrealloc(also, (size_t)(nalso + tmp.nskip) * sizeof *also);
                memcpy(also + nalso, tmp.skip, (size_t)tmp.nskip * sizeof *also);
                nalso += tmp.nskip;
            } else {
                apply_opt(a, hit->id, hit->name, val);
            }
            continue;
        }
        for (s++; *s; s++) {
            int id;
            switch (*s) {
            case 'R': id = O_R; break;
            case 'r': id = O_REV; break;
            case 'h': id = O_HUMAN; break;
            case 'i': id = O_IC; break;
            case 's': id = O_SUMMARY; break;
            case 'q': id = O_QUOTE; break;
            default:
                usage_error("unrecognized arguments: -%c", *s);
                id = 0;
            }
            apply_opt(a, id, "short", NULL);
        }
    }
    if (!a->skip_given) { /* defaults first, then --skip-also */
        const char **d;
        for (d = DEFAULT_SKIP; *d; d++)
            add_skip(a, *d);
    }
    if (nalso) {
        a->skip = xrealloc(a->skip, (size_t)(a->nskip + nalso) * sizeof *a->skip);
        memcpy(a->skip + a->nskip, also, (size_t)nalso * sizeof *also);
        a->nskip += nalso;
    }
}

/* ------------------------------------------------------------ targets */

enum { K_DIR, K_FILE, K_GLOB };

typedef struct {
    int kind;
    const char *arg;
    char *root;    /* directory to list (dir, glob) */
    char *pattern; /* glob only */
    char *prefix;  /* prepended to entry names */
} Spec;

/* 0 ok; -1 with *msg set */
static int resolve_target(const char *arg, int multi, Spec *sp, char **msg)
{
    size_t n = strlen(arg);
    int forced = n && (arg[n - 1] == '/' || (IS_WIN && arg[n - 1] == '\\'));
    char *dir, *base;
    const char *root;

    memset(sp, 0, sizeof *sp);
    sp->arg = arg;
    if (forced || is_dir(arg)) {
        if (forced && !is_dir(arg)) {
            size_t k = n + sizeof(": not a directory");
            *msg = xmalloc(k);
            snprintf(*msg, k, "%s: not a directory", arg);
            return -1;
        }
        sp->kind = K_DIR;
        sp->root = xstrdup(arg);
        sp->prefix = xstrdup(multi ? arg : "");
        return 0;
    }
    if (lexists(arg)) {
        sp->kind = K_FILE;
        sp->prefix = xstrdup("");
        return 0;
    }
    path_split(arg, &dir, &base);
    if (has_magic(dir)) {
        size_t k = n + sizeof(": wildcards in directory part not supported");
        *msg = xmalloc(k);
        snprintf(*msg, k, "%s: wildcards in directory part not supported", arg);
        return -1;
    }
    root = *dir ? dir : ".";
    if (!is_dir(root)) {
        size_t k = strlen(root) + sizeof(": not found");
        *msg = xmalloc(k);
        snprintf(*msg, k, "%s: not found", root);
        return -1;
    }
    if (!has_magic(base)) { /* fd-style implicit substring match */
        char *b2 = xmalloc(strlen(base) + 3);
        sprintf(b2, "*%s*", base);
        base = b2;
    }
    sp->kind = K_GLOB;
    sp->root = xstrdup(root);
    sp->pattern = base;
    sp->prefix = dir;
    return 0;
}

typedef struct {
    EList *v;
    size_t n, cap;
} Groups;

static void groups_add(Groups *g, EList l)
{
    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 8;
        g->v = xrealloc(g->v, g->cap * sizeof *g->v);
    }
    g->v[g->n++] = l;
}

static int skipped(const Args *a, const char *name)
{
    int i;
    for (i = 0; i < a->nskip; i++) {
        const char *p = a->skip[i];
        if (has_magic(p) ? wild(p, name, IS_WIN) : streq_ic(p, name, IS_WIN))
            return 1;
    }
    return 0;
}

typedef struct {
    char *name, *key, *rel;
} Sub;

static int sub_cmp_desc(const void *x, const void *y)
{
    return strcmp(((const Sub *)y)->key, ((const Sub *)x)->key);
}

static int native_ic(const Args *a)
{
    return a->ignore_case < 0 ? IS_WIN : a->ignore_case;
}

/* One entry list per directory, depth first in directory name order. */
static void collect(const Spec *sp, const Args *a, Groups *groups)
{
    int ic = native_ic(a);
    Sub *stack = NULL;
    size_t nstack = 0, capstack = 0;

    if (sp->kind == K_FILE) {
        Entry e;
        char *msg;
        EList l = {0};
        if (stat_path(sp->arg, &e, &msg) != 0) {
            status_error(2, "%s: %s", sp->arg, msg);
            return;
        }
        free(e.name);
        e.name = xstrdup(sp->arg);
        e.key = fold_key(e.name);
        elist_add(&l, &e);
        groups_add(groups, l);
        return;
    }
    capstack = 8;
    stack = xmalloc(capstack * sizeof *stack);
    stack[nstack].name = xstrdup("");
    stack[nstack].key = xstrdup("");
    stack[nstack++].rel = xstrdup("");
    while (nstack) {
        Sub cur = stack[--nstack];
        char *path = *cur.rel ? path_join(sp->root, cur.rel) : xstrdup(sp->root);
        EList raw = {0}, mine = {0};
        char *msg;
        Sub *subs = NULL;
        size_t nsub = 0, i;

        if (read_dir(path, &raw, &msg) != 0) {
            status_error(2, "%s: %s", path, msg);
            free(msg);
            free(path);
            continue;
        }
        for (i = 0; i < raw.n; i++) {
            Entry e = raw.v[i];
            char *rel, *shown;
            int matched = !sp->pattern || wild(sp->pattern, e.name, ic);
            if (a->no_hidden && e.hidden)
                continue; /* neither listed nor descended */
            rel = *cur.rel ? path_join(cur.rel, e.name) : xstrdup(e.name);
            shown = *sp->prefix ? path_join(sp->prefix, rel) : xstrdup(rel);
            if (a->recursive && e.is_dir && !skipped(a, e.name)) {
                subs = xrealloc(subs, (nsub + 1) * sizeof *subs);
                subs[nsub].name = xstrdup(e.name);
                subs[nsub].key = fold_key(e.name);
                subs[nsub++].rel = xstrdup(rel);
            }
            free(rel);
            if (matched) {
                free(e.name);
                e.name = shown;
                e.key = fold_key(shown);
                elist_add(&mine, &e);
            } else {
                free(shown);
                free(e.name);
                free(e.target);
            }
        }
        free(raw.v);
        groups_add(groups, mine);
        /* reversed so that popping gives name order */
        if (nsub > 1)
            qsort(subs, nsub, sizeof *subs, sub_cmp_desc);
        for (i = 0; i < nsub; i++) {
            if (nstack == capstack) {
                capstack *= 2;
                stack = xrealloc(stack, capstack * sizeof *stack);
            }
            stack[nstack++] = subs[i];
        }
        free(subs);
        free(path);
    }
    free(stack);
}

/* ------------------------------------------------------------ sorting */

static int g_sort, g_reverse, g_dirs_first;

static int cmp_ll(long long a, long long b) { return a < b ? -1 : a > b; }

static int entry_cmp(const void *x, const void *y)
{
    const Entry *a = x, *b = y;
    int c = 0;
    if (g_dirs_first && a->is_dir != b->is_dir)
        return a->is_dir ? -1 : 1; /* not reversed */
    if (g_sort == 0) {
        c = cmp_ll(a->sec, b->sec);
        if (!c)
            c = cmp_ll(a->nsec, b->nsec);
    } else if (g_sort == 2) {
        u64 sa = a->is_dir ? 0 : a->size, sb = b->is_dir ? 0 : b->size;
        c = sa < sb ? -1 : sa > sb;
    }
    if (!c)
        c = strcmp(a->key, b->key);
    return g_reverse ? -c : c;
}

static void sort_entries(EList *l, int key, int reverse, int dirs_first)
{
    g_sort = key;
    g_reverse = reverse;
    g_dirs_first = dirs_first;
    if (l->n > 1)
        qsort(l->v, l->n, sizeof *l->v, entry_cmp);
}

/* ------------------------------------------------------------ color */

/* da/sn follow eza's LS_COLORS keys (date, size number); dm (dir marker)
 * is ours. Values are SGR parameters, "" means no color. */
typedef struct {
    char *pat, *sgr;
} GlobColor;

typedef struct Palette {
    const char *di, *ln, *ex, *fi, *da, *sn, *dm;
    GlobColor *globs;
    int nglobs;
    char **exts; /* Windows executable extensions, lowercase */
    int nexts;
} Palette;

static int sgr_valid(const char *v)
{
    return strspn(v, "0123456789;") == strlen(v);
}

static char *dup_n(const char *s, size_t n)
{
    char *p = memcpy(xmalloc(n + 1), s, n);
    p[n] = '\0';
    return p;
}

/* LS_COLORS (dircolors format) over defaults. The last matching glob wins.
 * On Windows "executable" is an extension in pathext (PATHEXT). Strings are
 * never freed: the process is short lived. */
static Palette *palette_new(const char *ls_colors, const char *pathext)
{
    Palette *p = xmalloc(sizeof *p);
    const char *s;

    memset(p, 0, sizeof *p);
    p->di = "01;34";
    p->ln = "01;36";
    p->ex = "01;32";
    p->fi = "";
    p->da = "2";
    p->sn = "33";
    p->dm = "34";
    for (s = ls_colors ? ls_colors : ""; *s;) {
        const char *e = strchr(s, ':'), *eq;
        size_t n = e ? (size_t)(e - s) : strlen(s);
        char *item = dup_n(s, n), *key, *val;
        s += n + (e ? 1 : 0);
        eq = strchr(item, '=');
        if (!eq || !sgr_valid(eq + 1))
            continue;
        key = dup_n(item, (size_t)(eq - item));
        val = xstrdup(eq + 1);
        if (key[0] == '*') {
            p->globs = xrealloc(p->globs,
                                (size_t)(p->nglobs + 1) * sizeof *p->globs);
            p->globs[p->nglobs].pat = key;
            p->globs[p->nglobs++].sgr = val;
        } else if (!strcmp(key, "di")) {
            p->di = val;
        } else if (!strcmp(key, "ln")) {
            p->ln = val;
        } else if (!strcmp(key, "ex")) {
            p->ex = val;
        } else if (!strcmp(key, "fi")) {
            p->fi = val;
        } else if (!strcmp(key, "da")) {
            p->da = val;
        } else if (!strcmp(key, "sn")) {
            p->sn = val;
        } else if (!strcmp(key, "dm")) {
            p->dm = val;
        } /* other keys are ignored */
    }
    /* PATHEXT: ";"-separated; unset or empty gives the default list */
    for (s = pathext ? pathext : ""; *s;) {
        const char *e = strchr(s, ';');
        size_t n = e ? (size_t)(e - s) : strlen(s), i;
        const char *w = s;
        s += n + (e ? 1 : 0);
        while (n && (*w == ' ' || *w == '\t')) {
            w++;
            n--;
        }
        while (n && (w[n - 1] == ' ' || w[n - 1] == '\t'))
            n--;
        if (n) {
            char *x = dup_n(w, n);
            for (i = 0; i < n; i++)
                if (x[i] >= 'A' && x[i] <= 'Z')
                    x[i] = (char)(x[i] + 32);
            p->exts = xrealloc(p->exts,
                               (size_t)(p->nexts + 1) * sizeof *p->exts);
            p->exts[p->nexts++] = x;
        }
    }
    if (!p->nexts) {
        static const char *dflt[] = {".exe", ".bat", ".cmd", ".com"};
        int i;
        p->exts = xmalloc(4 * sizeof *p->exts);
        for (i = 0; i < 4; i++)
            p->exts[p->nexts++] = xstrdup(dflt[i]);
    }
    return p;
}

static int ends_with_ic(const char *s, const char *suffix)
{
    size_t n = strlen(s), k = strlen(suffix);
    return n >= k && streq_ic(s + n - k, suffix, 1);
}

/* SGR parameter string for an Entry ("" for none). */
static const char *palette_style(const Palette *p, const Entry *e)
{
    const char *base = e->name, *s;
    int exec, i;
    if (e->is_link)
        return p->ln;
    if (e->is_dir)
        return p->di;
    for (s = e->name; *s; s++)
        if (is_sep(*s))
            base = s + 1;
    if (IS_WIN) {
        exec = 0;
        for (i = 0; i < p->nexts; i++)
            if (ends_with_ic(base, p->exts[i]))
                exec = 1;
    } else {
        exec = e->is_exec;
    }
    if (exec)
        return p->ex;
    for (i = p->nglobs - 1; i >= 0; i--)
        if (wild(p->globs[i].pat, base, IS_WIN))
            return p->globs[i].sgr;
    return p->fi;
}

/* Append text, wrapped in the SGR sequence when there is one. */
static void paint_into(Buf *b, const char *sgr, const char *text)
{
    if (!*sgr) {
        buf_puts(b, text);
        return;
    }
    buf_puts(b, "\033[");
    buf_puts(b, sgr);
    buf_putc(b, 'm');
    buf_puts(b, text);
    buf_puts(b, "\033[0m");
}

/* mode: 0 auto, 1 always, 2 never. vt: Windows console accepted ANSI. */
static int color_enabled(int mode, int tty, int vt)
{
    const char *nc;
    if (mode == 1)
        return 1;
    nc = getenv("NO_COLOR");
    if (mode == 2 || (nc && *nc) || !tty)
        return 0;
    return vt || !IS_WIN || getenv("TERM") || getenv("TERM_PROGRAM")
           || getenv("WT_SESSION");
}

#if IS_WIN
static DWORD g_vt_old;
static int g_vt_set;

/* Turn on ANSI processing for a real console; 0 if not a console. */
static int enable_vt(void)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD m;
    if (g_vt_set)
        return 1;
    if (!GetConsoleMode(h, &m))
        return 0;
    g_vt_old = m;
    g_vt_set = SetConsoleMode(h, m | 0x0004) != 0; /* VIRTUAL_TERMINAL_PROCESSING */
    return g_vt_set;
}

static void restore_vt(void)
{
    if (g_vt_set)
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_vt_old);
}
#else
static int enable_vt(void) { return 0; }
static void restore_vt(void) {}
#endif

/* ------------------------------------------------------------ rendering */

static void size_text(char *out, const Entry *e, const Args *a)
{
    if (e->is_dir)
        strcpy(out, DIR_MARK);
    else
        format_size(out, e->size, a->human, a->commas);
}

static void format_entry(Buf *line, const Entry *e, int width, const Args *a)
{
    Buf name = {0}, q = {0};
    char when[32], size[40];
    const char *shown, *none = "";
    const Palette *pal = a->pal;
    int pad;

    buf_puts(&name, e->name);
    if (e->is_dir)
        buf_putc(&name, SEP);
    if (a->posix && IS_WIN) {
        size_t i;
        for (i = 0; i < name.n; i++)
            if (name.p[i] == '\\')
                name.p[i] = '/';
    }
    shown = buf_str(&name);
    if (a->quote) {
        quote_name(&q, shown, a->quote);
        shown = buf_str(&q);
    }
    if (a->names) {
        paint_into(line, pal ? palette_style(pal, e) : none, shown);
    } else {
        format_time(when, e->sec);
        size_text(size, e, a);
        pad = width - (int)strlen(size);
        paint_into(line, pal ? pal->da : none, when);
        buf_puts(line, "  ");
        while (pad-- > 0)
            buf_putc(line, ' '); /* padding stays uncolored */
        paint_into(line, pal ? (e->is_dir ? pal->dm : pal->sn) : none, size);
        buf_putc(line, ' ');
        paint_into(line, pal ? palette_style(pal, e) : none, shown);
        if (e->target) {
            buf_puts(line, " -> ");
            buf_puts(line, e->target);
        }
    }
    buf_free(&name);
    buf_free(&q);
}

static void summary_line(Buf *out, u64 files, u64 dirs, u64 nbytes, int has_free,
                         u64 freeb, int human)
{
    char t[40];
    fmt_u64(t, files, 1);
    buf_puts(out, t);
    buf_puts(out, " files | ");
    fmt_u64(t, dirs, 1);
    buf_puts(out, t);
    buf_puts(out, " dirs | ");
    format_size(t, nbytes, human, 1);
    buf_puts(out, t);
    if (!human)
        buf_puts(out, " bytes");
    if (has_free) {
        buf_puts(out, " | ");
        format_size(t, freeb, human, 1);
        buf_puts(out, t);
        buf_puts(out, human ? " free" : " bytes free");
    }
}

/* ------------------------------------------------------------ terminal */

static int stdout_is_tty(void)
{
#if IS_WIN
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    union {
        FILE_NAME_INFO info;
        char raw[sizeof(FILE_NAME_INFO) + 1024 * sizeof(WCHAR)];
    } u;
    if (GetConsoleMode(h, &mode))
        return 1;
    /* mintty / Cygwin pty: a pipe named \msys-...-pty<N>-to-master[-nat] */
    if (GetFileType(h) != FILE_TYPE_PIPE)
        return 0;
    if (!GetFileInformationByHandleEx(h, FileNameInfo, &u.info, sizeof u))
        return 0;
    if (u.info.FileNameLength >= 1024 * sizeof(WCHAR))
        return 0;
    u.info.FileName[u.info.FileNameLength / sizeof(WCHAR)] = 0;
    return (wcsncmp(u.info.FileName, L"\\msys-", 6) == 0
            || wcsncmp(u.info.FileName, L"\\cygwin-", 8) == 0)
           && wcsstr(u.info.FileName, L"-pty") != NULL
           && wcsstr(u.info.FileName, L"-master") != NULL;
#else
    return isatty(1);
#endif
}

/* ------------------------------------------------------------ L_OPTS */

/* shlex.split (POSIX mode): whitespace separated, '..' literal, ".." with
 * \" and \\ escapes, backslash escapes outside quotes. */
static char **shlex_split(const char *s, int *count)
{
    char **v = NULL;
    int n = 0, in_tok = 0;
    char quote = 0;
    Buf cur = {0};

    for (; *s; s++) {
        char c = *s;
        if (quote == '\'') {
            if (c == '\'')
                quote = 0;
            else
                buf_putc(&cur, c);
        } else if (quote == '"') {
            if (c == '"')
                quote = 0;
            else if (c == '\\' && (s[1] == '"' || s[1] == '\\'))
                buf_putc(&cur, *++s);
            else
                buf_putc(&cur, c);
        } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (in_tok) {
                v = xrealloc(v, (size_t)(n + 1) * sizeof *v);
                v[n++] = xstrdup(buf_str(&cur));
                buf_free(&cur);
                in_tok = 0;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            in_tok = 1;
        } else if (c == '\\') {
            if (!s[1])
                die("L_OPTS: no escaped character");
            buf_putc(&cur, *++s);
            in_tok = 1;
        } else {
            buf_putc(&cur, c);
            in_tok = 1;
        }
    }
    if (quote)
        die("L_OPTS: no closing quotation");
    if (in_tok) {
        v = xrealloc(v, (size_t)(n + 1) * sizeof *v);
        v[n++] = xstrdup(buf_str(&cur));
    }
    buf_free(&cur);
    *count = n;
    return v;
}

/* argv with the L_OPTS words inserted after argv[0] (command line wins). */
static char **with_l_opts(int *argc, char **argv)
{
    const char *env = getenv("L_OPTS");
    char **opts, **nv;
    int n, i;
    if (!env)
        return argv;
    opts = shlex_split(env, &n);
    if (!n)
        return argv;
    nv = xmalloc((size_t)(*argc + n + 1) * sizeof *nv);
    nv[0] = argv[0];
    for (i = 0; i < n; i++)
        nv[1 + i] = opts[i];
    for (i = 1; i < *argc; i++)
        nv[n + i] = argv[i];
    *argc += n;
    nv[*argc] = NULL;
    return nv;
}

/* ------------------------------------------------------------ main */

static char **utf8_argv(int *argc)
{
#if IS_WIN
    int n, i;
    wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
    char **v;
    if (!w)
        die("cannot read the command line");
    v = xmalloc(((size_t)n + 1) * sizeof *v);
    for (i = 0; i < n; i++)
        v[i] = w2u(w[i]);
    v[n] = NULL;
    *argc = n;
    return v;
#else
    (void)argc;
    return NULL;
#endif
}

int main(int argc, char **argv)
{
    Args a;
    Groups groups = {0};
    EList all = {0};
    const char *dot = ".";
    const char *def_targets[1];
    char *free_path = NULL;
    int show_summary, multi, tty, i;
    size_t gi;
    u64 files = 0, dirs = 0, nbytes = 0, freeb = 0;
    int width = 0, has_free = 0;
    char **uv;

    memset(&a, 0, sizeof a);
#if IS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
    uv = utf8_argv(&argc);
    argv = uv;
#else
    uv = utf8_argv(&argc);
    (void)uv;
#endif
    argv = with_l_opts(&argc, argv);
    parse_args(&a, argc, argv);
    if (a.by_dir)
        a.recursive = 1;
    if (!a.ntargets) {
        def_targets[0] = dot;
        a.targets = def_targets;
        a.ntargets = 1;
    }
    multi = a.ntargets > 1;
    tty = stdout_is_tty();
    if (color_enabled(a.color, tty,
                      IS_WIN && tty && a.color != 2 && enable_vt())) {
        a.pal = palette_new(getenv("LS_COLORS"), getenv("PATHEXT"));
        if (a.color == 1 && tty)
            enable_vt(); /* --color=always on a real console */
    }
    show_summary = a.summary || (tty && !a.names);

    for (i = 0; i < a.ntargets; i++) {
        Spec sp;
        char *msg = NULL;
        size_t before = groups.n, k;
        int any = 0;
        if (resolve_target(a.targets[i], multi, &sp, &msg) != 0) {
            status_error(2, "%s", msg);
            continue;
        }
        if (!free_path) {
            if (sp.root) {
                free_path = xstrdup(sp.root);
            } else {
                char *d, *b;
                path_split(sp.arg, &d, &b);
                free_path = *d ? d : xstrdup(".");
            }
        }
        collect(&sp, &a, &groups);
        for (k = before; k < groups.n; k++)
            if (groups.v[k].n)
                any = 1;
        if (sp.pattern && !any)
            status_error(1, "%s: no match", sp.arg);
    }

    if (show_summary && free_path)
        has_free = free_bytes(free_path, &freeb);

    if (a.by_dir) {
        for (gi = 0; gi < groups.n; gi++) {
            size_t k;
            sort_entries(&groups.v[gi], a.sort, a.reverse, a.group_dirs);
            for (k = 0; k < groups.v[gi].n; k++)
                elist_add(&all, &groups.v[gi].v[k]);
        }
    } else {
        for (gi = 0; gi < groups.n; gi++) {
            size_t k;
            for (k = 0; k < groups.v[gi].n; k++)
                elist_add(&all, &groups.v[gi].v[k]);
        }
        /* dirs-first is per directory: not for flat -R */
        sort_entries(&all, a.sort, a.reverse, a.group_dirs && !a.recursive);
    }

    for (gi = 0; gi < all.n; gi++) {
        char t[40];
        size_text(t, &all.v[gi], &a);
        if ((int)strlen(t) > width)
            width = (int)strlen(t);
    }
    for (gi = 0; gi < all.n; gi++) {
        Buf line = {0};
        format_entry(&line, &all.v[gi], width, &a);
        out_line(buf_str(&line));
        buf_free(&line);
        if (all.v[gi].is_dir)
            dirs++;
        else {
            files++;
            nbytes += all.v[gi].size;
        }
    }
    if (show_summary) {
        Buf s = {0};
        out_line(""); /* blank line separates the footer from the entries */
        summary_line(&s, files, dirs, nbytes, has_free, freeb, a.human);
        out_line(buf_str(&s));
        buf_free(&s);
    }
    out_flush();

    if (g_errors) {
        char m[80];
        snprintf(m, sizeof m, PROG ": %d error%s (see above)", g_errors,
                 g_errors == 1 ? "" : "s");
        err_line(m);
    }
    restore_vt();
    return g_code;
}

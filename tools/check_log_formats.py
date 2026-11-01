#!/usr/bin/env python3
"""Every LOG(...) call passes exactly as many arguments as its format string converts.

LOG is `Logf(const char* fmt, ...)`: nothing checks a call's arguments against its format, and MSVC only checks
the CRT's own printf family. One argument short is not a wrong log line, it is a crash: the next conversion reads
whatever the variadic slot holds. The case this guard exists for:

    LOG("First-person camera %s: director camera record bound: ... engine TLS +0x%X ...", record.blockOffset, ...)

`%s` takes the block offset (0x188 in Reach, 0x150 in Halo 4) as a string, strnlen reads address 0x188, and MCC
dies at every Reach and Halo 4 level load -- in player builds too, because the line is not a dev line. The build,
the unit tests and the offline verifiers all pass such a call: no test executes game.cpp's install path.

This guard counts. It does not check argument TYPES (a `%s` given an int still passes); it does catch a missing
or an extra argument in every call whose format is a plain string literal.

usage: check_log_formats.py [--self-test] [source root, default <repo>/src]
Exit 0 = every call agrees. Python stdlib only.
"""
import os
import re
import sys

BS = chr(92)
CONVERSION = re.compile(
    r'%[-+ #0]*(\*|\d+)?(?:\.(\*|\d+))?(?:hh|h|ll|l|z|I64|I32|j|t|L|w)?[diouxXeEfgGaAcCsSpn]')
CALL = re.compile(r'\bLOG\s*\(')


def split_call(src, i):
    """From just after LOG's '(' to its matching ')': the top-level arguments, as text."""
    depth = 1
    quote = None
    escaped = False
    args = []
    cur = []
    j = i
    while j < len(src):
        ch = src[j]
        if quote:
            cur.append(ch)
            if escaped:
                escaped = False
            elif ch == BS:
                escaped = True
            elif ch == quote:
                quote = None
        elif ch in ('"', "'"):
            quote = ch
            cur.append(ch)
        elif ch in '([{':
            depth += 1
            cur.append(ch)
        elif ch in ')]}':
            depth -= 1
            if depth == 0:
                args.append(''.join(cur))
                return args
            cur.append(ch)
        elif ch == ',' and depth == 1:
            args.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
        j += 1
    return None


def literal(text):
    """Adjacent string literals joined, or None when the argument is anything else (a variable, a macro)."""
    out = []
    k = 0
    seen = False
    while k < len(text):
        ch = text[k]
        if ch.isspace():
            k += 1
            continue
        if ch != '"':
            return None
        seen = True
        k += 1
        escaped = False
        while k < len(text):
            c = text[k]
            if escaped:
                out.append(BS + c)
                escaped = False
            elif c == BS:
                escaped = True
            elif c == '"':
                break
            else:
                out.append(c)
            k += 1
        k += 1
    return ''.join(out) if seen else None


def conversions(fmt):
    count = 0
    for width, precision in CONVERSION.findall(fmt.replace('%%', '')):
        count += 1 + (width == '*') + (precision == '*')
    return count


def strip_comments(src):
    """Blank out // and /* */ comments (keeping line numbers), leaving string literals alone."""
    out = []
    i = 0
    quote = None
    escaped = False
    while i < len(src):
        ch = src[i]
        if quote:
            out.append(ch)
            if escaped:
                escaped = False
            elif ch == BS:
                escaped = True
            elif ch == quote:
                quote = None
            i += 1
        elif ch in ('"', "'"):
            quote = ch
            out.append(ch)
            i += 1
        elif src.startswith('//', i):
            end = src.find('\n', i)
            end = len(src) if end < 0 else end
            out.append(' ' * (end - i))
            i = end
        elif src.startswith('/*', i):
            end = src.find('*/', i + 2)
            end = len(src) if end < 0 else end + 2
            out.append(''.join(c if c == '\n' else ' ' for c in src[i:end]))
            i = end
        else:
            out.append(ch)
            i += 1
    return ''.join(out)


def check_text(src, name):
    """-> (calls checked, calls skipped, [problem strings])."""
    src = strip_comments(src)
    checked = skipped = 0
    problems = []
    for m in CALL.finditer(src):
        args = split_call(src, m.end())
        line = src.count('\n', 0, m.start()) + 1
        if args is None:
            problems.append('%s:%d: LOG( is never closed' % (name, line))
            continue
        fmt = literal(args[0])
        if fmt is None:
            skipped += 1
            continue
        checked += 1
        want = conversions(fmt)
        have = len(args) - 1
        if want != have:
            problems.append('%s:%d: the format converts %d value(s) and the call passes %d: "%s"'
                            % (name, line, want, have, fmt[:100]))
    return checked, skipped, problems


def expect(condition, what):
    # Not `assert`: the test runs under -O, which strips asserts.
    if not condition:
        raise SystemExit('check_log_formats self-test FAILED: ' + what)


def self_test():
    good = """
        LOG("plain");
        LOG("one %s and %d%%", name, n);
        LOG("joined " "literals +0x%llX %.*s", static_cast<unsigned long long>(f(a, b)), int(n), text);
        LOG(fmt, a, b);                      // not a literal: skipped
        // LOG("commented out %s");
        LOG("a comma, in the text and a ) too: %u", count);
    """
    result = check_text(good, 'good')
    expect(result == (4, 1, []), 'the good calls read as %r' % (result,))
    # The line this guard exists for: seven conversions, six arguments (shortened here to three and two).
    short = 'LOG("First-person camera %s: TLS +0x%X (index at +0x%llX)", record.blockOffset, tlsRva);'
    expect(len(check_text(short, 'short')[2]) == 1, 'a call one argument short passed')
    expect(len(check_text('LOG("%d", a, b);', 'extra')[2]) == 1, 'a call with an extra argument passed')
    expect(len(check_text('LOG("%*d %.*f", width, value);', 'star')[2]) == 1, 'a * width was not counted')
    expect(conversions('100%% of %s') == 1, '%% was counted as a conversion')
    expect(len(check_text('LOG("never closed %s", a', 'open')[2]) == 1, 'an unclosed LOG( passed')


def main(argv):
    do_self_test = '--self-test' in argv
    roots = [a for a in argv if not a.startswith('--')]
    if do_self_test:
        self_test()
    root = roots[0] if roots else os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src')
    checked = skipped = 0
    problems = []
    for folder, _, files in os.walk(root):
        for name in sorted(files):
            if not name.endswith(('.cpp', '.h', '.inl', '.hpp')):
                continue
            path = os.path.join(folder, name)
            with open(path, encoding='utf-8', errors='replace') as f:
                c, s, p = check_text(f.read(), os.path.relpath(path, root))
            checked += c
            skipped += s
            problems += p
    for p in problems:
        print(p)
    if checked < 100:
        print('only %d LOG calls found under %s: the scan is not looking at the source' % (checked, root))
        return 1
    print('%d LOG calls checked (%d with a non-literal format skipped), %d disagree'
          % (checked, skipped, len(problems)))
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

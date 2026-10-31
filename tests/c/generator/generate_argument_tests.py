#!/usr/bin/env python3
"""Generates tests/c/unit/monero_c_arguments_table.h from the public headers and the sources.

The table lists every exported function with the kind of each parameter, so that
tests/c/unit/monero_c_arguments_unit_tests.c can call each one with a NULL in every position and check that
nothing crashes, that the required pointers are rejected with the message of the ABI, and that the outputs
of a failed call are reset. A new function that the table doesn't know fails the check, so it can't miss the test.

  generate_argument_tests.py            writes the table
  generate_argument_tests.py --check    fails if the table or abi/monero_c.symbols differ from the headers
"""
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
HEADERS = ['src/utils/monero_c_utils.h', 'src/common/monero_c_rpc_connection.h', 'src/wallet/monero_c_wallet.h', 'src/daemon/monero_c_daemon.h']
SOURCES = ['src/utils/monero_c_utils.cpp', 'src/common/monero_c_rpc_connection.cpp', 'src/wallet/monero_c_wallet.cpp', 'src/daemon/monero_c_daemon.cpp']
TABLE = 'tests/c/unit/monero_c_arguments_table.h'
SYMBOLS = 'tests/c/abi/monero_c.symbols'

# the kind of each parameter type, as the runner knows it. A type that isn't here fails the generator
KINDS = {
    'const char*': 'K_STR',
    'const char* const*': 'K_STRS',
    'monero_wallet*': 'K_WALLET',
    'monero_daemon*': 'K_DAEMON',
    'monero_rpc_connection*': 'K_CONNECTION',
    'monero_wallet_listener*': 'K_WALLET_LISTENER',
    'monero_daemon_listener*': 'K_DAEMON_LISTENER',
    'const monero_wallet_listener_callbacks*': 'K_WALLET_CALLBACKS',
    'const monero_daemon_listener_callbacks*': 'K_DAEMON_CALLBACKS',
    'uint64_t': 'K_U64',
    'uint32_t': 'K_U32',
    'int32_t': 'K_I32',
    'uint16_t': 'K_U16',
    'uint8_t': 'K_U8',
    'bool': 'K_BOOL',
    'size_t': 'K_SIZE',
    'double': 'K_DOUBLE',
    'const uint64_t*': 'K_PU64',
    'const uint32_t*': 'K_PU32',
    'const int32_t*': 'K_PI32',
    'const bool*': 'K_PBOOL',
    'const uint8_t*': 'K_PBYTES',
    'void*': 'K_PTR',
    'char*': 'K_PTR',
    'char**': 'K_OUT_STR',
    'uint8_t**': 'K_OUT_BYTES',
    'uint64_t**': 'K_OUT_U64S',
    'monero_key_image_spent_status**': 'K_OUT_STATUSES',
    'monero_daemon_listener***': 'K_OUT_LISTENERS',
    'monero_wallet**': 'K_OUT_WALLET',
    'monero_daemon**': 'K_OUT_DAEMON',
    'monero_rpc_connection**': 'K_OUT_CONNECTION',
    'monero_wallet_listener**': 'K_OUT_WALLET_LISTENER',
    'monero_daemon_listener**': 'K_OUT_DAEMON_LISTENER',
    'uint64_t*': 'K_OUT_U64',
    'uint32_t*': 'K_OUT_U32',
    'int32_t*': 'K_OUT_I32',
    'bool*': 'K_OUT_BOOL',
    'size_t*': 'K_OUT_SIZE',
    'monero_optional_bool*': 'K_OUT_ENUM',
    'monero_utils_network_type*': 'K_OUT_ENUM',
    'monero_tx_priority*': 'K_OUT_ENUM',
    'monero_key_image_spent_status*': 'K_OUT_ENUM',
    'uint8_t[32]': 'K_OUT_BUF32',
    'uint8_t[8]': 'K_OUT_BUF8',
}

DECLARATION = re.compile(r'^MONERO_EXPORT\s+(?P<ret>.+?)\s*\b(?P<name>monero_\w+)\((?P<params>.*)\);$')


def normalize(type_text):
    text = re.sub(r'\s+', ' ', type_text.strip())
    text = text.replace(' *', '*').replace('* *', '**')
    return text


def parse_params(text):
    text = text.strip()
    if text in ('', 'void'):
        return []
    params = []
    for part in text.split(','):
        m = re.match(r'^(?P<type>.+?)\s*\b(?P<name>\w+)(?P<array>\[\d+\])?$', part.strip())
        if not m:
            raise SystemExit('cannot parse the parameter "%s"' % part)
        params.append((normalize(m.group('type')) + (m.group('array') or ''), m.group('name')))
    return params


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


def declarations():
    found = {}
    for header in HEADERS:
        for line in read(header).splitlines():
            m = DECLARATION.match(line.strip())
            if m:
                name = m.group('name')
                if name in found:
                    raise SystemExit('%s is declared twice' % name)
                found[name] = (normalize(m.group('ret')), parse_params(m.group('params')))
    return found


def blank_literals(text):
    """the text with the contents of the strings, the characters and the comments blanked out, so that braces in them aren't counted"""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '/' and i + 1 < n and text[i + 1] == '/':
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif c == '/' and i + 1 < n and text[i + 1] == '*':
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append(c + ' ' * (j - i - 2) + c if j - i >= 2 else c)
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def bodies():
    """the body of each exported function, found in the sources"""
    found = {}
    for source in SOURCES:
        text = read(source)
        blank = blank_literals(text)
        for m in re.finditer(r'^[A-Za-z_][\w:\* ]*?\b(monero_\w+)\([^;{]*\)\s*\{', blank, re.MULTILINE):
            depth = 1
            i = m.end()
            while depth > 0 and i < len(blank):
                if blank[i] == '{':
                    depth += 1
                elif blank[i] == '}':
                    depth -= 1
                i += 1
            found[m.group(1)] = text[m.end():i]
    return found


def requirements(body, params):
    """the parameters that the function rejects when they are NULL: the ones that it requires, and the arrays that it copies with a count"""
    names = {name for _, name in params}
    required = set()
    for m in re.finditer(r'\brequire\(\s*(\w+)\s*,\s*"(\w+)"\s*\)', body):
        if m.group(1) != m.group(2) or m.group(2) not in names:
            raise SystemExit('require(%s, "%s") doesn\'t name a parameter' % (m.group(1), m.group(2)))
        required.add(m.group(2))
    for m in re.finditer(r'\b(?:array_of|string_array)\(\s*(\w+)\s*,\s*\w+\s*,\s*"(\w+)"\s*\)', body):
        if m.group(1) != m.group(2) or m.group(2) not in names:
            raise SystemExit('the array %s is named "%s" in its error' % (m.group(1), m.group(2)))
        required.add(m.group(2))
    return required


def generate():
    decls = declarations()
    code = bodies()
    lines = []
    lines.append('// generated by tests/c/generator/generate_argument_tests.py from the headers and the sources, do not edit.')
    lines.append('// one row for each exported function: the kind and the name of each parameter, and the ones that a NULL makes it fail.')
    lines.append('// monero_c_arguments_unit_tests.c includes it after the definitions of the kinds')
    lines.append('')
    wrappers = []
    rows = []
    for name in sorted(decls):
        ret, params = decls[name]
        if name not in code:
            raise SystemExit('the source of %s was not found' % name)
        required = requirements(code[name], params)
        kinds = []
        for type_text, pname in params:
            type_key = type_text
            if type_key.endswith(']'):
                type_key = re.sub(r'\[(\d+)\]$', lambda m: '[' + m.group(1) + ']', type_key)
                type_key = 'uint8_t[' + type_key.split('[')[1]
            if type_key not in KINDS:
                raise SystemExit('%s: the type "%s" of %s has no kind, add it to KINDS and to the runner' % (name, type_text, pname))
            kind = KINDS[type_key]
            if kind in ('K_OUT_U64', 'K_OUT_U32', 'K_OUT_I32', 'K_OUT_BOOL', 'K_OUT_SIZE') and not pname.startswith('out_'):
                raise SystemExit('%s: %s is a pointer to a scalar that isn\'t named out_' % (name, pname))
            kinds.append(kind)
        args = []
        for index, ((type_text, pname), kind) in enumerate(zip(params, kinds)):
            c_type = type_text
            if type_text.endswith(']'):
                args.append('(uint8_t*) a[%d]' % index)
            elif kind in ('K_U64', 'K_U32', 'K_I32', 'K_U16', 'K_U8', 'K_BOOL', 'K_SIZE', 'K_DOUBLE'):
                args.append('*(%s*) a[%d]' % (c_type, index))
            else:
                args.append('(%s) a[%d]' % (c_type, index))
        call = '%s(%s)' % (name, ', '.join(args))
        if ret == 'monero_result':
            body = 'return %s;' % call
        else:
            body = '(void) %s; return MONERO_OK;' % call
        wrappers.append('static monero_result call_%s(void** a) { %s }' % (name, body if params else body.replace('a[', 'a[') ))
        mask = 0
        for index, (_, pname) in enumerate(params):
            if pname in required:
                mask |= 1 << index
        kind_list = ', '.join(kinds) if kinds else 'K_END'
        name_list = ', '.join('"%s"' % pname for _, pname in params) if params else '""'
        rows.append('  {"%s", call_%s, %d, {%s}, {%s}, 0x%x, %s},' % (name, name, len(params), kind_list, name_list, mask, 'RET_RESULT' if ret == 'monero_result' else 'RET_OTHER'))
    lines.extend(wrappers)
    lines.append('')
    lines.append('static const arg_function g_functions[] = {')
    lines.extend(rows)
    lines.append('};')
    lines.append('')
    return '\n'.join(lines), sorted(decls)


def main():
    check = '--check' in sys.argv[1:]
    text, names = generate()
    table_path = os.path.join(ROOT, TABLE)
    problems = []
    if check:
        current = open(table_path, encoding='utf-8').read() if os.path.exists(table_path) else ''
        if current != text:
            problems.append('%s is not up to date, run tests/c/generator/generate_argument_tests.py' % TABLE)
        listed = read(SYMBOLS).split()
        if sorted(listed) != names:
            missing = sorted(set(names) - set(listed))
            extra = sorted(set(listed) - set(names))
            problems.append('%s differs from the headers: missing %s, not declared %s' % (SYMBOLS, missing, extra))
        if problems:
            for p in problems:
                print(p, file=sys.stderr)
            return 1
        print('the argument table matches the headers (%d functions)' % len(names))
        return 0
    with open(table_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote %s (%d functions)' % (TABLE, len(names)))
    return 0


if __name__ == '__main__':
    sys.exit(main())

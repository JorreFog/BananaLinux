"""Reading and writing theme files: JSON with // and /* */ comments and trailing commas (ui/src/json.cpp's dialect).

Key order is kept (Python dicts keep it). Comments are not kept on save: the studio says so when a file had any.
"""
import json


def strip(text):
    """The text without comments and trailing commas, and whether it had comments."""
    out = []
    i, n = 0, len(text)
    had = False
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith('//', i):
            had = True
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            had = True
            j = text.find('*/', i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    s = ''.join(out)
    # trailing commas: a comma followed (after spaces) by } or ], outside strings
    res = []
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if c == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            res.append(s[i:j + 1])
            i = j + 1
            continue
        if c == ',':
            j = i + 1
            while j < n and s[j] in ' \t\r\n':
                j += 1
            if j < n and s[j] in '}]':
                i += 1
                continue
        res.append(c)
        i += 1
    return ''.join(res), had


def loads(text):
    """(document, had_comments)"""
    s, had = strip(text)
    return json.loads(s), had


def load(path):
    with open(path, encoding='utf-8') as f:
        return loads(f.read())


def _short(v):
    """True when a value is small enough to stay on one line."""
    if isinstance(v, (dict, list)):
        s = json.dumps(v, ensure_ascii=False)
        return len(s) <= 72 and not any(isinstance(x, dict) and any(isinstance(y, (dict, list)) for y in x.values())
                                        for x in (v.values() if isinstance(v, dict) else v))
    return True


def dumps(v, indent=0):
    """Pretty JSON in the hand-written themes' style: small objects and arrays on one line, the rest indented."""
    pad = '  ' * indent
    if isinstance(v, dict):
        if not v:
            return '{}'
        if indent > 0 and _short(v):
            return '{ ' + ', '.join(json.dumps(k, ensure_ascii=False) + ': ' + dumps(x, indent + 1) for k, x in v.items()) + ' }'
        inner = ',\n'.join(pad + '  ' + json.dumps(k, ensure_ascii=False) + ': ' + dumps(x, indent + 1) for k, x in v.items())
        return '{\n' + inner + '\n' + pad + '}'
    if isinstance(v, list):
        if not v:
            return '[]'
        if _short(v) and not any(isinstance(x, dict) for x in v):
            return '[' + ', '.join(dumps(x, indent + 1) for x in v) + ']'
        inner = ',\n'.join(pad + '  ' + dumps(x, indent + 1) for x in v)
        return '[\n' + inner + '\n' + pad + ']'
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    return json.dumps(v, ensure_ascii=False)


def save(path, doc):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(dumps(doc) + '\n')

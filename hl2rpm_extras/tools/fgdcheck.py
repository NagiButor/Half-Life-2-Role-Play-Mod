import os, re, sys

# Simplified FGD parser: finds orphan top-level blocks, unbalanced brackets,
# and base() references to classes not yet defined (in Hammer load order).
BINDIR = r'E:\Steam\steamapps\common\Source SDK Base 2013 Singleplayer\bin'
defined = set()
loaded = set()
problems = []

def tokenize(text):
    toks = []
    i, line, n = 0, 1, len(text)
    while i < n:
        c = text[i]
        if c == '\n':
            line += 1; i += 1
        elif c in ' \t\r':
            i += 1
        elif text.startswith('//', i):
            while i < n and text[i] != '\n': i += 1
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == '\n': line += 1
                j += 1
            toks.append(('str', text[i+1:j], line)); i = j + 1
        elif c in '[]():=,+@':
            toks.append((c, c, line)); i += 1
        else:
            j = i
            while j < n and text[j] not in ' \t\r\n[]():=,+"@':
                j += 1
            toks.append(('id', text[i:j], line)); i = j
    return toks

def load(path):
    path = os.path.normpath(path)
    if path.lower() in loaded: return
    loaded.add(path.lower())
    name = os.path.basename(path)
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        problems.append(f"{name}: cannot open"); return
    t = tokenize(text)
    i = 0
    while i < len(t):
        k, v, ln = t[i]
        if k == '@':
            kind = t[i+1][1].lower()
            if kind == 'include':
                inc = os.path.join(os.path.dirname(path), t[i+2][1])
                if not os.path.exists(inc):
                    inc = os.path.join(BINDIR, t[i+2][1])  # Hammer falls back to its cwd (bin)
                load(inc); i += 3; continue
            if kind in ('mapsize', 'materialexclusion', 'autovisgroup'):
                # skip to matching end of its own ( ) or [ ] group
                i += 2
                if i < len(t) and t[i][0] in '([':
                    op = t[i][0]; cl = ')' if op == '(' else ']'; d = 0
                    while i < len(t):
                        if t[i][0] == op: d += 1
                        elif t[i][0] == cl:
                            d -= 1
                            if d == 0: i += 1; break
                        i += 1
                continue
            # class header: @XClass helpers... = name [: "desc"] [ body ]
            j = i + 2
            bases = []
            while j < len(t) and t[j][0] != '=':
                if t[j][0] == 'id' and t[j][1].lower() == 'base' and t[j+1][0] == '(':
                    j += 2
                    while t[j][0] != ')':
                        if t[j][0] == 'id': bases.append((t[j][1], t[j][2]))
                        j += 1
                elif t[j][0] == '(':
                    d = 0
                    while True:
                        if t[j][0] == '(': d += 1
                        elif t[j][0] == ')':
                            d -= 1
                            if d == 0: break
                        j += 1
                elif t[j][0] == '@' or t[j][0] == '[':
                    problems.append(f"{name}:{ln}: class header without '='"); break
                j += 1
            cname = t[j+1][1]
            for b, bl in bases:
                if b.lower() not in defined:
                    problems.append(f"{name}:{bl}: {cname}: undefined base class '{b}'")
            defined.add(cname.lower())
            j += 2
            if j < len(t) and t[j][0] == ':':
                j += 2
                while j < len(t) and t[j][0] == '+':
                    j += 2
            if j >= len(t) or t[j][0] != '[':
                problems.append(f"{name}:{ln}: {cname}: no [ body ]"); i = j; continue
            d = 0
            while j < len(t):
                if t[j][0] == '[': d += 1
                elif t[j][0] == ']':
                    d -= 1
                    if d == 0: break
                elif t[j][0] == '@':
                    problems.append(f"{name}:{t[j][2]}: '@' inside body of {cname} (unclosed bracket?)")
                j += 1
            if d != 0:
                problems.append(f"{name}:{ln}: {cname}: unbalanced brackets")
            i = j + 1
        else:
            problems.append(f"{name}:{ln}: unexpected top-level token '{v}' (orphan block?)")
            # skip the orphan group
            if k == '[':
                d = 0
                while i < len(t):
                    if t[i][0] == '[': d += 1
                    elif t[i][0] == ']':
                        d -= 1
                        if d == 0: break
                    i += 1
            i += 1

for f in sys.argv[1:]:
    load(f)
print(f"classes defined: {len(defined)}")
print("\n".join(problems) if problems else "no problems found")

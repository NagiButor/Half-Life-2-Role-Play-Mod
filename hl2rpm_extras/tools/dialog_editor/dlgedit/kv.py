"""KeyValues (KV1) reader/writer for HL2RPM dialog files.

The reader keeps `//` comments: a comment line right before a key becomes that
key's `comment`, so notes the author wrote in a dialog file survive an import
into the editor (they become node/option notes).
"""
import re

_TOK = re.compile(r'"((?:[^"\\]|\\.)*)"|(\{)|(\})|(//[^\n]*)|(\[[^\]]*\])|([^\s{}"]+)')


class KV:
    __slots__ = ("name", "value", "children", "comment", "inline")

    def __init__(self, name, value=None, comment=""):
        self.name = name
        self.value = value          # str for leaves, None for blocks
        self.children = []          # list of KV for blocks
        self.comment = comment
        self.inline = False         # writer: one line (dialog options)

    # --- lookup helpers (case-insensitive, like the engine) ---------------
    def get(self, key, default=None):
        k = key.lower()
        for c in self.children:
            if c.value is not None and c.name.lower() == k:
                return c.value
        return default

    def has(self, key):
        k = key.lower()
        return any(c.value is not None and c.name.lower() == k for c in self.children)

    def block(self, key):
        k = key.lower()
        for c in self.children:
            if c.value is None and c.name.lower() == k:
                return c
        return None

    def blocks(self):
        return [c for c in self.children if c.value is None]

    def values(self):
        return [c for c in self.children if c.value is not None]

    # --- building --------------------------------------------------------
    def add(self, name, value=None, comment=""):
        kv = KV(name, value, comment)
        self.children.append(kv)
        return kv

    def __repr__(self):
        return "KV(%r, %r, %d children)" % (self.name, self.value, len(self.children))


def read_text(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return raw.decode("utf-16")
    if raw[:3] == b"\xef\xbb\xbf":
        raw = raw[3:]
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("cp1251")


def parse(text):
    """Parse KV text into a root KV whose children are the top-level blocks."""
    root = KV("<root>")
    stack = [root]
    key = None
    pending = []
    for m in _TOK.finditer(text):
        q, lb, rb, comment, cond, bare = m.groups()
        if cond:
            continue
        if comment:
            # only full-line comments become notes; trailing ones too (same line
            # after a value) would also be fine, keep it simple
            c = comment[2:].strip()
            if c:
                pending.append(c)
            continue
        if lb:
            node = KV(key if key is not None else "", None, "\n".join(pending))
            pending = []
            stack[-1].children.append(node)
            stack.append(node)
            key = None
        elif rb:
            if len(stack) > 1:
                stack.pop()
            key = None
            pending = []
        else:
            tok = q if q is not None else bare
            if key is None:
                key = tok
            else:
                stack[-1].children.append(KV(key, tok, "\n".join(pending)))
                pending = []
                key = None
    return root


def parse_file(path):
    return parse(read_text(path))


def quote(s):
    # KeyValues in the engine is read without escape sequences: a double quote
    # inside a value would end it. The editor never stores straight quotes in
    # texts (see model.clean_text), this is the last line of defence.
    return '"' + str(s).replace('"', "''") + '"'


def write(kv, indent=0, out=None):
    """Serialize a KV block. Blocks flagged `inline` whose children are all
    leaves are written on one line (dialog options)."""
    if out is None:
        out = []
    tab = "\t" * indent
    if kv.comment:
        for line in kv.comment.splitlines():
            out.append(tab + "// " + line)
    if kv.value is not None:
        out.append(tab + quote(kv.name) + " " + quote(kv.value))
        return out
    if kv.inline and kv.children and all(c.value is not None and not c.comment for c in kv.children):
        body = "  ".join(quote(c.name) + " " + quote(c.value) for c in kv.children)
        out.append(tab + quote(kv.name) + " { " + body + " }")
        return out
    out.append(tab + quote(kv.name))
    out.append(tab + "{")
    prev_block = False
    for i, c in enumerate(kv.children):
        is_block = c.value is None
        if i > 0 and (is_block and indent <= 1 or prev_block and indent <= 1):
            out.append("")
        write(c, indent + 1, out)
        prev_block = is_block
    out.append(tab + "}")
    return out


def dumps(kv, header=""):
    lines = []
    if header:
        lines.extend("// " + h for h in header.splitlines())
    lines.extend(write(kv, 0))
    return "\n".join(lines) + "\n"

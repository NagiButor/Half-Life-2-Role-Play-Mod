"""Automatic placement of dialog nodes.

A chain of lines joined by «далее» (auto_next) is a monologue: it is stacked
vertically as one block. Blocks go into columns by depth from the start node
(longest path, back links ignored); a column is ordered by the rows of the
blocks that lead into it."""

COL_W = 400
ROW_GAP = 30
CHAIN_GAP = 18
NODE_W = 300


def node_height(n):
    """Must match graph.NodeItem geometry."""
    rows = len(n.options) if n.options else 1
    return 30 + 62 + 6 + rows * 30 + 6


def successors(n):
    out = []
    if n.auto_next != -1:
        out.append(n.auto_next)
    for o in n.options:
        if o.next != -1:
            out.append(o.next)
        if o.has_after():
            out.append(o.after_next)
    return [x for x in out if x != -1]


def _incoming(npc):
    inc = {n.id: [] for n in npc.nodes}
    for n in npc.nodes:
        for v in successors(n):
            if v in inc:
                inc[v].append(n.id)
    return inc


def _chains(npc):
    """node id -> chain (list of ids), chains keyed by head."""
    by_id = {n.id: n for n in npc.nodes}
    inc = _incoming(npc)
    roots = {npc.start_node}
    if npc.returning.next != -1:
        roots.add(npc.returning.next)
    cont = set()
    for n in npc.nodes:
        t = n.auto_next
        if (not n.options and t in by_id and t != n.id and t not in roots and len(inc[t]) == 1):
            cont.add(t)
    chains = {}
    owner = {}
    for n in sorted(npc.nodes, key=lambda x: x.id):
        if n.id in cont:
            continue
        ch = [n.id]
        cur = n
        while not cur.options and cur.auto_next in cont and cur.auto_next not in ch:
            ch.append(cur.auto_next)
            cur = by_id[cur.auto_next]
        chains[n.id] = ch
        for x in ch:
            owner[x] = n.id
    # nodes left out (cycles made only of auto links) become their own chains
    for n in npc.nodes:
        if n.id not in owner:
            chains[n.id] = [n.id]
            owner[n.id] = n.id
    return chains, owner


def auto_layout(npc, only_ids=None):
    if not npc.nodes:
        return
    by_id = {n.id: n for n in npc.nodes}
    chains, owner = _chains(npc)
    succ = {}
    for head, ch in chains.items():
        s = []
        for nid in ch:
            for v in successors(by_id[nid]):
                if v in owner and owner[v] != head and owner[v] not in s:
                    s.append(owner[v])
        succ[head] = s

    depth = {}
    order = []
    onstack = set()

    def dfs(u, d):
        if u in onstack:
            return
        if u in depth and depth[u] >= d:
            return
        depth[u] = d
        if u not in order:
            order.append(u)
        onstack.add(u)
        for v in succ[u]:
            dfs(v, d + 1)
        onstack.discard(u)

    if npc.start_node in owner:
        dfs(owner[npc.start_node], 0)
    if npc.returning.next != -1 and npc.returning.next in owner:
        dfs(owner[npc.returning.next], 1)
    base = (max(depth.values()) + 2) if depth else 0
    for head in sorted(chains):
        if head not in depth:
            dfs(head, base)

    cols = {}
    for h in order:
        cols.setdefault(depth[h], [])
        if h not in cols[depth[h]]:
            cols[depth[h]].append(h)

    parents = {}
    for h, s in succ.items():
        for v in s:
            parents.setdefault(v, []).append(h)
    row_of = {}
    x0 = 260.0
    for c in sorted(cols):
        col = cols[c]
        if c > 0:
            def key(h):
                ps = [row_of[p] for p in parents.get(h, []) if p in row_of]
                return (sum(ps) / len(ps)) if ps else 1e9
            col.sort(key=key)
        y = 0.0
        for h in col:
            row_of[h] = y
            for nid in chains[h]:
                n = by_id[nid]
                if only_ids is None or nid in only_ids:
                    n.pos = [x0 + c * COL_W, y]
                y += node_height(n) + CHAIN_GAP
            y += ROW_GAP - CHAIN_GAP
    s = by_id.get(npc.start_node)
    if s:
        npc.start_pos = [s.pos[0] - 230, s.pos[1] + 6]
        npc.returning.pos = [s.pos[0] - 300, s.pos[1] + 80]

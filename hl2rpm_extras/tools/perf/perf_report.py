"""Per-step summary of the frame profiler (r_hl2rpm_gpuprof 1) from console.log.

  python perf_report.py <console.log | run output.txt> [--since-line N] [--skip 2] [--top 14]

The tour prints one "[tour] step N ..." line at the end of each shot step; the [gpuprof] and
[cpuprof] lines (one per second) printed before it belong to that step. The first --skip
lines of each step are dropped (the camera just moved: shadows, GI, the light reach catch up).
For every step: frame time of the tour, GPU frame and period, the passes sorted by cost, and
the main-thread systems.
"""
import re
import sys
import argparse
from collections import OrderedDict

RX_TOUR = re.compile(r'^\[tour\] step (\d+) "(.*)": ([\d.]+) ms avg \(([\d.]+) fps\), worst ([\d.]+) ms')
RX_GPU = re.compile(r'^\[gpuprof\] (\d+) fr: gpu ([\d.]+) period ([\d.]+) frame ([\d.]+) ms(.*)$')
RX_CPU = re.compile(r'^\[cpuprof\] (\d+) fr:(.*)$')


RX_PAIR = re.compile(r'^(.+?) ([\d.]+)(?: x([\d.]+))?$')


def parse_pairs(tail):
    out = OrderedDict()
    for part in tail.split('|'):
        part = part.strip()
        m = RX_PAIR.match(part)
        if m:
            out[m.group(1)] = float(m.group(2))
            if m.group(3):
                out[m.group(1) + '#n'] = float(m.group(3))
    return out


def avg_dicts(dicts):
    acc = OrderedDict()
    for d in dicts:
        for k, v in d.items():
            acc[k] = acc.get(k, 0.0) + v
    n = max(1, len(dicts))
    return OrderedDict((k, v / n) for k, v in acc.items())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--since-line', type=int, default=0)
    ap.add_argument('--skip', type=int, default=2)
    ap.add_argument('--top', type=int, default=14)
    ap.add_argument('--csv', default='')
    a = ap.parse_args()

    lines = open(a.log, encoding='utf-8', errors='replace').read().splitlines()[a.since_line:]
    gpu, cpu = [], []
    steps = []
    for ln in lines:
        ln = ln.strip()
        m = RX_GPU.match(ln)
        if m:
            gpu.append((float(m.group(2)), float(m.group(3)), float(m.group(4)), parse_pairs(m.group(5))))
            continue
        m = RX_CPU.match(ln)
        if m:
            cpu.append(parse_pairs(m.group(2)))
            continue
        m = RX_TOUR.match(ln)
        if m:
            g = gpu[a.skip:] if len(gpu) > a.skip else gpu
            c = cpu[a.skip:] if len(cpu) > a.skip else cpu
            steps.append(dict(step=int(m.group(1)), cmd=m.group(2), ms=float(m.group(3)), fps=float(m.group(4)),
                              worst=float(m.group(5)), gpu=g, cpu=c))
            gpu, cpu = [], []

    rows = []
    for s in steps:
        print('=' * 100)
        print('step %d  %s' % (s['step'], s['cmd'][:90]))
        print('  tour: %.2f ms (%.1f fps), worst %.1f' % (s['ms'], s['fps'], s['worst']))
        if s['gpu']:
            n = len(s['gpu'])
            g_ms = sum(x[0] for x in s['gpu']) / n
            p_ms = sum(x[1] for x in s['gpu']) / n
            f_ms = sum(x[2] for x in s['gpu']) / n
            passes = avg_dicts([x[3] for x in s['gpu']])
            print('  gpu %.2f  period %.2f  frame %.2f  (%d samples)' % (g_ms, p_ms, f_ms, n))
            top = sorted(passes.items(), key=lambda kv: -kv[1])[:a.top]
            print('  GPU: ' + ', '.join('%s %.2f' % kv for kv in top if kv[1] >= 0.05))
            rows.append((s['step'], s['ms'], g_ms, p_ms, passes))
        if s['cpu']:
            cp = avg_dicts(s['cpu'])
            ms = [(k, v) for k, v in cp.items() if not k.endswith('#n')]
            top = sorted(ms, key=lambda kv: -kv[1])[:a.top]
            print('  CPU: ' + ', '.join('%s %.2f%s' % (k, v, (' x%.1f' % cp[k + '#n']) if (k + '#n') in cp and cp[k + '#n'] != 1.0 else '')
                                     for k, v in top if v >= 0.05))

    if a.csv and rows:
        names = []
        for r in rows:
            for k in r[4]:
                if k not in names:
                    names.append(k)
        with open(a.csv, 'w', encoding='utf-8') as f:
            f.write('step,tour_ms,gpu_ms,period_ms,' + ','.join(names) + '\n')
            for r in rows:
                f.write('%d,%.2f,%.2f,%.2f,' % r[:4] + ','.join('%.2f' % r[4].get(k, 0.0) for k in names) + '\n')


if __name__ == '__main__':
    main()

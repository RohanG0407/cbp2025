#!/usr/bin/env python3
import os
import re
import argparse
import numpy as np
import matplotlib.pyplot as plt


def parse_log(path):
    """
    Parse a .log file for MPKI and CycWP values under the branch-prediction table.
    Returns (mpki, cycwp) as floats.
    """
    with open(path) as f:
        lines = f.readlines()
    for i, line in enumerate(lines):
        if 'MPKI' in line and 'CycWP' in line and 'Instr' in line:
            header = line.strip().split()
            for j in range(i+1, len(lines)):
                val_line = lines[j].strip()
                if not val_line or val_line.startswith('-'):
                    continue
                vals = val_line.split()
                if len(vals) >= len(header):
                    data = dict(zip(header, vals))
                    mpki = float(data['MPKI'].rstrip('%'))
                    cycwp = float(data['CycWP'].rstrip('%'))
                    return mpki, cycwp
    raise ValueError(f"Metrics not found in {path}")


def main():
    p = argparse.ArgumentParser(
        description='Plot per-benchmark % reduction of MPKI and CycWP vs baseline logs'
    )
    p.add_argument('--workload', required=True,
                   help='Name of workload subdirectory (e.g. int, fp)')
    p.add_argument('--baseline-dir', required=True,
                   help='Path to baseline_full_results directory')
    p.add_argument('--start-index', type=int, default=None,
                   help='Lowest benchmark index to include (inclusive)')
    p.add_argument('--end-index', type=int, default=None,
                   help='Highest benchmark index to include (inclusive)')
    p.add_argument('result_dirs', nargs='+',
                   help='Other result directories to compare')
    p.add_argument('--out-prefix', default='bench_reduction',
                   help='Prefix for output files')
    args = p.parse_args()

    # Collect and filter baseline logs
    base_path = os.path.join(args.baseline_dir, args.workload)
    all_logs = sorted([f for f in os.listdir(base_path) if f.endswith('.log')])
    logs = []
    for log in all_logs:
        m = re.search(r'_(\d+)', log)
        if not m:
            continue
        idx = int(m.group(1))
        if ((args.start_index is None or idx >= args.start_index) and
            (args.end_index   is None or idx <= args.end_index)):
            logs.append(log)
    if not logs:
        raise ValueError("No logs found in range")

    # Parse baseline
    baseline = {}
    for log in logs:
        mp, cw = parse_log(os.path.join(base_path, log))
        baseline[log] = {'MPKI': mp, 'CycWP': cw}

    # Compute reductions
    red_mpki = {}
    red_cycwp = {}
    for rd in args.result_dirs:
        label = os.path.basename(os.path.normpath(rd))
        red_mpki[label] = []
        red_cycwp[label] = []
        path = os.path.join(rd, args.workload)
        for log in logs:
            mp, cw = parse_log(os.path.join(path, log))
            b = baseline[log]
            red_mpki[label].append((b['MPKI'] - mp) / b['MPKI'] * 100 if b['MPKI'] else 0)
            red_cycwp[label].append((b['CycWP'] - cw) / b['CycWP'] * 100 if b['CycWP'] else 0)

    # Plotting helper
    def plot(vals_dict, ylabel, fname):
        benchmarks = [os.path.splitext(l)[0] for l in logs]
        x = np.arange(len(benchmarks))
        n = len(vals_dict)
        width = 0.8 / n if n else 0.8

        fig, ax = plt.subplots()
        ax.set_title(ylabel)
        for i, (label, vals) in enumerate(vals_dict.items()):
            ax.bar(x + i*width, vals, width, label=label)
        ax.set_xticks(x + width*(n-1)/2)
        ax.set_xticklabels(benchmarks, rotation=45, ha='right')
        ax.set_ylabel(ylabel)
        ax.set_xlabel('Individual Benchmark Trace')
        ax.legend()
        fig.tight_layout()
        fig.savefig(fname)
        print(f"Saved {fname}")

    # Generate plots
    plot(red_mpki, '% Reduction in MPKI', f"{args.out_prefix}_{args.workload}_mpki.png")
    plot(red_cycwp, '% Reduction in CycWP', f"{args.out_prefix}_{args.workload}_cycwp.png")

if __name__ == '__main__':
    main()

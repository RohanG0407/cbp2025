#!/usr/bin/env python3
import re
import os
import argparse
import subprocess
import numpy as np
import matplotlib.pyplot as plt

def run_and_capture(script, trace_dir, results_dir):
    """Run the trace_exec script and return its stdout as a string."""
    cmd = [
        "python3", script,
        "--trace_dir", trace_dir,
        "--results_dir", results_dir
    ]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return res.stdout


def parse_output(text):
    """
    From the captured stdout, extract:
      mpki[wl]   = per-workload Branch Misprediction PKI AMean
      cycwp[wl]  = per-workload Cycles On Wrong-Path PKI AMean
      agg_mpki   = overall Branch Misprediction PKI AMean
      agg_cycwp  = overall Cycles On Wrong-Path PKI AMean
    """
    mpki = {}
    cycwp = {}
    agg_mpki = None
    agg_cycwp = None

    # per-workload metrics
    re_mpki = re.compile(r'^WL:(\w+)\s+Branch Misprediction PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    re_cyc  = re.compile(r'^WL:(\w+)\s+Cycles On Wrong-Path PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    
    # aggregate metrics
    re_agg_mpki = re.compile(r'^\s*Branch Misprediction PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    re_agg_cycwp = re.compile(r'^\s*Cycles On Wrong-Path PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)

    # extract per-workload
    for wl, val in re_mpki.findall(text):
        mpki[wl] = float(val)
    for wl, val in re_cyc.findall(text):
        cycwp[wl] = float(val)

    # extract aggregate
    m = re_agg_mpki.search(text)
    if m:
        agg_mpki = float(m.group(1))
    m2 = re_agg_cycwp.search(text)
    if m2:
        agg_cycwp = float(m2.group(1))

    return mpki, cycwp, agg_mpki, agg_cycwp


def plot_bar(data, metric_key, ylabel, output_file):
    """
    data: { label: { 'mpki': {...}, 'cycwp': {...}, 'agg_mpki': float, 'agg_cycwp': float } }
    metric_key: 'mpki' or 'cycwp'

    Adds an 'Average' category at the end, using the aggregate metric.
    """
    labels = list(data.keys())
    orig_workloads = sorted({w for d in data.values() for w in d[metric_key]})
    workloads = orig_workloads + ['Average']

    x = np.arange(len(workloads))
    n = len(labels)
    width = 0.8 / n if n else 0.8

    fig, ax = plt.subplots()
    for i, lab in enumerate(labels):
        vals = [data[lab][metric_key].get(w, 0) for w in orig_workloads]
        # pull aggregate if available, else fallback to computed mean
        agg_key = 'agg_' + metric_key
        avg = data[lab].get(agg_key)
        if avg is None and vals:
            avg = sum(vals) / len(vals)
        vals.append(avg if avg is not None else 0)

        ax.bar(x + i*width, vals, width, label=lab)

    ax.set_xticks(x + width*(n-1)/2)
    ax.set_xticklabels(workloads)
    ax.set_ylabel(ylabel)
    ax.set_xlabel("Workload Category")
    ax.legend()
    fig.tight_layout()
    fig.savefig(output_file)
    print(f" → Saved {output_file}")


def main():
    p = argparse.ArgumentParser(
        description="Run trace_exec script over multiple results_dirs and plot MPKI/CycWP"
    )
    p.add_argument("--script", default="scripts/trace_exec_training_list.py",
                   help="Path to the trace_exec_training_list.py script")
    p.add_argument("--trace-dir", required=True,
                   help="Common trace directory to pass to the script")
    p.add_argument("results_dirs", nargs='+',
                   help="One or more results_dir paths to compare")
    p.add_argument("--out-prefix", default="comparison",
                   help="Prefix for output plot files")
    args = p.parse_args()

    all_data = {}
    for rd in args.results_dirs:
        label = os.path.basename(os.path.normpath(rd))
        print(f"[*] Running on {label}…")
        out = run_and_capture(args.script, args.trace_dir, rd)
        mpki, cycwp, agg_mpki, agg_cycwp = parse_output(out)
        all_data[label] = {
            "mpki": mpki,
            "cycwp": cycwp,
            "agg_mpki": agg_mpki,
            "agg_cycwp": agg_cycwp
        }

    # plot MPKI
    plot_bar(
        all_data,
        metric_key="mpki",
        ylabel="Branch Misprediction PKI (AMean)",
        output_file=f"{args.out_prefix}_mpki.png"
    )

    # plot Cycles-on-WP
    plot_bar(
        all_data,
        metric_key="cycwp",
        ylabel="Cycles On Wrong-Path PKI (AMean)",
        output_file=f"{args.out_prefix}_cycwp.png"
    )

if __name__ == "__main__":
    main()

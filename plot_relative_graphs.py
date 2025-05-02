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
    Extract:
      mpki[wl]   = per-workload Branch Misprediction PKI AMean
      cycwp[wl]  = per-workload Cycles On Wrong-Path PKI AMean
      agg_mpki   = overall Branch Misprediction PKI AMean
      agg_cycwp  = overall Cycles On Wrong-Path PKI AMean
    """
    mpki = {}
    cycwp = {}
    agg_mpki = None
    agg_cycwp = None

    re_mpki = re.compile(r'^WL:(\w+)\s+Branch Misprediction PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    re_cyc  = re.compile(r'^WL:(\w+)\s+Cycles On Wrong-Path PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    re_agg_mpki = re.compile(r'^\s*Branch Misprediction PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)
    re_agg_cycwp = re.compile(r'^\s*Cycles On Wrong-Path PKI.*AMean\s*:\s*([\d.]+)', re.MULTILINE)

    for wl, val in re_mpki.findall(text): mpki[wl] = float(val)
    for wl, val in re_cyc.findall(text):  cycwp[wl] = float(val)
    m = re_agg_mpki.search(text)
    if m: agg_mpki = float(m.group(1))
    m2 = re_agg_cycwp.search(text)
    if m2: agg_cycwp = float(m2.group(1))
    return mpki, cycwp, agg_mpki, agg_cycwp


def compute_reduction(all_data, baseline_label, metric_key):
    """
    Compute percent reduction: (baseline - current)/baseline * 100
    """
    baseline = all_data[baseline_label]
    base_vals = baseline[metric_key]
    base_agg = baseline['agg_' + metric_key]
    new_data = {}
    for label, metrics in all_data.items():
        if label == baseline_label:
            continue
        perc = {}
        for wl, base_v in base_vals.items():
            cur_v = metrics[metric_key].get(wl, 0)
            perc[wl] = (base_v - cur_v) / base_v * 100 if base_v else 0
        agg_v = metrics.get('agg_' + metric_key)
        perc_avg = (base_agg - agg_v) / base_agg * 100 if base_agg and agg_v is not None else 0
        new_data[label] = {
            metric_key: perc,
            'agg_' + metric_key: perc_avg
        }
    return new_data


def plot_bar(data, metric_key, ylabel, title, output_file):
    labels = list(data.keys())
    orig_workloads = sorted({w for d in data.values() for w in d[metric_key]})
    workloads = orig_workloads + ['Average']
    x = np.arange(len(workloads))
    n = len(labels)
    width = 0.8 / n if n else 0.8

    fig, ax = plt.subplots()
    # add title
    fig.suptitle(title)

    bar_containers = []
    for i, lab in enumerate(labels):
        vals = [data[lab][metric_key].get(w, 0) for w in orig_workloads]
        avg = data[lab]['agg_' + metric_key]
        vals.append(avg)
        bars = ax.bar(x + i*width, vals, width, label=lab)
        bar_containers.append(bars)

    # Annotate all bars, including zeros
    for bars in bar_containers:
        for bar in bars:
            h = bar.get_height()
            y = h if h > 0 else 0.5
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                y,
                f"{h:.1f}%",
                ha='center', va='bottom', fontsize=8
            )

    ax.set_xticks(x + width*(n-1)/2)
    ax.set_xticklabels(workloads)
    ax.set_ylabel(ylabel)
    ax.set_xlabel("Workload Category")
    ax.legend()
    fig.tight_layout(rect=[0, 0.03, 1, 0.95])  # leave space for suptitle
    fig.savefig(output_file)
    print(f" → Saved {output_file}")


def main():
    p = argparse.ArgumentParser(
        description="Plot % reduction vs baseline across result dirs"
    )
    p.add_argument("--script", default="scripts/trace_exec_training_list.py",
                   help="Path to trace_exec_training_list.py")
    p.add_argument("--trace-dir", required=True,
                   help="Trace directory")
    p.add_argument("--baseline-label", default="baseline_full_results",
                   help="Directory name to use as baseline")
    p.add_argument("results_dirs", nargs='+',
                   help="Result directories to compare")
    p.add_argument("--out-prefix", default="reduction",
                   help="Prefix for output files")
    args = p.parse_args()

    raw = {}
    for rd in args.results_dirs:
        label = os.path.basename(os.path.normpath(rd))
        print(f"[*] Running on {label}…")
        out = run_and_capture(args.script, args.trace_dir, rd)
        mpki, cycwp, agg_mpki, agg_cycwp = parse_output(out)
        raw[label] = {"mpki": mpki, "cycwp": cycwp,
                      "agg_mpki": agg_mpki, "agg_cycwp": agg_cycwp}

    # compute percent reductions (excluding baseline from the legend)
    red_mpki = compute_reduction(raw, args.baseline_label, 'mpki')
    red_cycwp = compute_reduction(raw, args.baseline_label, 'cycwp')

    # plot reductions
    plot_bar(red_mpki, 'mpki', '% Reduction in MPKI vs baseline', "% Reduction in MPKI vs TAGE Baseline",
             f"{args.out_prefix}_mpki.png")
    plot_bar(red_cycwp, 'cycwp', '% Reduction in Cycles/WP vs baseline', "% Reduction in CycWP vs TAGE Baseline",
             f"{args.out_prefix}_cycwp.png")

if __name__ == "__main__":
    main()

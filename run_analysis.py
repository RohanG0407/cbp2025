#!/usr/bin/env python3
import os
import re
import subprocess

OUTPUT_ROOT = 'outputs/media/'  # Root outputs directory
SCRIPT      = 'chain_analyzer.py'  # Chain analyzer script
LOG_FILE    = 'media_results.txt'  # Log file to store all outputs

# Key function to sort benchmark directories by trailing number
def numeric_key(name):
    match = re.search(r"(\d+)$", name)
    return int(match.group(1)) if match else float('inf')


def main():
    # Gather all benchmark directories
    bench_dirs = []
    for entry in os.listdir(OUTPUT_ROOT):
        dir_path = os.path.join(OUTPUT_ROOT, entry)
        trace_file = os.path.join(dir_path, 'trace.txt')
        stats_file = os.path.join(dir_path, 'stats.txt')
        if os.path.isdir(dir_path) and os.path.isfile(trace_file) and os.path.isfile(stats_file):
            bench_dirs.append(entry)

    # Sort benchmarks numerically by their trailing number
    bench_dirs.sort(key=numeric_key)

    with open(LOG_FILE, 'w') as log:
        for bench in bench_dirs:
            dir_path = os.path.join(OUTPUT_ROOT, bench)
            trace = os.path.join(dir_path, 'trace.txt')
            stats = os.path.join(dir_path, 'stats.txt')
            # Write header and flush immediately
            log.write(f"=== Benchmark: {bench} ===\n")
            log.flush()
            # Run chain analyzer, capturing stdout and stderr
            subprocess.run(
                ['python3', SCRIPT, trace, stats],
                stdout=log, stderr=log, check=True
            )

if __name__ == '__main__':
    main()

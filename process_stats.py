#!/usr/bin/env python3
import os
import subprocess
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed

# Worker function for a single trace file
def process_trace(trace_path, trace_dir, output_root, output_name):
    # Determine relative directory and base name
    rel_dir = os.path.relpath(os.path.dirname(trace_path), trace_dir)
    fname = os.path.basename(trace_path)
    if fname.endswith('_trace.gz'):
        base = fname[:-len('_trace.gz')]
    else:
        base = os.path.splitext(fname)[0]

    # Prepare output path
    out_dir = os.path.join(output_root, rel_dir, base)
    os.makedirs(out_dir, exist_ok=True)
    out_file = os.path.join(out_dir, output_name)

    # Run cbp and capture stdout
    print(f"Processing {trace_path} → {out_file}")
    with open(out_file, 'wb') as out:
        subprocess.run(['./cbp', trace_path], stdout=out, check=True)


def main():
    parser = argparse.ArgumentParser(
        description="Run ./cbp on each .gz trace in parallel, writing full output to stats.txt"
    )
    parser.add_argument(
        '--trace-dir', default='traces',
        help='Root folder of .gz traces (with subdirectories)'
    )
    parser.add_argument(
        '--output-root', default='outputs',
        help='Root folder where mirrored outputs go'
    )
    parser.add_argument(
        '--output-name', default='stats.txt',
        help='Filename for cbp output in each trace folder'
    )
    parser.add_argument(
        '-j', '--jobs', type=int, default=os.cpu_count() or 1,
        help='Number of parallel workers (default: CPU count)'
    )
    args = parser.parse_args()

    # Gather all .gz trace paths
    jobs = []
    for root, _, files in os.walk(args.trace_dir):
        for fname in files:
            if not fname.endswith('.gz'):
                continue
            trace_path = os.path.join(root, fname)
            jobs.append((trace_path, args.trace_dir, args.output_root, args.output_name))

    print(f"Found {len(jobs)} traces; processing with {args.jobs} workers.")

    # Execute in parallel
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = [executor.submit(process_trace, *job) for job in jobs]
        for future in as_completed(futures):
            # Errors will propagate here
            future.result()

if __name__ == '__main__':
    main()
#!/usr/bin/env python3
import os
import subprocess
import argparse
import concurrent.futures
from functools import partial

# Worker function to process a single trace
def process_trace(trace_path, out_file, threshold):
    """
    Run './cbp' on trace_path, write to out_file, stop when threshold bytes reached.
    """
    try:
        proc = subprocess.Popen(['./cbp', trace_path], stdout=subprocess.PIPE)
        total = 0
        os.makedirs(os.path.dirname(out_file), exist_ok=True)
        with open(out_file, 'wb') as out:
            while True:
                chunk = proc.stdout.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
                total += len(chunk)
                if total >= threshold:
                    proc.kill()
                    print(f"→ Reached threshold for {os.path.basename(trace_path)}; stopping.")
                    break
        proc.wait()
    except Exception as e:
        print(f"Error processing {trace_path}: {e}")


def gather_jobs(trace_dir, output_root, threshold):
    """
    Walk trace_dir to build list of (trace_path, out_file, threshold) tuples.
    """
    jobs = []
    for root, _, files in os.walk(trace_dir):
        for fname in files:
            if not fname.endswith('.gz'):
                continue
            trace_path = os.path.join(root, fname)
            rel_dir = os.path.relpath(root, trace_dir)
            if fname.endswith('_trace.gz'):
                base = fname[:-len('_trace.gz')]
            else:
                base = os.path.splitext(fname)[0]
            out_dir = os.path.join(output_root, rel_dir, base)
            out_file = os.path.join(out_dir, 'trace.txt')
            jobs.append((trace_path, out_file, threshold))
    return jobs


def main():
    parser = argparse.ArgumentParser(
        description="Process all .gz traces in parallel, cap output at threshold."
    )
    parser.add_argument(
        '--trace-dir', default='traces',
        help='Root directory of .gz traces'
    )
    parser.add_argument(
        '--output-root', default='outputs',
        help='Root directory for mirrored outputs'
    )
    parser.add_argument(
        '-t', '--threshold', type=int,
        default=1 * 1024 * 1024 * 1024,
        help='Byte limit per output (default: 256 MiB)'
    )
    parser.add_argument(
        '-j', '--jobs', type=int,
        default=os.cpu_count() or 1,
        help='Number of parallel workers (default: CPU count)'
    )
    args = parser.parse_args()

    jobs = gather_jobs(args.trace_dir, args.output_root, args.threshold)
    print(f"Found {len(jobs)} traces; processing with {args.jobs} workers.")

    # Use ProcessPool for CPU-bound or heavy I/O tasks
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as exec:
        # partial to fix threshold
        futures = []
        for trace_path, out_file, thresh in jobs:
            futures.append(
                exec.submit(process_trace, trace_path, out_file, thresh)
            )
        # Wait for all to complete
        for f in concurrent.futures.as_completed(futures):
            pass

if __name__ == '__main__':
    main()

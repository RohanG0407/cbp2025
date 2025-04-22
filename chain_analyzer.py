import re
import sys
import argparse
from collections import deque

# Regular expressions to parse lines in trace
uop_pattern = re.compile(
    r'(\d+)::uOP:: \[PC: (0x[\da-f]+) type: (\w+)'
    r'(?: \( tkn:(\d) tar: (0x[\da-f]+)\))?.*?\]',
    re.IGNORECASE
)
input_pattern = re.compile(
    r'input:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)',
    re.IGNORECASE
)
output_pattern = re.compile(
    r'output:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)',
    re.IGNORECASE
)
ea_pattern = re.compile(r'ea: (0x[\da-f]+)', re.IGNORECASE)
stats_index_pattern = re.compile(r'^Index:\s*(0x[\da-fA-F]+)', re.MULTILINE)


def parse_trace(file_path):
    """
    Parses the trace file into a list of uOP entries.
    """
    trace_entries = []
    with open(file_path, 'r') as f:
        for line in f:
            uop_match = uop_pattern.search(line)
            if not uop_match:
                continue
            inputs = input_pattern.findall(line)
            outputs = output_pattern.findall(line)
            ea_match = ea_pattern.search(line)
            ea = ea_match.group(1) if ea_match else None
            trace_entries.append({
                'line_num': int(uop_match.group(1)),
                'pc': uop_match.group(2).lower(),
                'type': uop_match.group(3),
                'taken': int(uop_match.group(4)) if uop_match.group(4) else None,
                'target': uop_match.group(5).lower() if uop_match.group(5) else None,
                'inputs': {int(idx): val for idx, val in inputs},
                'outputs': {int(idx): val for idx, val in outputs},
                'ea': ea,
                'raw_line': line.strip()
            })
    return trace_entries


def extract_branch_pcs_from_stats(stats_file, top_n=10):
    """
    Extract the top N branch PCs from a stats.txt file.
    Looks for lines like 'Index: 0xFFFF..., Count: ...'.
    """
    pcs = []
    try:
        with open(stats_file, 'r') as f:
            for line in f:
                m = stats_index_pattern.match(line)
                if m:
                    pcs.append(m.group(1).lower())
                    if len(pcs) == top_n:
                        break
    except IOError as e:
        print(f"Error reading stats file: {e}")
        sys.exit(1)
    return pcs


def chain_analyzer(trace_entries, branch_pcs=None, window=25):
    """
    Analyze chains for given branch PCs. If branch_pcs is None, analyze all condBrOp.
    """
    branch_chains = {pc: [] for pc in (branch_pcs or [])}
    print(f"Analyzing {len(trace_entries)} entries; tracking PCs: {branch_pcs}")
    for i, entry in enumerate(trace_entries):
        if entry['type'] != 'condBrOp':
            continue
        if branch_pcs and entry['pc'] not in branch_pcs:
            continue
        # get any src reg of branch
        if not entry['inputs']:
            continue
        br_src_reg = next(iter(entry['inputs'].keys()))
        # scan backwards up to window
        for j in range(i-1, max(i-window, -1), -1):
            prev = trace_entries[j]
            if br_src_reg in prev['outputs']:
              if prev['type'] != 'loadOp':
                break
              else:
                pc_load = prev['pc'].lower()
                if entry['pc'] not in branch_chains:
                    branch_chains[entry['pc']] = []
                if pc_load not in branch_chains[entry['pc']]:
                    branch_chains[entry['pc']].append(pc_load)
                    print(f"Branch {entry['pc']} <- load {pc_load} (line {prev['line_num']})")
                break
    print("\nResults:")
    for pc, loads in branch_chains.items():
        print(f"{pc}: {loads}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze restricted chains using trace and stats files."
    )
    parser.add_argument(
        'trace_file', help='Path to trace.txt'
    )
    parser.add_argument(
        'stats_file', help='Path to stats.txt'
    )
    parser.add_argument(
        '-n', '--top-n', type=int, default=10,
        help='Number of top indices to extract from stats'
    )
    parser.add_argument(
        '-w', '--window', type=int, default=25,
        help='Backwards search window size'
    )
    args = parser.parse_args()

    # Extract branch PCs from stats.txt
    branch_pcs = extract_branch_pcs_from_stats(args.stats_file, args.top_n)
    print(f"Extracted {len(branch_pcs)} branch PCs from stats: {branch_pcs}\n")

    # Parse trace and analyze
    entries = parse_trace(args.trace_file)
    chain_analyzer(entries, branch_pcs, window=args.window)

if __name__ == '__main__':
    main()

import sys
import re
from collections import Counter, defaultdict

# Regular expression to extract PC from each uOP line
pc_pattern = re.compile(r'PC: (0x[\da-f]+)', re.IGNORECASE)

# Load trace PCs from a trace file (extracts in order of appearance)
def parse_trace_pcs(trace_file):
    trace_pcs = []
    with open(trace_file, 'r') as f:
        for idx, line in enumerate(f):
            match = pc_pattern.search(line)
            if match:
                print(match.group(1).lower())
                trace_pcs.append((idx, match.group(1).lower()))
    return trace_pcs

# Load PC chains from a provided file (one chain per line, PCs space-separated)
def load_chains(chain_file):
    chains = []
    with open(chain_file, 'r') as f:
        for line in f:
            pcs = [pc.strip().lower() for pc in line.strip().split() if pc.strip()]
            if pcs:
                chains.append(pcs)
    return chains

# Find the earliest occurrence of a chain in the trace by checking subsequences
def locate_chains_in_trace(trace_pcs, chains):
    trace_pc_list = [pc for _, pc in trace_pcs]
    locations = []
    for chain_idx, chain in enumerate(chains):
        for i in range(len(trace_pc_list) - len(chain) + 1):
            if trace_pc_list[i:i+len(chain)] == chain:
                start_uop = trace_pcs[i][0]
                locations.append((chain_idx, start_uop, chain))
                break
        else:
            locations.append((chain_idx, None, chain))
    return locations

# Compute average overlap across all pairs of chains
def compute_average_overlap(chains):
    total_overlap = 0
    total_pairs = 0

    for i in range(len(chains)):
        for j in range(i + 1, len(chains)):
            overlap = len(set(chains[i]) & set(chains[j]))
            total_overlap += overlap
            total_pairs += 1

    avg_overlap = total_overlap / total_pairs if total_pairs else 0
    print(f"\nAverage overlap across all chain pairs: {avg_overlap:.2f} PCs")
    return avg_overlap

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <chain_file> <trace_file>")
        print("<chain_file>: space-separated list of PCs per line")
        print("<trace_file>: trace.txt containing PC lines")
        sys.exit(1)

    chain_file = sys.argv[1]
    trace_file = sys.argv[2]

    trace_pcs = parse_trace_pcs(trace_file)
    chains = load_chains(chain_file)

    print(f"Loaded {len(trace_pcs)} PCs from trace.txt")
    print(f"Loaded {len(chains)} chains from chain file")

    compute_average_overlap(chains)

    print("\nChain locations in trace:")
    locations = locate_chains_in_trace(trace_pcs, chains)
    for chain_idx, uop_idx, chain in locations:
        if uop_idx is not None:
            print(f"Chain {chain_idx + 1} starts at uOP index {uop_idx}: {chain}")
        else:
            print(f"Chain {chain_idx + 1} not found in trace: {chain}")
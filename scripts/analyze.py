import re
import sys

# Regular expressions to parse lines
uop_pattern = re.compile(r'PC: (0x[\da-f]+) type: (\w+)(?: \( tkn:(\d) tar: (0x[\da-f]+)\))?')
input_pattern = re.compile(r'input:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)')
output_pattern = re.compile(r'output:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)')

# Read and parse trace file
def parse_trace(file_path):
    trace_entries = []
    with open(file_path, 'r') as f:
        for line in f:
            uop_match = uop_pattern.search(line)
            inputs = input_pattern.findall(line)
            outputs = output_pattern.findall(line)
            if uop_match:
                trace_entries.append({
                    'pc': uop_match.group(1),
                    'type': uop_match.group(2),
                    'taken': int(uop_match.group(3)) if uop_match.group(3) else None,
                    'target': uop_match.group(4) if uop_match.group(4) else None,
                    'inputs': {int(idx): val for idx, val in inputs},
                    'outputs': {int(idx): val for idx, val in outputs}
                })
    return trace_entries

# Find branch dependencies on loads
def find_branch_dependencies(trace_entries, target_branch_pc=None):
    load_outputs = {}
    branch_instances = []

    for entry in trace_entries:
        # Track outputs from loads
        if entry['type'] == 'loadOp':
            for idx, val in entry['outputs'].items():
                load_outputs[idx] = entry

        # Check if condBrOp uses a value produced by a load
        elif entry['type'] == 'condBrOp':
            if target_branch_pc is None or entry['pc'] == target_branch_pc:
                for idx in entry['inputs'].keys():
                    if idx in load_outputs:
                        branch_instances.append((
                            entry['pc'],
                            idx,
                            load_outputs[idx]['pc'],
                            load_outputs[idx]['outputs'][idx],
                            entry['taken'],
                            entry['target']
                        ))

    return branch_instances

# Example usage:
if __name__ == "__main__":
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print(f"Usage: {sys.argv[0]} <trace_file> [branch_pc]")
        sys.exit(1)

    file_path = sys.argv[1]
    target_branch_pc = sys.argv[2] if len(sys.argv) == 3 else None
    trace_entries = parse_trace(file_path)
    branch_instances = find_branch_dependencies(trace_entries, target_branch_pc)

    for br in branch_instances:
        direction = 'taken' if br[4] else 'not taken'
        print(f"Branch at {br[0]} ({direction}, target: {br[5]}) depends on load at {br[2]} (idx: {br[1]}, loaded val: {br[3]})")

import re
import sys
from collections import deque

# Regular expressions to parse lines
uop_pattern = re.compile(r'(\d+)::uOP:: \[PC: (0x[\da-f]+) type: (\w+)(?: \( tkn:(\d) tar: (0x[\da-f]+)\))?.*?\]', re.IGNORECASE)
input_pattern = re.compile(r'input:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
output_pattern = re.compile(r'output:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
ea_pattern = re.compile(r'ea: (0x[\da-f]+)', re.IGNORECASE)

global_chain_reg = deque(maxlen=16)


# Read and parse trace file
def parse_trace(file_path):
    trace_entries = []
    with open(file_path, 'r') as f:
        for line in f:
            uop_match = uop_pattern.search(line)
            inputs = input_pattern.findall(line)
            outputs = output_pattern.findall(line)
            ea_match = ea_pattern.search(line)
            ea = ea_match.group(1) if ea_match else None
            if uop_match:
                trace_entries.append({
                    'line_num': uop_match.group(1),
                    'pc': uop_match.group(2).lower(),
                    'type': uop_match.group(3),
                    'taken': int(uop_match.group(4)) if uop_match.group(4) else None,
                    'target': uop_match.group(5) if uop_match.group(5) else None,
                    'inputs': {int(idx): val for idx, val in inputs},
                    'outputs': {int(idx): val for idx, val in outputs},
                    'ea': ea,
                    'raw_line': line.strip()
                })
    return trace_entries

# Find the most recent store to an address before a given load
def find_recent_store(trace_entries, load_index):
  load_entry = trace_entries[load_index]
  #print(f"Finding recent store for load entry: {load_entry['raw_line']}")
  current_idx = load_index - 1
  load_ea = load_entry['ea']
  num_stores_in_between = 0
  
  while current_idx >= 0:
    current_entry = trace_entries[current_idx]
    if current_entry['type'] == 'stOp':
      if current_entry.get('ea') == load_ea:
        #print(f"Found recent store entry: {current_entry['raw_line']}")
        return current_entry, num_stores_in_between
      else:
        num_stores_in_between += 1
        #print(f"Found store entry {current_entry['raw_line']} but not matching load EA")
    current_idx -= 1
  return None, None

        
def resolve_depdencies_for_branch(trace_entries, branch_idx, chain):
    branch_entry = trace_entries[branch_idx]
    inputs = branch_entry['inputs']
    store_not_found = False
   # print(f"Branch inputs {inputs}")
    
    current_idx = branch_idx - 1
    
    while inputs and current_idx >= 0:
      current_entry = trace_entries[current_idx]
      #print(f"Current entry {current_entry['raw_line']}")
      for output_idx in current_entry['outputs']:
        
        if current_entry['type'] == 'loadOp':
         # print(f"Found load {output_idx} in outputs")
          if output_idx in inputs:
              #print(f"Found output {output_idx} in inputs")
              chain.append(current_entry['raw_line'])
              inputs.pop(output_idx)
              
              ## find recent store for the load
              load_index = current_idx
              find_recent_store_entry, num_stores_in_between = find_recent_store(trace_entries, load_index)
              if find_recent_store_entry:
                  #print(f"Found recent store entry: {find_recent_store_entry['raw_line']}")
                  #print(f"Found {num_stores_in_between} stores in between load and store")
                  chain.append(find_recent_store_entry['raw_line'])
              else:
                store_not_found = True
        else:
          if output_idx in inputs:
              #print(f"Found output {output_idx} in inputs")
              chain.append(current_entry['raw_line'])
              inputs.pop(output_idx)
              
              ## add inputs of current entry to the inputs
              inputs.update(current_entry['inputs'])
              #print(f"Updated inputs {inputs}")
      
      if store_not_found:
        return False, None
      current_idx -= 1
    
    return True, num_stores_in_between
      

def find_dependency_chain(trace_entries, target_branch_pc=None, max_chains=None, unique_only=True):
    if target_branch_pc:
        target_branch_pc = target_branch_pc.lower()

    unique_chains = {}
    unique_chains_store_count = {}
    found_chains = 0
    start_index = 0
    print("trace_entries: %d\n" % len(trace_entries))

    for idx in range(start_index, len(trace_entries)):
        entry = trace_entries[idx]
        if entry['type'] == 'condBrOp' and (target_branch_pc is None or entry['pc'] == target_branch_pc):
            target_branch_idx = idx
            chain = []

            # resolve dependencies for the branch instruction
            correct_chain, num_stores_in_between = resolve_depdencies_for_branch(trace_entries, target_branch_idx, chain)
            # if not correct_chain:
            #     continue

            chain.reverse()
            chain.append(entry['raw_line'])

            # Create a unique signature for the chain
            chain_signature = tuple(line.split('PC: ')[1].split(' ')[0] for line in chain if 'PC: ' in line)

            #iterate through unique chains and count occurrences
            for i, (sig, data) in enumerate(unique_chains.items(), 1):
              if chain_signature == sig:
                global_chain_reg.appendleft(i)
                # print(list(global_chain_reg))
                break
            
            if chain_signature in unique_chains:
                unique_chains[chain_signature]["count"] += 1
            else:
                unique_chains[chain_signature] = {"count": 1, "chain": list(chain)}
                

            # Only print if this is the first occurrence or if not in unique_only mode
            if not unique_only or unique_chains[chain_signature]["count"] == 1:
                found_chains += 1
                print(f"\nDependency Chain {found_chains}:\n")
                if( num_stores_in_between is not None):
                  print("number of stores in between: %d" % num_stores_in_between)
                for instruction in chain:
                    print(instruction)

            if max_chains is not None and found_chains >= max_chains:
                break

    # Print summary of unique chain counts
    print("\nSummary of Unique Dependency Chain Occurrences:\n")
    for i, (sig, data) in enumerate(unique_chains.items(), 1):
        print(f"Chain {i}: occurred {data['count']} time(s)")
        for line in data["chain"]:
            print(line)


# Example usage:
if __name__ == "__main__":
    if len(sys.argv) < 2 or len(sys.argv) > 5:
        print(f"Usage: {sys.argv[0]} <trace_file> [branch_pc] [max_chains] [--all]")
        sys.exit(1)

    file_path = sys.argv[1]
    target_branch_pc = sys.argv[2] if len(sys.argv) >= 3 and not sys.argv[2].isdigit() and not sys.argv[2].startswith('--') else None
    max_chains = int(sys.argv[3]) if len(sys.argv) >= 4 and sys.argv[3].isdigit() else None
    unique_only = '--all' not in sys.argv

    trace_entries = parse_trace(file_path)
    find_dependency_chain(trace_entries, target_branch_pc, max_chains, unique_only)
import re
import sys
from collections import deque

# Regular expressions to parse lines
uop_pattern = re.compile(r'(\d+)::uOP:: \[PC: (0x[\da-f]+) type: (\w+)(?: \( tkn:(\d) tar: (0x[\da-f]+)\))?.*?\]', re.IGNORECASE)
input_pattern = re.compile(r'input:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
output_pattern = re.compile(r'output:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
ea_pattern = re.compile(r'ea: (0x[\da-f]+)', re.IGNORECASE)

global_chain_reg = deque(maxlen=16)

register_file = [0] * 66


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
  
  

# Traverse the entries to find store and load dependencies
def analaze(trace_entries):
    dependence_chains = [
      {
      'stOp': ['0xfffff0d8f23c'],
      'loadOp': ['0xfffff0d8f284'],
      'branch': ['0xfffff0d8f288']
      },
      {
      'stOp': ['0xfffff0d8f1a4'],
      'loadOp': ['0xfffff0d8f284'],
      'branch': ['0xfffff0d8f288']
      },
      {
      'stOp': ['0xfffff0d8f158'],
      'loadOp': ['0xfffff0d8f284'],
      'branch': ['0xfffff0d8f288']
      }
    ]
    
    active_chains = []
    
    for entry in trace_entries:
      if entry['type'] == 'stOp':
        for i in range(len(dependence_chains)):
          if entry['pc'] in dependence_chains[i]['stOp']:
            ## get idx of 2nd input
            print(entry)
            idx = list(entry['inputs'].keys())[1]
            value = register_file[idx]
            print(f"Found store dependency for {entry['pc']} in chain {i} with value {value}")
            active_chains.append({
              'stOp': entry['pc'],
              'address' : entry['ea'],
              'value': value,
              'chainIdx': i
            })
      elif entry['type'] == 'loadOp':
        for active_chain in reversed(active_chains):
          if entry['pc'] in dependence_chains[active_chain['chainIdx']]['loadOp']:
            print(entry)
            print(f"Found load dependency for {entry['pc']} in chain {active_chain['chainIdx']} with value {value}")
            active_chain['value'] = hex(value)
            return
      
      if entry['outputs']:
        # update reg file
        for idx, val in entry['outputs'].items():
          register_file[idx] = int(val, 16)
      
  
  



# Example usage:
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <trace_file>")
        sys.exit(1)

    file_path = sys.argv[1]
    
    trace_entries = parse_trace(file_path)
    print(f"Parsed {len(trace_entries)} entries from {file_path}")
    analaze(trace_entries)
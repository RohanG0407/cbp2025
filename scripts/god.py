import re
import sys
from collections import deque

# Regular expressions to parse lines
uop_pattern = re.compile(r'(\d+)::uOP:: \[PC: (0x[\da-f]+) type: (\w+)(?: \( tkn:(\d) tar: (0x[\da-f]+)\))?.*?\]', re.IGNORECASE)
input_pattern = re.compile(r'input:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
output_pattern = re.compile(r'output:  \(int: \d+, idx: (\d+) val: ([\da-f]+)\)', re.IGNORECASE)
ea_pattern = re.compile(r'ea: (0x[\da-f]+)', re.IGNORECASE)

global_chain_reg = deque(maxlen=5)
global_store_reg = deque(maxlen=5)
store_chain_addr_map = deque(maxlen=1000)
predict_table = {}

register_file = [0] * 66

def pack_ghr(ghr):
    """Convert GHR deque to an int (like a bit vector)."""
    value = 0
    for bit in global_chain_reg:
        value = (value << 2) | bit
    return value

def predict():
    index = pack_ghr(global_chain_reg)
    return predict_table.get(index, "default_prediction")

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
    
    active_store_table = [0] * 3
    
    active_chains = []
    count_load_matches = 0
    count_store_matches = 0
    entry_counter = 0
    correct_load = 0
    total_loads = 0
    predicted_correct = 0
    
    
    
    for entry in trace_entries:
      entry_counter += 1
      if entry_counter < 10_000:
        continue
      if entry['type'] == 'stOp':
        for i in range(len(dependence_chains)):
          if entry['pc'] in dependence_chains[i]['stOp']:
            ## get idx of 2nd input
            idx = list(entry['inputs'].keys())[1]
            value = register_file[idx]
            active_chains.append({
              'stOp': entry['pc'],
              'address' : entry['ea'],
              'value': value,
              'chainIdx': i
            })
            count_store_matches+= 1
            active_store_table[i] = hex(value)
            store_chain_addr_map.append({'address': entry['ea'], 'chainIdx:': i})
            global_store_reg.append(i)
          
            break
      elif entry['type'] == 'loadOp':
        for i in range(len(dependence_chains)):
          if entry['pc'] in dependence_chains[i]['loadOp']:
            for store_map in reversed(list(store_chain_addr_map)):
              if entry['ea'] == store_map['address']:
                
                predicted_chain = predict()
                if predicted_chain == store_map['chainIdx:']:
                  predicted_correct+=1
                
                print("Chain reg ->", end=' ')
                print(list(global_chain_reg))
                print("Store reg ->", end=' ')
                print(list(global_store_reg))
                index = pack_ghr(global_chain_reg)
                predict_table[index] = store_map['chainIdx:']
                global_chain_reg.append(store_map['chainIdx:'])
                break
            for idx, val in entry['outputs'].items():
              if hex(int(val, 16)) in active_store_table:
                correct_load+=1
          
            total_loads+=1
            count_load_matches+=1
          break

      if entry['outputs']:
        # update reg file
        for idx, val in entry['outputs'].items():
          register_file[idx] = int(val, 16)

    print("Load accuracy is", (correct_load / total_loads) * 100)
    print("Predicted accuracy is", (predicted_correct / total_loads) * 100)

# Example usage:
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <trace_file>")
        sys.exit(1)

    file_path = sys.argv[1]
    
    trace_entries = parse_trace(file_path)
    print(f"Parsed {len(trace_entries)} entries from {file_path}")
    analaze(trace_entries)
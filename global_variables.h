#ifndef GLOBAL_VARIABLES_H
#define GLOBAL_VARIABLES_H

#include <unordered_set>
#include <unordered_map>
#include <deque>

enum BranchType
{
  CBZ,
  CBNZ,
};


struct BranchTableEntry
{
  uint64_t tag;
  uint64_t src_reg;
  uint64_t sat_ctr;
  bool override_tage_pred;
  uint64_t store_triggers[8];
  uint64_t num_triggers;
  bool is_linked;
  BranchType br_type;
  uint64_t predicted_load_addr;
};

struct StoreTableEntry
{
  uint64_t tag;
  uint64_t pc;
  uint64_t value;
};

struct TriggerTableEntry 
{
  uint64_t tag;
  uint64_t value;
  uint64_t addr;
  BranchType br_type;
};

struct PredictionTableEntry
{
  uint64_t tag;
  bool taken;
};

struct RetireOp
{
  uint64_t pc;
  ExecuteInfo exec_info;
};

enum AddrPredictorState {
  INVALID,
  TRAINING,
  VALID
};

struct LoadTableEntry {
  uint64_t tag;
  uint64_t br_pc;
  uint64_t last_addr;
  uint64_t  stride;
  uint64_t inflight_loads;
  AddrPredictorState state; 
};

struct SpeculativeInfo {
  uint64_t seq_no;
  uint64_t pc;
  uint64_t predicted_addr;
  bool valid;
};


// Branch Table Info
#define BT_SIZE 65535
#define BT_SAT_COUNTER_MAX 31
extern BranchTableEntry branch_table[BT_SIZE]; // 2^16 entries - 1
extern std::unordered_set<uint64_t> high_mispred_pc;

// Store Table Info
#define ST_SIZE 65535
extern StoreTableEntry store_table[ST_SIZE]; // 2^16 entries - 1

// Store Chain Info
#define SC_SIZE 65535
extern TriggerTableEntry trigger_table[SC_SIZE]; // 2^16 entries - 1

// Prediction Table Info
#define PT_SIZE 65535
extern PredictionTableEntry prediction_table[PT_SIZE]; // 2^16 entries - 1

// Value Predictor Load Table
#define LT_SIZE 65535
extern LoadTableEntry load_table[LT_SIZE]; // 2^16 entries - 1
extern std::unordered_map<uint64_t, SpeculativeInfo> speculation_map;

// RetireOp Queue
extern std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

extern uint64_t RegFile[66];

#endif

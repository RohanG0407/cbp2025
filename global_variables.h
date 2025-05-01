#ifndef GLOBAL_VARIABLES_H
#define GLOBAL_VARIABLES_H

#include <unordered_set>
#include <unordered_map>
#include <deque>

enum BranchType
{
  NA,
  CBZ,
  CBNZ,
  TBZ,
  TBNZ
};

enum ALU_Operation
{
  UNKNOWN,
  TST,
  TEQ,
  CMP,
  CMN
};

struct BranchTableEntry
{
  uint64_t tag;
  uint64_t src_reg;
  uint64_t sat_ctr;
  bool is_linked;
  BranchType br_type;
  uint64_t predicted_load_addr;
  uint64_t prev_value;
  bool prev_taken;
  uint64_t branch_bit_mask; // Mask with 1 at the branch bit
  bool bit_position_matters;
  bool direction_zero_match;
  bool bit_flag;
  long long int correct_counter = 0;
  long long int incorrect_counter = 0;
  bool src_flag[16];
  bool flag_br;
  std::unordered_map<uint64_t, bool> value_prediction_map;
};

struct StoreTableEntry
{
  uint64_t tag;
  uint64_t pc;
};

struct TriggerTableEntry
{
  uint64_t tag;
  BranchType br_type;
  uint64_t branch_bit_mask;
  bool src_flag[16];
  bool flag_br;
  std::unordered_map<uint64_t, bool> value_prediction_map;
};

struct PredictionTableEntry
{
  //uint64_t tag;
  bool taken;
};

struct RetireOp
{
  uint64_t pc;
  ExecuteInfo exec_info;
};

enum AddrPredictorState
{
  INVALID,
  TRAINING,
  VALID_STRIDE,
  VALID_CORRELATION
};

constexpr int LAP_SHIFT_BITS = 4;
constexpr int LAP_HISTORY_LENGTH = 4;
constexpr int LAP_HISTORY_BITS = LAP_HISTORY_LENGTH * LAP_SHIFT_BITS;
constexpr uint64_t LAP_HISTORY_MASK = (1 << LAP_HISTORY_BITS) - 1;
constexpr uint64_t LAP_SUBSET_MASK = (1 << LAP_SHIFT_BITS) - 1;
constexpr uint8_t LAP_STRIDE_MAX_CONFIDENCE = 3;

struct LoadTableEntry
{
  uint64_t tag;
  uint64_t br_pc;
  uint64_t last_addr;
  uint64_t stride;
  uint64_t inflight_loads;
  uint8_t confidence_ctr;
  AddrPredictorState state;
  uint64_t addr_history_reg;
  uint64_t spec_addr_history_reg;
};

struct LinkTable
{
  uint64_t address;
};

struct SpeculativeInfo
{
  uint64_t seq_no;
  uint64_t pc;
  uint64_t predicted_addr;
  bool valid;
};

// Branch Table Info
#define BT_BITS 16
#define BT_SIZE 1 << BT_BITS
#define BT_SAT_COUNTER_MAX 31
extern BranchTableEntry branch_table[BT_SIZE]; // 2^16 entries - 1
extern std::unordered_set<uint64_t> high_mispred_pc;

// Store Table Info
#define ST_BITS 12
#define ST_TAG_BITS 16
#define ST_SIZE 1 << ST_BITS
#define ST_MASK ((1 << ST_BITS) - 1)
#define ST_TAG_MASK ((1 << ST_TAG_BITS) - 1) << ST_BITS
extern StoreTableEntry store_table[ST_SIZE]; // 4096 entries * (64 bits for pc + 16 bits tag) = 40 KB

// Store Chain Info
#define SC_SIZE 65535
extern TriggerTableEntry trigger_table[SC_SIZE]; // 2^16 entries - 1

// Prediction Table Info
#define PT_BITS 16
#define PT_SIZE 1 << PT_BITS
#define PT_MASK ((1 << PT_BITS) - 1)
extern PredictionTableEntry prediction_table[PT_SIZE]; // 65536 entries * (1 bit for taken/not-taken) = 8 KB 

// Value Predictor Load Table
#define LT_SIZE 65535
extern LoadTableEntry load_table[LT_SIZE]; // 2^16 entries - 1
extern std::unordered_map<uint64_t, SpeculativeInfo> speculation_map;
extern LinkTable link_table[LT_SIZE];

// RetireOp Queue
extern std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

extern uint64_t RegFile[66];

#endif

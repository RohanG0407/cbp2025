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

enum aluop
{
 EQ,
 ENQ,
 HQ,
 LQ,
 AT,
 ANT
};

struct alu_reverse_table
{
	uint64_t value;
	bool valid;
};

struct BranchLearningEntry {
  uint64_t tag;
  uint8_t sat_ctr;
};

struct BranchTableEntry
{
  uint64_t tag;
  bool valid;
  uint64_t sat_ctr;
  bool override_tage;
  bool is_linked;
  BranchType br_type;
  uint64_t predicted_load_addr;
  uint64_t prev_value;
  bool prev_taken;
  uint64_t branch_bit_mask; // Mask with 1 at the branch bit
  bool bit_position_matters;
  bool direction_zero_match;
  bool bit_flag;
  bool src_flag[16];
  bool flag_br;
 bool is_alu;
 alu_reverse_table alu_result_entries[6];
 aluop alu_type;
 uint64_t threshold;
 bool override_alu;
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
  //std::unordered_map<uint64_t, bool> value_prediction_map;
  bool is_alu;
  aluop alu_type;
  uint64_t threshold;
  bool override_alu;
};

struct PredictionTableEntry
{
  bool taken;
  bool valid;
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
constexpr int LAP_HISTORY_LENGTH = 3;
constexpr int LAP_HISTORY_BITS = LAP_HISTORY_LENGTH * LAP_SHIFT_BITS;
constexpr uint64_t LAP_HISTORY_MASK = (1 << LAP_HISTORY_BITS) - 1;
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
  uint64_t spec_addr_history_reg; // speculative history not counted to total budget
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

// Branch Learning Table Info
#define BLT_BITS 12
#define BLT_TAG_BITS 16
#define BLT_SIZE 1 << BLT_BITS
#define BLT_MASK ((1 << BLT_BITS) - 1)
#define BLT_TAG_MASK ((1 << BLT_TAG_BITS) - 1) << BLT_BITS
#define BLT_SAT_COUNTER_MAX 31
extern BranchLearningEntry branch_learning_table[BLT_SIZE]; // 4096 entries * (16 bits for tag
                                                            //                 5 bits saturation counter) = 10.5 KB

// Branch Table Info
// #define BT_BITS 16
#define BT_TAG_BITS 16
#define BT_SIZE 16
#define BT_TAG_MASK ((1 << BT_TAG_BITS) - 1)
#define BT_SAT_COUNTER_MAX 31
extern BranchTableEntry branch_table[BT_SIZE];  // 16 entries * (16 bits for tag
                                                //                 1 bit for valid
                                                //                 5 bits for saturation counter
                                                //                 1 bit for override tag
                                                //                 1 bits for is linked 
                                                //                 3 bits for branch type      
                                                //                 64 bits for predicted load addr
                                                //                 64 bits for prev value
                                                //                 1 bit for prev taken
                                                //                 64 bits for branch bit mask
                                                //                 1 bit for bit position matters
                                                //                 1 bit for direction zero match
                                                //                 1 bit for bit flag
                                                //                 16 bits for src flag
                                                //                 1 bit for flag br
                                                //                 1 bit for is alu
                                                //                 6 * (65 bits for alu result entries)
                                                //                 3 bits for alu type
                                                //                 64 bits for threshold
                                                //                 1 bit for override alu) = 699 bits * 32 entries = 1.5 KB

// Store Table Info
#define ST_BITS 12
#define ST_TAG_BITS 16
#define ST_SIZE 1 << ST_BITS
#define ST_MASK ((1 << ST_BITS) - 1)
#define ST_TAG_MASK ((1 << ST_TAG_BITS) - 1) << ST_BITS
extern StoreTableEntry store_table[ST_SIZE]; // 4096 entries * (64 bits for pc + 16 bits tag) = 40 KB

// Store Trigger  Info
#define TT_BITS 12
#define TT_TAG_BITS 16
#define TT_SIZE 1 << TT_BITS
#define TT_MASK ((1 << TT_BITS) - 1)
#define TT_TAG_MASK ((1 << TT_TAG_BITS) - 1) << TT_BITS
extern TriggerTableEntry trigger_table[TT_SIZE]; // 4096 entries * (16 bits for tag
                                                 //                 3 bits branch type
                                                 //                 64 bits for branch bit mask        
                                                 //                 16 bits for src flag
                                                 //                 1 bit for flag br) = 50 KB
// Prediction Table Info
#define PT_BITS 16
#define PT_SIZE 1 << PT_BITS
#define PT_MASK ((1 << PT_BITS) - 1)
extern PredictionTableEntry prediction_table[PT_SIZE]; // 65536 entries * (1 bit for taken/not-taken, 1 for valid) = 16 KB 

// RetireOp Queue
extern std::deque<RetireOp> retire_op_queue; // 16 entires * (136 bytes per entry) = 2.125 KB
#define RETIRE_OP_QUEUE_SIZE 16

// Value Predictor Load Table
#define LDT_TAG_BITS 16
#define LDT_SIZE 16
#define LDT_TAG_MASK ((1 << LDT_TAG_BITS) - 1)
extern LoadTableEntry load_table[LDT_SIZE]; // 16 entries * (16 bits for tag
                                            //                 64 bits branch pc
                                            //                 64 bits for last addr        
                                            //                 16 bits for stride
                                            //                 8 bits for inflight loads
                                            //                 2 bits for confidence ctr
                                            //                 2 bits for state
                                            //                 16 bits for addr history reg) = 172 bits * 32 entries = .33 KB
                                        

#define LKT_BITS 12
#define LKT_SIZE 1 << LKT_BITS
extern LinkTable link_table[LKT_SIZE]; // X entries * (64 bits for address) = 64 bits * 4096 entries = 32 KB
extern std::unordered_map<uint64_t, SpeculativeInfo> speculation_map; // speculative history not counted to total budget



extern uint64_t RegFile[66]; // arch register file not counted to total budget

#endif

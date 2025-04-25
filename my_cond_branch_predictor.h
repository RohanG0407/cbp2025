#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <unordered_set>
#include <cmath>
#include "lib/sim_common_structs.h"

#define ReservationStation_NUM_SOURCES 5
#define trcSim_STUPID_VALUE 0x00DABBA000F00100
#define UINT_64_T_MAX_VALUE 0xFFFFFFFFFFFFFFFF

extern bool DEBUG_MODE;

uint64_t hash(const uint64_t ip_num, uint64_t index_width);

struct SampleHist
{
      uint64_t ghist;
      bool tage_pred;
      //
      SampleHist()
      {
          ghist = 0;
      }
};

struct BranchTableEntry
{
  uint64_t src_reg;
  uint64_t sat_counter;
};

struct RetireOp
{
  uint64_t pc;
  ExecuteInfo exec_info;
};

struct StoreTableEntry
{
  bool is_valid;
  bool is_zero;
  uint64_t pc;
};

struct Store_Table_Entry {
    bool valid;
    uint64_t producer_pc;
};

struct PC_UID_Map_Entry {
    bool valid;
    uint64_t pc;
};

struct ProducerConsumer_Entry {
    uint64_t producer_pc;
    uint64_t consumer_pc;
    bool valid_pair;
};

struct ProducerConsumer_Entry_Memory {
    uint64_t virtual_address;
    bool store_observed;
    ProducerConsumer_Entry prodCons_info;
};

struct PredictionTable_Entry {
    uint64_t pc;
    bool valid;
    uint16_t src_uid;
    bool src_value;
    bool brnz;
    bool prediction;
    bool trcSim_infeasible;
    uint64_t trcSim_prev_value;
    uint64_t trcSim_curr_value;
    bool trcSim_prev_resolveDir;
    uint64_t trcSim_branch_bit_mask;
};

struct Source_Field {
    bool valid;
    uint16_t src_uid;
    uint64_t value;
    bool updated;
};

struct Reservation_Station_Entry {
    bool valid;
    InstClass opcode;
    //uint16_t dest_uid;
    Source_Field src_info[ReservationStation_NUM_SOURCES];
    bool any_update;
};

struct Trigger_Buffer_Entry {
    bool valid;
    uint64_t pc;
    uint16_t dst_uid;
    bool active_src;
};

class Reservation_Station;
class Prediction_Table;

class Trigger_Buffer {
        Trigger_Buffer_Entry* trigger_buffer;
        uint16_t num_entries;
        uint16_t wr_ptr;

        uint16_t get_dst (uint64_t pc);

    public:
        Trigger_Buffer (uint16_t num_entries); 
        ~Trigger_Buffer ();
        void add_entry (uint64_t pc, uint16_t dst_uid);
        void trigger (uint64_t pc, uint64_t data, Reservation_Station& rs, Prediction_Table& pred_table);
        void printState (bool DEBUG_MODE);
};

class Reservation_Station {
        Reservation_Station_Entry* RS;
        uint16_t num_entries;
        uint16_t wr_ptr;

        void printState_line(uint16_t uid);

    public:
        Reservation_Station (uint16_t num_entries);
        ~Reservation_Station ();
        void add_entry (const uint16_t dst_uid, const InstClass instr_class, const std::vector<uint16_t> src_uid_vector, const std::vector<uint64_t> src_reg_value_vector);
        // Wrapper around above function, to support a simpler syntax for single source dependencies.
        void add_entry (const uint16_t dst_uid, const InstClass instr_class, const uint16_t src_uid, const uint64_t src_value);
        void receive_broadcast(uint16_t src_uid, uint64_t value);
        void evaluate(Prediction_Table& pred_table);
        void printState(bool DEBUG_MODE);
        void printState(bool DEBUG_MODE, uint16_t uid);
        void printState_updated(bool DEBUG_MODE);
};

class Prediction_Table
{
        PredictionTable_Entry* prediction_table;
        //std::unordered_map<uint64_t, PredictionTable_Entry> prediction_table;
        uint16_t num_entries;   // max number of hard-to-predict branches which can be pre-computed
        uint16_t wr_ptr; // write pointer in the circular buffer
    public:
        Prediction_Table (uint16_t num_entries);
        ~Prediction_Table();
        bool is_learnt(uint64_t pc);
        bool trcSim_is_predicted(uint64_t pc);
        void add_entry(uint64_t pc, uint16_t src_uid, uint64_t value);
        void receive_broadcast(uint16_t src_uid, uint64_t value);
        bool get_prediction(uint64_t pc);
        void printState(bool DEBUG_MODE);

        void trcSim_markInfeasible(uint16_t src_uid);
        void trcSim_learn_branch_bit(uint64_t pc, uint64_t value, bool taken);
        void trcSim_learn_branch_type(uint64_t pc, uint64_t value, bool taken);
        void trcSim_update_branch_bit_state(uint64_t pc, uint64_t value);
};

class Producer_Consumer_Pairs_Memory
{
        ProducerConsumer_Entry_Memory* ProdCons_Table;
        uint16_t num_entries;
        uint16_t wr_ptr;
        uint64_t hashedVA_width;

        //uint64_t hash(const uint64_t ip_num);

    public:
        Producer_Consumer_Pairs_Memory (uint16_t num_entries, uint64_t hashedVA_width);
        ~Producer_Consumer_Pairs_Memory ();
        bool is_tracked (uint64_t virt_addr);
        bool is_traced (uint64_t virt_addr);
        void add_memVA (uint64_t virt_addr, uint64_t consumer_pc);
        void record_producer(uint64_t virt_addr, uint64_t producer_pc);
        void record_consumer(uint64_t virt_addr, uint64_t consumer_pc);
        uint64_t get_producerPC (uint64_t virt_addr);
        void printState (bool DEBUG_MODE, uint64_t virt_addr);
};

class Producer_Consumer_Pairs_Register
{
        ProducerConsumer_Entry* ProdCons_Table;
    public:
        Producer_Consumer_Pairs_Register (uint16_t num_entries);
        ~Producer_Consumer_Pairs_Register ();
        uint64_t get_producerPC (uint8_t src_reg);
        void record_producer(uint8_t src_reg, uint64_t pc);
        void record_consumer(uint8_t dst_reg, uint64_t pc);
        void printState(bool DEBUG_MODE, uint8_t reg);
};

// class PC_2_uid_map
// {
//         std::unordered_map<uint64_t, uint16_t> uid;
//         uint16_t num_entries;   // max number of pc->uid mappings which can be tracked
//         uint16_t next_uid;  // uid given to next new pc
//     public:
//         PC_2_uid_map (uint16_t num_entries) {
//             this->num_entries = num_entries;
//         }

//         uint16_t get_uid(uint64_t pc) {
//             if (uid.count(pc)) {
//                 return uid[pc];
//             } else { // Allocate a uid to this unknown pc
//                 // Allocation is like a circular buffer. Overwrite oldest allocations once full
//                 uint16_t new_uid = next_uid;
//                 uid.insert({pc, new_uid});
//                 next_uid = (next_uid + 1) >= num_entries ? 0 : (next_uid + 1);

//                 return new_uid;
//             }
//         }
// };

class PC_UID_Map
{
        // Implemented as a circular buffer
        PC_UID_Map_Entry* PC_UID_Map_Table;
        uint16_t num_entries; // Max number of pc <--> uid mappings which can be tracked
        uint16_t next_uid;  // uid given to next new pc
    public:
        PC_UID_Map (uint16_t num_entries);
        ~PC_UID_Map ();
        uint16_t get_uid(uint64_t pc);
};

class Seeker_Buffer {
        std::unordered_set<uint64_t> seeker_buffer;
    public:
        void insert(uint64_t elem);
        void remove(uint64_t elem);
        bool exists(uint64_t elem);
        void printState(bool DEBUG_MODE);
};

// From Memory Address to PC
class Store_Table {
        Store_Table_Entry* ST_Table;
        uint64_t num_entries;
        uint64_t index_width;
    public:
        Store_Table (uint64_t num_entries);
        ~Store_Table ();
        void record (uint64_t memVA, uint64_t pc);
        bool is_recorded(uint64_t memVA);
        uint64_t get_pc(uint64_t memVA);
        void printState(uint64_t memVA);
};

// class SampleCondPredictor
// {
//         SampleHist active_hist;
//         std::unordered_map<uint64_t/*key*/, SampleHist/*val*/> pred_time_histories;
//     public:
//         SampleCondPredictor (void);
//         void setup();
//         void terminate();
//         // sample function to get unique instruction id
//         uint64_t get_unique_inst_id(uint64_t seq_no, uint8_t piece) const;
//         bool predict (uint64_t seq_no, uint8_t piece, uint64_t PC, bool tage_pred);
//         bool predict_using_given_hist (uint64_t seq_no, uint8_t piece, uint64_t PC, const SampleHist& hist_to_use, bool pred_time_predict);
//         void history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC);
//         void update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC);
//         void update (uint64_t PC, bool resolveDir, bool pred_taken, uint64_t nextPC, const SampleHist& hist_to_use);
// };
// =================
// Predictor End
// =================

#endif
//static SampleCondPredictor cond_predictor_impl;

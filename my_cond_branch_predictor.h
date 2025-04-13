#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <unordered_set>
#include <cmath>

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

struct PC_UID_Map_Entry
{
    bool valid;
    uint64_t pc;
};

struct ProducerConsumer_Entry
{
    uint64_t producer_pc;
    uint64_t consumer_pc;
    bool valid_pair;
};

struct ProducerConsumer_Entry_Memory
{
    uint64_t virtual_address;
    ProducerConsumer_Entry prodCons_info;
};

struct PredictionTable_Entry {
    uint64_t pc;
    bool valid;
    uint16_t src_uid;
    bool prediction;
};

struct Source_Field {
    bool valid;
    uint16_t src_uid;
    uint64_t value;
};

struct Reservation_Station_Entry {
    bool valid;
    InstClass opcode;
    //uint16_t dest_uid;
    Source_Field src_info[3];
    bool any_update;
};

struct Trigger_Buffer_Entry {
    bool valid;
    uint64_t pc;
    uint16_t dst_uid;
};

class Trigger_Buffer {
        Trigger_Buffer_Entry* trigger_buffer;
        uint16_t num_entries;
        uint16_t wr_ptr;
    public:
        Trigger_Buffer (uint16_t num_entries) {
            this->num_entries = num_entries;
            trigger_buffer = new Trigger_Buffer_Entry[num_entries];
            wr_ptr = 0;

            // Mark all entries as invalid
            for (int i = 0; i < num_entries; i++) {
                trigger_buffer[i].valid = false;
            }
        }
        
        ~Trigger_Buffer () {
            if (trigger_buffer)
                delete[] trigger_buffer;
        }

        void add_entry (uint64_t pc, uint16_t dst_uid) {
            // Don't invalidate any existing entry. A single store can trigger multiple DFGs
            trigger_buffer[wr_ptr].valid = true;
            trigger_buffer[wr_ptr].pc = pc;
            trigger_buffer[wr_ptr].dst_uid = dst_uid;

            wr_ptr++;
            wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
        }

        void printState () {
            std::cout << "Trigger Buffer:\n";
            for (int i = 0; i < num_entries; i++) {
                if (trigger_buffer[i].valid)
                    std::cout << "PC: 0x" << std::hex << trigger_buffer[i].pc
                            << " | dst_uid: " << std::dec << trigger_buffer[i].dst_uid << "\n";
            }
        }
};

class Reservation_Station {
        Reservation_Station_Entry* RS;
        uint16_t num_entries;
    public:
        Reservation_Station (uint16_t num_entries) {
            this->num_entries = num_entries;
            RS = new Reservation_Station_Entry[num_entries];

            for (int i = 0; i < num_entries; i++) {
                RS[i].valid = false;
                for (Source_Field src: RS[i].src_info) {
                    src.valid = false;
                }
            }
        }

        ~Reservation_Station () {
            if (RS)
                delete[] RS;
        }

        void add_entry (const uint16_t dst_uid, const InstClass instr_class, const std::vector<uint16_t> src_uid_vector, const std::vector<uint64_t> src_reg_value_vector) {
            RS[dst_uid].valid = true;
            RS[dst_uid].opcode = instr_class;
            for (int i = 0; i < src_uid_vector.size(); i++) {
                RS[dst_uid].src_info[i].valid = true;
                RS[dst_uid].src_info[i].src_uid = src_uid_vector[i];
                RS[dst_uid].src_info[i].value = src_reg_value_vector[i];
            }
            RS[dst_uid].any_update = false;
        }

        // Wrapper around above function, to support a simpler syntax for single source dependencies.
        void add_entry (const uint16_t dst_uid, const InstClass instr_class, const uint16_t src_uid, const uint64_t src_value) {
            std::vector<uint16_t> src_uid_vector = { src_uid };
            std::vector<uint64_t> src_value_vector = { src_value };
            add_entry (dst_uid, instr_class, src_uid_vector, src_value_vector);
        }

        void printState() {
            std::cout << "Reservation Station:\n";
            for (int i = 0; i < num_entries; i++) {
                if (RS[i].valid) {
                    std::cout << "Opcode: " << std::dec << cInfo[static_cast<uint8_t>(RS[i].opcode)]
                                << " | any_update: " << RS[i].any_update;
                    for (Source_Field src: RS[i].src_info) {
                        if (src.valid)
                            std::cout << " || src_uid: " << src.src_uid
                                        << " | src_value: 0x" << std::hex << src.value;
                    }
                    std::cout << "\n";
                }
            }
        }
};

class Prediction_Table
{
        PredictionTable_Entry* prediction_table;
        //std::unordered_map<uint64_t, PredictionTable_Entry> prediction_table;
        uint16_t num_entries;   // max number of hard-to-predict branches which can be pre-computed
        uint16_t wr_ptr; // write pointer in the circular buffer
    public:
        Prediction_Table (uint16_t num_entries) {
            this->num_entries = num_entries;
            wr_ptr = 0;
            
            prediction_table = new PredictionTable_Entry[num_entries];

            for (int i = 0; i < num_entries; i++) {
                prediction_table[i].valid = false;
            }
        }

        ~Prediction_Table() {
            if (prediction_table)
                delete[] prediction_table;
        }

        bool is_learnt(uint64_t pc) {
            // Check if an entry already exists.
            for (int i = 0; i < num_entries; i++) {
                auto entry = prediction_table[i];
                if (entry.valid && entry.pc == pc)
                    return true;
            }
            return false;
        }

        void add_entry(uint64_t pc, uint16_t src_uid) {
            // Check if an entry already exists. If it does then invalidate it
            for (int i = 0; i < num_entries; i++) {
                auto entry = prediction_table[i];
                if (entry.valid && entry.pc == pc)
                    entry.valid = false;
            }

            prediction_table[wr_ptr].pc = pc;
            prediction_table[wr_ptr].valid = 1;
            prediction_table[wr_ptr].src_uid = src_uid;

            wr_ptr++;
            wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
        }

        void printState() {
            std::cout << "Prediction Table:\n";
            for (int i = 0; i < num_entries; i++) {
                if (prediction_table[i].valid)
                    std::cout << "Br. PC: 0x" << std::hex << prediction_table[i].pc
                                << " | Src. uid: " << std::dec << prediction_table[i].src_uid
                                << " | Pred: " << prediction_table[i].prediction << "\n";
            }
        }
};

class Producer_Consumer_Pairs_Memory
{
        ProducerConsumer_Entry_Memory* ProdCons_Table;
        uint16_t num_entries;
        uint16_t wr_ptr;

        uint64_t hash(const uint64_t ip_num) {
            uint64_t index_width = static_cast<int>(std::log2(num_entries));
            uint64_t mask = (1 << index_width) - 1;

            uint64_t remaining_num = ip_num >> 3; // Ignore the lower 3 bits
            uint64_t hashed_num = 0;
            while (remaining_num != 0) {
                hashed_num = hashed_num ^ (remaining_num & mask);
                remaining_num = remaining_num >> index_width;
            }

            return hashed_num;
        }

    public:
        Producer_Consumer_Pairs_Memory (uint16_t num_entries) {
            this->num_entries = num_entries;
            ProdCons_Table = new ProducerConsumer_Entry_Memory[num_entries];
            wr_ptr = 0;
            
            // Mark all entries as invalid
            for (int i = 0; i < num_entries; i++) {
                ProdCons_Table[i].prodCons_info.valid_pair = false;
            }
        }

        ~Producer_Consumer_Pairs_Memory () {
            if (ProdCons_Table)
                delete[] ProdCons_Table;
        }

        bool is_tracked (uint64_t virt_addr) {
            for (int i = 0; i < num_entries; i++) {
                if (ProdCons_Table[i].virtual_address == hash(virt_addr))
                    return true;
            }
            return false;
        }

        void add_memVA (uint64_t virt_addr, uint64_t consumer_pc) {
            ProdCons_Table[wr_ptr].virtual_address = hash(virt_addr);
            ProdCons_Table[wr_ptr].prodCons_info.consumer_pc = consumer_pc;
            ProdCons_Table[wr_ptr].prodCons_info.valid_pair = false;

            wr_ptr++;
            wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
        }

        void record_producer(uint64_t virt_addr, uint64_t producer_pc) {
            for (int i = 0; i < num_entries; i++) {
                if (ProdCons_Table[i].virtual_address == hash(virt_addr)){
                    ProdCons_Table[i].prodCons_info.producer_pc = producer_pc;
                    ProdCons_Table[i].prodCons_info.valid_pair = false;
                    break;  // Each memory address can have atmost one entry
                }
            }
        }

        void record_consumer(uint64_t virt_addr, uint64_t consumer_pc) {
            for (int i = 0; i < num_entries; i++) {
                if (ProdCons_Table[i].virtual_address == hash(virt_addr)) {
                    ProdCons_Table[i].prodCons_info.consumer_pc = consumer_pc;
                    ProdCons_Table[i].prodCons_info.valid_pair = true;
                    break;  // Each memory address can have atmost one entry
                }
            }
        }

        uint64_t get_producerPC (uint64_t virt_addr) {
            for (int i = 0; i < num_entries; i++) {
                if (ProdCons_Table[i].virtual_address == hash(virt_addr) && ProdCons_Table[i].prodCons_info.valid_pair) {
                    return ProdCons_Table[i].prodCons_info.producer_pc;
                }
            }
            return 0xFFFFFFFFFFFFFFFF; // this should never be returned becuase the function should only be called after verifying that the virt_addr is_tracked.
        }

        void printState (uint64_t virt_addr) {
            std::cout << "Producer Consumer Table - Memory:\n";

            bool is_tracked = false;
            for (int i = 0; i < num_entries; i++) {
                if (ProdCons_Table[i].virtual_address == hash(virt_addr)) {
                    std::cout << "VA: 0x" << std::hex << virt_addr
                            << " | hash: 0x" << hash(virt_addr)
                            << " | Prod. PC: 0x" << ProdCons_Table[i].prodCons_info.producer_pc
                            << " | Cons. PC: 0x" << ProdCons_Table[i].prodCons_info.consumer_pc
                            << " | Valid: " << std::dec << ProdCons_Table[i].prodCons_info.valid_pair << "\n";
                    is_tracked = true;
                    break;  // Each memory address can have atmost one entry
                }
            }

            if (!is_tracked) {
                std::cout << "\t Not tracking VA: " << std::hex << virt_addr
                        << " | hash: 0x" << hash(virt_addr) << "\n";
            }
        }
};

class Producer_Consumer_Pairs_Register
{
        ProducerConsumer_Entry* ProdCons_Table;
    public:
        Producer_Consumer_Pairs_Register (uint16_t num_entries) {
            ProdCons_Table = new ProducerConsumer_Entry[num_entries];

            // Mark all entries as invalid
            for (int i = 0; i < num_entries; i++) {
                ProdCons_Table[i].valid_pair = false;
            }
        }

        ~Producer_Consumer_Pairs_Register () {
            if (ProdCons_Table)
                delete[] ProdCons_Table;
        }

        uint64_t get_producerPC (uint8_t src_reg) {
            return ProdCons_Table[src_reg].producer_pc;
        }
        
        void record_producer(uint8_t src_reg, uint64_t pc) {
            ProdCons_Table[src_reg].producer_pc = pc;
            ProdCons_Table[src_reg].valid_pair = false;
        }

        void record_consumer(uint8_t dst_reg, uint64_t pc) {
            ProdCons_Table[dst_reg].consumer_pc = pc;
            ProdCons_Table[dst_reg].valid_pair = true;
        }

        void printState(uint8_t reg) {
            std::cout << "r" << std::dec << unsigned(reg)
                    << " | Producer - 0x" << std::hex << ProdCons_Table[reg].producer_pc
                    << " | Consumer - 0x" << std::hex << ProdCons_Table[reg].consumer_pc
                    << " | Valid - " << ProdCons_Table[reg].valid_pair << "\n";
        }
};

class PC_2_uid_map
{
        std::unordered_map<uint64_t, uint16_t> uid;
        uint16_t num_entries;   // max number of pc->uid mappings which can be tracked
        uint16_t next_uid;  // uid given to next new pc
    public:
        PC_2_uid_map (uint16_t num_entries) {
            this->num_entries = num_entries;
        }

        uint16_t get_uid(uint64_t pc) {
            if (uid.count(pc)) {
                return uid[pc];
            } else { // Allocate a uid to this unknown pc
                // Allocation is like a circular buffer. Overwrite oldest allocations once full
                uint16_t new_uid = next_uid;
                uid.insert({pc, new_uid});
                next_uid = (next_uid + 1) >= num_entries ? 0 : (next_uid + 1);

                return new_uid;
            }
        }
};

class PC_UID_Map
{
        // Implemented as a circular buffer
        PC_UID_Map_Entry* PC_UID_Map_Table;
        uint16_t num_entries; // Max number of pc <--> uid mappings which can be tracked
        uint16_t next_uid;  // uid given to next new pc
    public:
        PC_UID_Map (uint16_t num_entries) {
            this->num_entries = num_entries;

            PC_UID_Map_Table = new PC_UID_Map_Entry[num_entries];

            // Mark all entries as invalid
            for (int i = 0; i < num_entries; i++) {
                PC_UID_Map_Table[i].valid = false;
            }

            next_uid = 0;
        }

        ~PC_UID_Map () {
            if (PC_UID_Map_Table)
                delete[] PC_UID_Map_Table;
        }

        uint16_t get_uid(uint64_t pc) {
            uint16_t uid;
            bool mapping_exists = false;

            /* Do an associative lookup
                There can only be one uid for every pc
            */
            for (uid = 0; uid < num_entries; uid++) {
                auto entry = PC_UID_Map_Table[uid];

                if (entry.valid && entry.pc == pc) {
                    mapping_exists = true;
                    break;
                }
            }

            if (mapping_exists) {
                return uid;
            } else {  // Create an entry
                uid = next_uid;
                PC_UID_Map_Table[uid].pc = pc;
                PC_UID_Map_Table[uid].valid = true;

                // Circular buffer
                next_uid = (next_uid + 1) == num_entries ? 0 : (next_uid + 1);

                return uid;
            }
        }
};

class Seeker_Buffer {
        std::unordered_set<uint64_t> seeker_buffer;
    public:
        void insert(uint64_t elem) {
            seeker_buffer.insert(elem);
        }

        void remove(uint64_t elem) {
            seeker_buffer.erase(elem);
        }

        bool exists(uint64_t elem) {
            return (seeker_buffer.count(elem) != 0);
        }

        void printState() {
            std::cout << "Seeker Buffer State:\n";
            for (uint64_t pc : seeker_buffer) {
                std::cout << "0x" << std::hex << std::uppercase << pc << std::endl;
            }
        }
};

class SampleCondPredictor
{
        SampleHist active_hist;
        std::unordered_map<uint64_t/*key*/, SampleHist/*val*/> pred_time_histories;
    public:

        SampleCondPredictor (void)
        {
        }

        void setup()
        {
        }

        void terminate()
        {
        }

        // sample function to get unique instruction id
        uint64_t get_unique_inst_id(uint64_t seq_no, uint8_t piece) const
        {
            assert(piece < 16);
            return (seq_no << 4) | (piece & 0x000F);
        }

        bool predict (uint64_t seq_no, uint8_t piece, uint64_t PC, const bool tage_pred)
        {
            active_hist.tage_pred = tage_pred;
            // checkpoint current hist
            pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist);
            const bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true/*pred_time_predict*/);
            return pred_taken;
        }

        bool predict_using_given_hist (uint64_t seq_no, uint8_t piece, uint64_t PC, const SampleHist& hist_to_use, const bool pred_time_predict)
        {
            return hist_to_use.tage_pred;
        }

        void history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC)
        {
            active_hist.ghist = active_hist.ghist << 1;
            if(taken)
            {
                active_hist.ghist |= 1;
            }
        }

        void update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC)
        {
            const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
            const auto& pred_time_history = pred_time_histories.at(pred_hist_key);
            update(PC, resolveDir, predDir, nextPC, pred_time_history);
            pred_time_histories.erase(pred_hist_key);
        }

        void update (uint64_t PC, bool resolveDir, bool pred_taken, uint64_t nextPC, const SampleHist& hist_to_use)
        {
        }
};
// =================
// Predictor End
// =================

#endif
static SampleCondPredictor cond_predictor_impl;

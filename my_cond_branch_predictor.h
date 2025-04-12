#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>

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

struct ProducerConsumer_Entry
{
    uint64_t producer_pc;
    uint64_t consumer_pc;
    bool valid_pair;
};

class Producer_Consumer_Pairs
{
        ProducerConsumer_Entry* ProdCons_Table;
        uint16_t num_entries;   // max number of pc->uid mappings which can be tracked
    public:
        Producer_Consumer_Pairs (uint16_t num_entries) {
            ProdCons_Table = new ProducerConsumer_Entry[num_entries];

            // Mark all entries as invalid
            for (int i = 0; i < num_entries; i++) {
                ProdCons_Table[i].valid_pair = false;
            }
        }

        ~Producer_Consumer_Pairs () {
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
            //printf("r%d | Producer - 0x%x | Consumer - 0x%x | Valid - %d", reg, ProdCons_Table[reg].producer_pc, ProdCons_Table[reg].consumer_pc, ProdCons_Table[reg].valid_pair);
            std::cout << "r" << unsigned(reg)
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

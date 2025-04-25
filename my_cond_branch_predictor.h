#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#define GHR_SIZE 8  // 8-bit GHR (256 possible values)
#define PHT_SIZE (1 << GHR_SIZE)  // 256 PHT rows
#define NUM_CHAINS 4  // 4 possible dependency chains
#define MAX_COUNTER 3  // Max value for counters (2-bit saturating counter)

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

//Structure for branch table entry 

struct BranchTableEntry
{
  uint64_t tag;
  uint64_t src_reg;
  uint64_t sat_counter;
  bool override_tage_pred;
  uint64_t st_table_index;
  uint64_t dependence_chains[4];
  uint64_t valid_chains;
  bool direction_matched;    // true if the if_zero == predict_taken 
  //For variant 2
  uint32_t ghr = 0;  // Global History Register (shift register)
  int pht[PHT_SIZE][NUM_CHAINS];
  int actual_chain; // This points to the dependence chain actually used for prediction useful for updating.
  //For TBZ TBNZ bit prediction 
   uint64_t prev_value;     // Previous value tested
   bool prev_taken;         // Previous branch outcome
   uint64_t branch_bit_mask; // Mask with 1 at the branch bit
   int bit_position;
};

struct RetireOp
{
  uint64_t pc;
  ExecuteInfo exec_info;
};

//Structure for store table entry

struct StoreTableEntry
{
  bool is_valid;
  bool is_zero;
  uint64_t pc;
  bool predict_taken;   //Vineeth to do, move this to branch entry as well
//  bool direction_matched; // true if the if_zero == predict_taken Rohan : direction_matched should be a feature of branch not store table as multiple branches might point to same store, but a particular branch will have fixed direction. 
  bool link_made;
  uint64_t value;
};

//Structure for trigger table

struct StoreChainEntry 
{
  uint64_t tag;
  bool is_valid;
  bool predict_taken;
//  bool direction_matched;
  uint64_t value;
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

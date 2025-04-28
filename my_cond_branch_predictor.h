#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>

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
  uint64_t tag;
  uint64_t src_reg;
  uint64_t sat_ctr;
  bool override_tage_pred;
  uint64_t store_triggers[32];
  uint64_t num_triggers;
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
  uint64_t branch_bit_mask;
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

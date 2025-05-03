/*
  Copyright (C) ARM Limited 2008-2025  All rights reserved.

  All rights reserved.

  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// This file provides a sample predictor integration based on the interface provided.

#include "lib/sim_common_structs.h"
#include "cbp2016_tage_sc_l.h"
#include "my_cond_branch_predictor.h"
#include <cassert>
#include <unordered_set>
#include <deque>
#include <cstdlib>

#include "global_variables.h"

#define DEBUG_FLAG false
#define LV_DEBUG_FLAG false
#define PERFECT_ADDR_PRED true
#define STUPID_VALUE 8898
#define SUPPORT_ALU_OPS false
#define ALU_OVERRIDE false
#define INC_VAL 8
// Branch Table Info
BranchTableEntry branch_table[BT_SIZE];
int get_branch_table_idx(uint64_t bpc)
{
  int idx = -1;
  uint64_t branch_table_tag = ((bpc >> 2) & BT_TAG_MASK);
  for (int i = 0; i < BT_SIZE; i++)
  {
    if (branch_table[i].valid && (branch_table[i].tag == branch_table_tag))
    {
      return i;
    }
  }
  return idx;
}

// Branch Learning Table Info
BranchLearningEntry branch_learning_table[BLT_SIZE];

// Store Table Info
StoreTableEntry store_table[ST_SIZE];

// Store Chain Info
TriggerTableEntry trigger_table[TT_SIZE];

// Prediction Table Info
PredictionTableEntry prediction_table[PT_SIZE];

// Value Predictor Load Table
LoadTableEntry load_table[LDT_SIZE];
int get_load_table_idx(uint64_t lpc)
{
  int idx = -1;
  uint64_t load_table_tag = ((lpc >> 2) & LDT_TAG_MASK);
  for (int i = 0; i < BT_SIZE; i++)
  {
    if (load_table[i].state != INVALID && (load_table[i].tag == load_table_tag))
    {
      return i;
    }
  }
  return idx;
}
std::unordered_map<uint64_t, SpeculativeInfo> speculation_map;
LinkTable link_table[LKT_SIZE];

// RetireOp Queue
std::deque<RetireOp> retire_op_queue;

// Register File
uint64_t RegFile[66];

void beginLoadAddrPredictor()
{
  // initial load_table setup
  for (int i = 0; i < LDT_SIZE; i++)
  {
    load_table[i].tag = 0;
    load_table[i].br_pc = 0;
    load_table[i].last_addr = 0;
    load_table[i].stride = 0;
    load_table[i].state = INVALID;
    load_table[i].inflight_loads = 0;
    load_table[i].addr_history_reg = 0;
    load_table[i].spec_addr_history_reg = 0;
    load_table[i].confidence_ctr = 0;
  }

  for (int i = 0; i < LKT_SIZE; i++)
  {
    link_table[i].address = 0;
  }
}

//
// beginCondDirPredictor()
//
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//

void beginCondDirPredictor()
{
  srand(time(NULL));
  // setup sample_predictor
  cbp2016_tage_sc_l.setup();
  cond_predictor_impl.setup();
  // initial branch_table setup
  for (int i = 0; i < BT_SIZE; i++)
  {
    branch_table[i].sat_ctr = 0;
    branch_table[i].tag = 0;
    for (int k = 0; k < 16; k++)
    {
      branch_table[i].src_flag[k] = 0;
    }
    branch_table[i].is_linked = false;
    branch_table[i].valid = false;
    branch_table[i].br_type = NA;
    branch_table[i].predicted_load_addr = 0;
    branch_table[i].prev_value = STUPID_VALUE;
    branch_table[i].prev_taken = false;
    branch_table[i].branch_bit_mask = UINT64_MAX;
    branch_table[i].bit_position_matters = false;
    branch_table[i].direction_zero_match = true;
    branch_table[i].bit_flag = false;
    branch_table[i].flag_br = false;
    branch_table[i].is_alu = false;
    for (int l = 0; l < 6; l++)
    {
      branch_table[i].alu_result_entries[l].value = 0;
      branch_table[i].alu_result_entries[l].valid = 0;
    }
    branch_table[i].alu_type = EQ;
    branch_table[i].threshold = 0;
    branch_table[i].override_alu = false;
    branch_table[i].override_tage = false;
  }

  // initial branch_learning_table setup
  for (int i = 0; i < BLT_SIZE; i++)
  {
    branch_learning_table[i].tag = 0;
    branch_learning_table[i].sat_ctr = 0;
  }

  // initial store_table setup
  for (int i = 0; i < ST_SIZE; i++)
  {
    store_table[i].tag = 0;
    store_table[i].pc = 0;
  }

  // initial trigger_table setup
  for (int i = 0; i < TT_SIZE; i++)
  {
    trigger_table[i].tag = 0;
    trigger_table[i].br_type = CBZ;
    trigger_table[i].branch_bit_mask = UINT64_MAX;
    trigger_table[i].flag_br = 0;
    for (int k = 0; k < 16; k++)
    {
      trigger_table[i].src_flag[k] = 0;
    }
    trigger_table[i].is_alu = false;
    trigger_table[i].alu_type = EQ;
    trigger_table[i].threshold = 0;
    trigger_table[i].override_alu = false;
  }

  // initial prediction_table setup
  for (int i = 0; i < PT_SIZE; i++)
  {
    prediction_table[i].valid = false;
    prediction_table[i].taken = false;
  }

  beginLoadAddrPredictor();

  // initial retire_op_queue setup
  retire_op_queue.clear();

  // initial RegFile setp
  for (int i = 0; i < 66; i++)
  {
    RegFile[i] = 0;
  }
}

void predictLoadAddr(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle, uint64_t oracle_load_addr)
{
  uint64_t predicted_addr = 0xdeadbeef;
  uint64_t load_table_idx = get_load_table_idx(pc);

  if (load_table_idx == -1)
    return;
  uint64_t branch_pc = load_table[load_table_idx].br_pc;
  uint64_t branch_table_idx = get_branch_table_idx(branch_pc);
  if (branch_table_idx == -1)
    return;
  assert(branch_table_idx != -1);
  if (load_table_idx != -1)
  {
    if (PERFECT_ADDR_PRED)
    {
      predicted_addr = oracle_load_addr;
      branch_table[branch_table_idx].predicted_load_addr = predicted_addr;
      return;
    }
    if (load_table[load_table_idx].state == VALID_STRIDE)
    {
      predicted_addr = load_table[load_table_idx].last_addr + load_table[load_table_idx].stride;
      branch_table[branch_table_idx].predicted_load_addr = predicted_addr;
      speculation_map[seq_no] = {seq_no, pc, predicted_addr, true};
      load_table[load_table_idx].last_addr = predicted_addr;
      load_table[load_table_idx].inflight_loads += 1;

      if (LV_DEBUG_FLAG)
      {
        std::cout << "LAP Stride Predicting: Sequence Number: " << seq_no
                  << " | Load PC: 0x" << std::hex << pc << std::dec
                  << " | Predicted Addr: 0x" << std::hex << predicted_addr << std::dec
                  << " | Branch PC: 0x" << std::hex << load_table[load_table_idx].br_pc << std::dec
                  << " | Inflight Loads: " << load_table[load_table_idx].inflight_loads
                  << std::endl;
      }
    }
    else if (load_table[load_table_idx].state == VALID_CORRELATION)
    {
      uint64_t current_history_reg = load_table[load_table_idx].spec_addr_history_reg;
      predicted_addr = link_table[current_history_reg].address;
      branch_table[branch_table_idx].predicted_load_addr = predicted_addr;
      load_table[load_table_idx].inflight_loads += 1;
      if (LV_DEBUG_FLAG)
      {
        std::cout << "LAP Correlation Predicting: Sequence Number: " << seq_no
                  << " | Load PC: 0x" << std::hex << pc << std::dec
                  << " | History Reg: 0x" << std::hex << current_history_reg << std::dec
                  << " | Predicted Addr: 0x" << std::hex << predicted_addr << std::dec
                  << " | Branch PC: 0x" << std::hex << load_table[load_table_idx].br_pc << std::dec
                  << " | Inflight Loads: " << load_table[load_table_idx].inflight_loads
                  << std::endl;
      }
      // update spec history reg
      uint64_t trimmed_address = (predicted_addr >> 2);
      load_table[load_table_idx].spec_addr_history_reg = ((load_table[load_table_idx].spec_addr_history_reg << LAP_SHIFT_BITS) ^ trimmed_address) & LAP_HISTORY_MASK;
    }
    else if (load_table[load_table_idx].state == TRAINING)
    {
      branch_table[branch_table_idx].predicted_load_addr = predicted_addr;
      if (LV_DEBUG_FLAG)
      {
        std::cout << "LAP Training: Sequence Number: " << seq_no
                  << " | Load PC: 0x" << std::hex << pc << std::dec
                  << " | Branch PC: 0x" << std::hex << load_table[load_table_idx].br_pc << std::dec
                  << std::endl;
      }
    }
  }
}

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
//
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and fetch_cycle are also provided as inputs
//
void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle, uint64_t load_addr)
{
  predictLoadAddr(seq_no, piece, pc, fetch_cycle, load_addr);
}

//
// get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
//
// This function is called by the simulator for predicting conditional branches.
// input values are unique identifying ids(seq_no, piece) and PC of the branch.
// return value is the predicted direction.
//
bool get_cond_dir_prediction(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t pred_cycle)
{
  const bool tage_sc_l_pred = cbp2016_tage_sc_l.predict(seq_no, piece, pc);
  bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);

  uint64_t branch_table_idx = get_branch_table_idx(pc);
  if ((branch_table_idx != -1) && branch_table[branch_table_idx].is_linked)
  {
    if (DEBUG_FLAG)
    {
      std::cout << "--------------------------------------------------" << std::endl;
      std::cout << "Branch Predicting: Sequence Number: " << seq_no
                << " | PC: 0x" << std::hex << pc << std::dec
                << " | Branch Type: " << branch_table[branch_table_idx].br_type
                << std::endl;
    }

    // our custom predictor
    uint64_t pred_load_addr = branch_table[branch_table_idx].predicted_load_addr;
    if (DEBUG_FLAG)
    {
      std::cout << "Branch Predicting: Predicted Load Address: 0x" << std::hex << pred_load_addr << std::dec
                << std::endl;
    }
    uint64_t addr_index = (pred_load_addr >> 2) & PT_MASK;
    if (prediction_table[addr_index].valid && branch_table[branch_table_idx].override_tage)
    {
      my_prediction = prediction_table[addr_index].taken;
      if (DEBUG_FLAG)
      {
        std::cout << "Branch Predicting with custom predictor!";
        std::cout << " | Predicted Addr: 0x" << std::hex << pred_load_addr << std::dec
                  << " | Custom Prediction: " << my_prediction << std::endl;
      }
    }
    else
    {
      if (DEBUG_FLAG)
      {
        std::cout << "Branch Predicting with TAGE!";
      }
    }
  }
  return my_prediction;
}

//
// spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc)
//
// This function is called by the simulator for updating the history vectors and any state that needs to be updated speculatively.
// The function is called for all the branches (not just conditional branches). To faciliate accurate history updates, spec_update is called right
// after a prediction is made.
// input values are unique identifying ids(seq_no, piece), PC of the instruction, instruction class, predicted/resolve direction and the next_pc
//
uint64_t mispred_for_target = 0;
void spec_update(uint64_t seq_no, uint8_t piece, uint64_t pc, InstClass inst_class, const bool resolve_dir, const bool pred_dir, const uint64_t next_pc)
{
  assert(is_br(inst_class));
  int br_type = 0;
  switch (inst_class)
  {
  case InstClass::condBranchInstClass:
    br_type = 1;
    break;
  case InstClass::uncondDirectBranchInstClass:
    br_type = 0;
    break;
  case InstClass::uncondIndirectBranchInstClass:
    br_type = 2;
    break;
  case InstClass::callDirectInstClass:
    br_type = 0;
    break;
  case InstClass::callIndirectInstClass:
    br_type = 2;
    break;
  case InstClass::ReturnInstClass:
    br_type = 2;
    break;
  default:
    assert(false);
  }

  if (inst_class == InstClass::condBranchInstClass)
  {
    cbp2016_tage_sc_l.history_update(seq_no, piece, pc, br_type, pred_dir, resolve_dir, next_pc);
    cond_predictor_impl.history_update(seq_no, piece, pc, resolve_dir, next_pc);
  }
  else
  {
    cbp2016_tage_sc_l.TrackOtherInst(pc, br_type, pred_dir, resolve_dir, next_pc);
  }
}

//
// notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle)
//
// This function is called when any instructions(not just branches) gets decoded.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage decode information
void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo &_decode_info, const uint64_t decode_cycle)
{
}

void updateLoadPredictor(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo &_decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
{
  // updates load predictor table when addr is ready
  uint64_t load_table_idx = get_load_table_idx(pc);
  if (load_table_idx != -1)
  {
    if (load_table[load_table_idx].state == VALID_STRIDE)
    {
      if (speculation_map[seq_no].valid)
      {
        uint64_t spec_pc = speculation_map[seq_no].pc;
        uint64_t spec_load_table_idx = get_load_table_idx(spec_pc);
        assert(spec_load_table_idx != -1);
        if (load_table[spec_load_table_idx].inflight_loads != 0)
        {
          load_table[spec_load_table_idx].inflight_loads -= 1;
        }
        // check if the predicted address is correct
        if (speculation_map[seq_no].predicted_addr != mem_va)
        {
          if (spec_load_table_idx != -1)
          {

            load_table[spec_load_table_idx].last_addr = mem_va + (load_table[spec_load_table_idx].stride * load_table[spec_load_table_idx].inflight_loads);
            uint64_t branch_pc = load_table[spec_load_table_idx].br_pc;
            uint64_t branch_table_idx = get_branch_table_idx(branch_pc);
            assert(branch_table_idx != -1);
            branch_table[branch_table_idx].predicted_load_addr = load_table[spec_load_table_idx].last_addr + load_table[spec_load_table_idx].stride;
            if (LV_DEBUG_FLAG)
            {
              std::cout << "LAP Resteer: Sequence Number: " << seq_no
                        << " | PC: 0x" << std::hex << spec_pc << std::dec
                        << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                        << " | Inflight Loads: " << load_table[spec_load_table_idx].inflight_loads
                        << " | Resteered Addr: 0x" << std::hex << load_table[spec_load_table_idx].last_addr << std::dec
                        << " | Branch PC: 0x" << std::hex << branch_pc << std::dec
                        << std::endl;
            }
          }
        }
        speculation_map[seq_no].valid = false;
      }
    }
    else if (load_table[load_table_idx].state == VALID_CORRELATION)
    {
      if (load_table[load_table_idx].inflight_loads != 0)
      {
        load_table[load_table_idx].inflight_loads -= 1;
      }
      // correlation predictor
      uint64_t current_history_reg = load_table[load_table_idx].addr_history_reg;
      link_table[current_history_reg].address = mem_va;
      if (LV_DEBUG_FLAG)
      {
        std::cout << "LAP Correlation Updating: Sequence Number: " << seq_no
                  << " | PC: 0x" << std::hex << pc << std::dec
                  << " | History Reg: 0x" << std::hex << current_history_reg << std::dec
                  << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                  << " | Inflight Loads: " << load_table[load_table_idx].inflight_loads
                  << std::endl;
      }

      uint64_t trimmed_address = (mem_va >> 2);
      load_table[load_table_idx].addr_history_reg = ((load_table[load_table_idx].addr_history_reg << LAP_SHIFT_BITS) ^ trimmed_address) & LAP_HISTORY_MASK;
      if (load_table[load_table_idx].inflight_loads == 0)
        load_table[load_table_idx].spec_addr_history_reg = load_table[load_table_idx].addr_history_reg;
    }
    else if (load_table[load_table_idx].state == TRAINING)
    {
      // correlation predictor
      uint64_t current_history_reg = load_table[load_table_idx].addr_history_reg;
      link_table[current_history_reg].address = mem_va;
      uint64_t trimmed_address = (mem_va >> 2);
      load_table[load_table_idx].addr_history_reg = ((load_table[load_table_idx].addr_history_reg << LAP_SHIFT_BITS) ^ trimmed_address) & LAP_HISTORY_MASK;
      if (LV_DEBUG_FLAG)
      {
        std::cout << "LAP Correlation Training: Sequence Number: " << seq_no
                  << " | PC: 0x" << std::hex << pc << std::dec
                  << " | History Reg: 0x" << std::hex << current_history_reg << std::dec
                  << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                  << std::endl;
      }

      // stride predictor
      if (load_table[load_table_idx].stride == -1)
      {
        load_table[load_table_idx].stride = mem_va - load_table[load_table_idx].last_addr;
        load_table[load_table_idx].last_addr = mem_va;
        load_table[load_table_idx].confidence_ctr = 0;
        if (LV_DEBUG_FLAG)
        {
          std::cout << "LAP Stride Training: Sequence Number: " << seq_no
                    << " | PC: 0x" << std::hex << pc << std::dec
                    << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                    << " | Stride: 0x" << std::hex << load_table[load_table_idx].stride << std::dec
                    << " | Confidence: " << (int)load_table[load_table_idx].confidence_ctr
                    << std::endl;
        }
      }
      else
      {
        uint64_t stored_stride = load_table[load_table_idx].stride;
        uint64_t current_stride = mem_va - load_table[load_table_idx].last_addr;
        if (current_stride == stored_stride)
        {
          load_table[load_table_idx].confidence_ctr += 1;
        }
        else
        {
          // if you fail to find stride once, reset the confidence + stride and switch to correlation
          load_table[load_table_idx].confidence_ctr = 0;
          load_table[load_table_idx].stride = -1;
          load_table[load_table_idx].state = VALID_CORRELATION;
          if (LV_DEBUG_FLAG)
          {
            std::cout << "LAP Correlation Training Complete: Sequence Number: " << seq_no
                      << " | PC: 0x" << std::hex << pc << std::dec
                      << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                      << std::endl;
          }
          return;
        }
        load_table[load_table_idx].last_addr = mem_va;
        if (load_table[load_table_idx].confidence_ctr == LAP_STRIDE_MAX_CONFIDENCE)
        {
          load_table[load_table_idx].state = VALID_STRIDE;
          uint64_t branch_pc = load_table[load_table_idx].br_pc;
          uint64_t branch_table_idx = get_branch_table_idx(branch_pc);
          branch_table[branch_table_idx].predicted_load_addr = mem_va + load_table[load_table_idx].stride;
          if (LV_DEBUG_FLAG)
          {
            std::cout << "LAP Stride Training Complete: Sequence Number: " << seq_no
                      << " | PC: 0x" << std::hex << pc << std::dec
                      << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                      << " | Stride: 0x" << std::hex << load_table[load_table_idx].stride << std::dec
                      << " | Confidence: " << (int)load_table[load_table_idx].confidence_ctr
                      << std::endl;
          }
        }
        else
        {
          if (LV_DEBUG_FLAG)
          {
            std::cout << "LAP Stride Training: Sequence Number: " << seq_no
                      << " | PC: 0x" << std::hex << pc << std::dec
                      << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                      << " | Stride: 0x" << std::hex << load_table[load_table_idx].stride << std::dec
                      << " | Confidence: " << (int)load_table[load_table_idx].confidence_ctr
                      << std::endl;
          }
        }
      }
    }
  }
}

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
//
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo &_decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
{
  updateLoadPredictor(seq_no, piece, pc, _decode_info, mem_va, mem_sz, agen_cycle);
}

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
//
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For conditional branches, we use this information to update the predictor.
// At the moment, we do not consider updating any other structure, but the contestants are allowed to  update any other predictor state.
void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo &_exec_info, const uint64_t execute_cycle)
{
  DecodeInfo _decode_info = _exec_info.dec_info;

  if (_decode_info.insn_class == InstClass::storeInstClass)
  {
    uint64_t src_reg_data_idx = 0;
    if (_decode_info.src_reg_info.size() == 3)
    {
      src_reg_data_idx = _decode_info.src_reg_info[2];
    }
    else if (_decode_info.src_reg_info.size() == 2)
    {
      src_reg_data_idx = _decode_info.src_reg_info[1];
    }
    else
    {
      assert(_decode_info.src_reg_info.size());
    }

    uint64_t dest_val = RegFile[src_reg_data_idx];
    uint64_t store_addr = _exec_info.mem_va.value();
    uint64_t store_pc = pc;

    uint64_t pred_table_idx;
    uint64_t trigger_table_idx = (store_pc >> 2) & TT_MASK;
    uint64_t trigger_table_tag = ((store_pc >> 2) & TT_TAG_MASK) >> TT_BITS;
    // check if in trigger table
    if (trigger_table[trigger_table_idx].tag == trigger_table_tag)
    {
      // check if the value is in the trigger table

      // if in the trigger table, update the prediction table
      // prediction_table[pred_table_idx].tag = store_addr_tag;
      // std::cout << " the trigger table prediction is \n" << trigger_table[store_pc_index].br_type << "\n";
      //
      // std::cout << "before prediction store \n";
      if (trigger_table[trigger_table_idx].is_alu && !(trigger_table[trigger_table_idx].override_alu))
      {
        if (trigger_table[trigger_table_idx].alu_type == EQ)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold == dest_val) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold == dest_val) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold == dest_val) ? 1 : 0;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (trigger_table[trigger_table_idx].alu_type == ENQ)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold != dest_val) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold != dest_val) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (trigger_table[trigger_table_idx].threshold != dest_val) ? 1 : 0;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (trigger_table[trigger_table_idx].alu_type == AT)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = 1;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = 1;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = 1;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (trigger_table[trigger_table_idx].alu_type == ANT)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = 0;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (trigger_table[trigger_table_idx].alu_type == HQ)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (dest_val >= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (dest_val >= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val >= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (trigger_table[trigger_table_idx].alu_type == LQ)
        {
          if (_exec_info.mem_sz.value() == 16)
          {
            for (uint64_t i = 0; i < 16; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (dest_val <= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else if (_exec_info.mem_sz.value() == 8)
          {
            for (uint64_t i = 0; i < 8; i += 4)
            {
              pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
              prediction_table[pred_table_idx].taken = (dest_val <= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
              prediction_table[pred_table_idx].valid = true;
            }
          }
          else
          {
            pred_table_idx = (store_addr >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val <= trigger_table[trigger_table_idx].threshold) ? 1 : 0;
            prediction_table[pred_table_idx].valid = true;
          }
        }
      }
      else if (trigger_table[trigger_table_idx].flag_br)
      {
        // std::cout << "before dest_val \n";
        if (dest_val >= 16)
        {
          // std::cout << "ERROR ERROR flag values going above 16\n";
          dest_val = 15;
          // prediction_table[store_addr_index].taken = trigger_table[store_pc_index].src_flag[dest_module_val];
        }
        if (dest_val < 0)
        {
          // std::cout << "ERROR ERROR flag values going above 16\n";
          dest_val = 0;
          // prediction_table[store_addr_index].taken = trigger_table[store_pc_index].src_flag[dest_module_val];
        }
        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = trigger_table[trigger_table_idx].src_flag[dest_val];
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = trigger_table[trigger_table_idx].src_flag[dest_val];
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].taken = trigger_table[trigger_table_idx].src_flag[dest_val];
          prediction_table[pred_table_idx].valid = true;
        }
        // std::cout << "Triggered Value Prediction Map: "
        //           << "Sequence Number: " << seq_no << std::hex
        //           << " | PC 0x" << pc << " | Store Value: 0x" << dest_val << std::dec
        //           << " | Prediction: " << prediction_table[store_addr_index].taken << std::endl;
        //   std::cout << " weird table prediction is \n" << prediction_table[store_addr_index].taken << " For value " << dest_val << "\n";
      }
      else if (trigger_table[trigger_table_idx].br_type == CBZ)
      {
        // std::cout << "before cbz \n";
        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val == 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val == 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].taken = (dest_val == 0) ? true : false;
          prediction_table[pred_table_idx].valid = true;
        }
        // std::cout << " the trigger table prediction cbz is \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if (trigger_table[trigger_table_idx].br_type == CBNZ)
      {
        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val != 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = (dest_val != 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].taken = (dest_val != 0) ? true : false;
          prediction_table[pred_table_idx].valid = true;
        }
        // std::cout << " the trigger table prediction is cbnz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if (trigger_table[trigger_table_idx].br_type == TBZ)
      {
        // std::cout << "before tbz \n";

        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) == 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) == 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) == 0) ? true : false;
          prediction_table[pred_table_idx].valid = true;
        }
        // std::cout << " the trigger table prediction is tbz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if (trigger_table[trigger_table_idx].br_type == TBNZ)
      {
        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) != 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) != 0) ? true : false;
            prediction_table[pred_table_idx].valid = true;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].taken = ((dest_val & trigger_table[trigger_table_idx].branch_bit_mask) != 0) ? true : false;
          prediction_table[pred_table_idx].valid = true;
        }
        // std::cout << " the trigger table prediction is tbnz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if (trigger_table[trigger_table_idx].br_type == NA)
      {
        // std::cout << "before NA \n";

        if (_exec_info.mem_sz.value() == 16)
        {
          for (uint64_t i = 0; i < 16; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].valid = false;
          }
        }
        else if (_exec_info.mem_sz.value() == 8)
        {
          for (uint64_t i = 0; i < 8; i += 4)
          {
            pred_table_idx = ((store_addr + i) >> 2) & PT_MASK;
            prediction_table[pred_table_idx].valid = false;
          }
        }
        else
        {
          pred_table_idx = (store_addr >> 2) & PT_MASK;
          prediction_table[pred_table_idx].valid = false;
        }
        // std::cout <<"printing tag, value, addr, br_type and branch_bit_mask respectively" << trigger_table[store_pc_index].tag << "\n" << trigger_table[store_pc_index].value << "\n" << trigger_table[store_pc_index].addr << "\n" << trigger_table[store_pc_index].br_type << "\n" << trigger_table[store_pc_index].branch_bit_mask << "\n";

        // std::cout << "PROBLEM PROBLEM trigger table setting even without br type set!!!!!\n";
        // exit(0);
      }
      // std::cout << "after prediction set \n";

      if (DEBUG_FLAG)
      {
        std::cout << "Trigger Store: Sequence Number: " << seq_no;
        std::cout << " | PC 0x:" << std::hex << pc << " | Value: 0x" << std::hex << dest_val << std::dec
                  << " | Addr: 0x" << std::hex << _exec_info.mem_va.value() << std::dec
                  << " | BranchType: " << trigger_table[trigger_table_idx].br_type
                  << " | Stored Prediction: " << prediction_table[pred_table_idx].taken << std::endl;
      }
    }

    uint64_t store_table_addr_idx;
    if (_exec_info.mem_sz.value() == 16)
    {
      // if(_exec_info.mem_va.value() == 0xffffef6b9000) {
      //   std::cout << "IMPORTANT: PC 0x: " << std::hex << store_pc << " | store addr is 0xffffef6b9000" << std::dec << std::endl;
      // }
      // std::cout << "before prediction store table mask \n";
      for (uint64_t i = 0; i < 16; i += 4)
      {
        store_table_addr_idx = ((store_addr + i) >> 2) & ST_MASK;
        store_table[store_table_addr_idx].tag = (((store_addr + i) >> 2) & ST_TAG_MASK) >> ST_BITS;
        store_table[store_table_addr_idx].pc = store_pc;
      }
    }
    else if (_exec_info.mem_sz.value() == 8)
    {
      for (int i = 0; i < 8; i += 4)
      {
        store_table_addr_idx = ((store_addr + i) >> 2) & ST_MASK;
        store_table[store_table_addr_idx].tag = (((store_addr + i) >> 2) & ST_TAG_MASK) >> ST_BITS;
        store_table[store_table_addr_idx].pc = store_pc;
      }
    }
    else
    {
      store_table_addr_idx = (store_addr >> 2) & ST_MASK;
      store_table[store_table_addr_idx].tag = ((store_addr >> 2) & ST_TAG_MASK) >> ST_BITS;
      store_table[store_table_addr_idx].pc = store_pc;
    }
  }
  // std::cout << "before is branch \n";
  const bool is_branch = is_br(_exec_info.dec_info.insn_class);
  if (is_branch)
  {
    if (is_cond_br(_exec_info.dec_info.insn_class))
    {
      const bool _resolve_dir = _exec_info.taken.value();
      const uint64_t _next_pc = _exec_info.next_pc;
      cbp2016_tage_sc_l.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
      // std::cout << "tage called \n";
      cond_predictor_impl.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
      // std::cout << "cond_predicted called \n";
      uint64_t branch_table_idx = get_branch_table_idx(pc);
      // std::cout << "before inbetween \n";
      if (branch_table_idx != -1)
      {
        if (branch_table[branch_table_idx].is_linked)
        {
          if (pred_dir == _resolve_dir)
          {
            // branch_table[branch_table_idx].correct_counter += 1;
          }
          else
          {
            // branch_table[branch_table_idx].incorrect_counter += 1;
            //  if (pc_index == 33084 || 54540 || 12080 || 11912)
          }
        }
      }
    }
    else
    {
      // std::cout << "before assertion \n";
      assert(pred_dir);
    }
  }
}

void get_branch_bit_direction(uint16_t branch_table_idx, uint64_t dest_reg_val, const bool _resolve_dir)
{
  if (branch_table[branch_table_idx].br_type > 5)
  {
    // std::cout << "ERROR ERROR " << dest_reg_val << "\n";
    //  exit(0);
  }
  // std::cout << "branch bit drection started for pc_index:" << pc_index <<" val : "<< dest_reg_val << "dir: "<< _resolve_dir << " \n";
  if (branch_table[branch_table_idx].bit_position_matters == false)
  {

    if (dest_reg_val == 0)
    {
      // std::cout << "under 0  check for  pc_index:" << pc_index << "br_type: " << branch_table[pc_index].br_type <<"\n";
      if (branch_table[branch_table_idx].br_type == 0)
      {
        branch_table[branch_table_idx].br_type = _resolve_dir ? CBZ : CBNZ;
        //      std::cout << "under 0 na  check for  pc_index:" << pc_index << "new br set" << branch_table[pc_index].br_type << "\n";
      }
      else if (branch_table[branch_table_idx].br_type == 1)
      {
        if (_resolve_dir)
        {
          // std::cout << "branch type cbz found \n";
          return; // same prediction
        }
        else
        {
          branch_table[branch_table_idx].br_type = CBNZ;
          branch_table[branch_table_idx].bit_position_matters = true;
          // std::cout << "under zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
        }
      }
      else if (branch_table[branch_table_idx].br_type == 2)
      {
        if (!(_resolve_dir))
        {
          // std::cout << "branch type cbnz found \n";
          return; // same prediction
        }
        else
        {
          branch_table[branch_table_idx].br_type = CBZ;
          branch_table[branch_table_idx].bit_position_matters = true;
          // std::cout << "under zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
        }
      }
    }
    else
    {
      // std::cout << "under non-zero  check for  pc_index:" << pc_index << "br_type: " << branch_table[pc_index].br_type <<"\n";
      if (branch_table[branch_table_idx].br_type == 0)
      {

        branch_table[branch_table_idx].br_type = _resolve_dir ? CBNZ : CBZ;
        //       std::cout << "under non-zero with na check for  pc_index:" << pc_index << "new br type " << branch_table[pc_index].br_type << "\n";
      }
      else if (branch_table[branch_table_idx].br_type == 2)
      {
        //    std::cout << "entered cbnz check!!! \n";
        if (_resolve_dir)
        {
          //	      std::cout << " branch type cbnz found \n";
          return; // same prediction
        }
        else
        {
          branch_table[branch_table_idx].br_type = CBZ;
          branch_table[branch_table_idx].bit_position_matters = true;
          //	      std::cout << "under non-zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
        }
      }
      else if (branch_table[branch_table_idx].br_type == 1)
      {
        //    std::cout << "entered cbz check!!! \n";
        if (!(_resolve_dir))
        {
          //          std::cout << " branch type cbz found \n";
          return; // same prediction
        }
        else
        {
          branch_table[branch_table_idx].br_type = CBNZ;
          branch_table[branch_table_idx].bit_position_matters = true;
          //        std::cout << "under non-zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
        }
      }
    }
  }
  if (branch_table[branch_table_idx].bit_position_matters == true)
  {
    //	 std::cout << "Have entered tbz test for branch pc_index " << pc_index << "branch bit mask is" << branch_table[pc_index].branch_bit_mask << "\n";
    if (branch_table[branch_table_idx].prev_value != STUPID_VALUE)
    {
      uint64_t changed_bits = branch_table[branch_table_idx].prev_value ^ dest_reg_val;
      uint64_t unchanged_bits = ~changed_bits;
      uint64_t branch_bit_mask = (_resolve_dir == branch_table[branch_table_idx].prev_taken) ? unchanged_bits : changed_bits;
      //		std::cout << "Branch bit mask this cycle is " << branch_bit_mask << " branch_table[pc_index].prev_taken " << branch_table[pc_index].prev_taken << " _resolve_dir " << _resolve_dir << " unchanged_bits" << unchanged_bits << " changed_bits " << changed_bits;
      uint64_t prev_branch_bit_mask = branch_table[branch_table_idx].branch_bit_mask;
      branch_table[branch_table_idx].branch_bit_mask &= branch_bit_mask;
      //		std::cout << "previous value: " << branch_table[pc _index].prev_value << " this time value : " << dest_reg_val << " changed_bits: " << changed_bits << "unchanged_bits" << unchanged_bits << " branch_bit_mask " << branch_bit_mask << "branch_table[pc_index].branch_bit_mask" << branch_table[pc_index].branch_bit_mask << "\n";
      bool prev_direction = branch_table[branch_table_idx].direction_zero_match;
      branch_table[branch_table_idx].direction_zero_match = !(((dest_reg_val & branch_table[branch_table_idx].branch_bit_mask) == 0) ^ _resolve_dir);
      bool prediction = !(((dest_reg_val & branch_table[branch_table_idx].branch_bit_mask) == 0) ^ branch_table[branch_table_idx].direction_zero_match);
      if (prev_direction != branch_table[branch_table_idx].direction_zero_match && branch_table[branch_table_idx].branch_bit_mask == prev_branch_bit_mask && prediction != _resolve_dir)
      {
        //			std::cout << "have entered fatal phase prediction is " << prediction << " resolve dir is " <<  _resolve_dir << "prev direction " << prev_direction << " this direction " << branch_table[pc_index].direction_zero_match << "\n";
        if (!(branch_table[branch_table_idx].bit_flag))
        {
          branch_bit_mask = branch_table[branch_table_idx].direction_zero_match ? dest_reg_val : branch_table[branch_table_idx].prev_value;
          branch_table[branch_table_idx].branch_bit_mask &= branch_bit_mask;
        }
        else
        {
          branch_bit_mask = branch_table[branch_table_idx].direction_zero_match ? branch_table[branch_table_idx].prev_value : dest_reg_val;
          branch_table[branch_table_idx].branch_bit_mask &= branch_bit_mask;
        }
      }
    }
    branch_table[branch_table_idx].prev_value = dest_reg_val;
    branch_table[branch_table_idx].prev_taken = _resolve_dir;

    uint64_t mask = branch_table[branch_table_idx].branch_bit_mask;
    int bit_count = 0;
    for (int i = 0; i < 64; i++)
    {
      if (mask & (1ULL << i))
        bit_count++;
    }
    if (bit_count > 0)
    {
      branch_table[branch_table_idx].direction_zero_match = !(((dest_reg_val & branch_table[branch_table_idx].branch_bit_mask) == 0) ^ _resolve_dir);

      if (bit_count == 1)
      {
        // std::cout << " TB direction found its " << branch_table[pc_index].direction_zero_match << "hope this helps \n";
      }
      if (branch_table[branch_table_idx].direction_zero_match)
      {
        branch_table[branch_table_idx].br_type = TBZ;
        // std::cout << " TB direction set to  " << branch_table[pc_index].direction_zero_match << " br type is now " << branch_table[pc_index].br_type << "\n";
      }
      else
      {
        branch_table[branch_table_idx].br_type = TBNZ;
        // std::cout << " TB direction set to  " << branch_table[pc_index].direction_zero_match << " br type is now " << branch_table[pc_index].br_type << "\n";
      }
    }
    else
    {
      if (!(branch_table[branch_table_idx].bit_flag))
      {
        branch_table[branch_table_idx].bit_flag = true;
        branch_table[branch_table_idx].branch_bit_mask = UINT64_MAX;
      }
      else
      {
        //  std::cout << " FATAL DEBUG final mask : " << branch_table[pc_index].branch_bit_mask  << "branch_table[pc_index].bit_flag " << branch_table[pc_index].bit_flag << " bit_count " << bit_count << "\n";
        // std::cout << "NOTE NOTE SOMETHING WENT WRONG!!!!! Branch type could not be determined!!!\n" << std::endl;
      }
    }
  }
}

void learn_src_branch_behaivour(uint16_t branch_table_idx, uint64_t dest_reg_val, const bool _resolve_dir)
{
  if (dest_reg_val >= 16)
  {
    // std::cout << "SOMETHING IS WRONG, this is not src branch, cant have value more than 16!!! \n";
    // exit(0);
  }

  if (_resolve_dir == 1)
  {
    branch_table[branch_table_idx].src_flag[dest_reg_val] = 1;
  }
}

uint64_t pack_r64(bool N, bool Z, bool C, bool V)
{
  return ((N << 3) | (Z << 2) | (C << 1) | V);
}

ALU_Operation reverse_engineer_aluOp(const uint64_t pc, const ExecuteInfo &exec_info)
{
  std::vector<uint64_t> src_reg_info = exec_info.dec_info.src_reg_info;
  ALU_Operation aluOp = UNKNOWN;
  int detected_op_count = 0;
  uint64_t dst_reg_value;
  std::vector<uint8_t> DEBUG_r64_possibilities;

  // Supporting only two source operands for now
  if (src_reg_info.size() != 2)
  {
    if (DEBUG_FLAG)
    {
      std::cout << "WARN:: reverse_engineer_aluOp(): src_reg_info.size() != 2\n";
    }
    return UNKNOWN;
  }

  int64_t op1 = RegFile[src_reg_info[0]];
  int64_t op2 = RegFile[src_reg_info[1]];
  int64_t alu_result;
  uint64_t r64 = RegFile[64];

  bool N = (r64 >> 0x3) & 0x1;
  bool Z = (r64 >> 0x2) & 0x1;
  bool C = (r64 >> 0x1) & 0x1;
  bool V = (r64 >> 0x0) & 0x1;

  // TST
  alu_result = op1 & op2;
  N = (alu_result < 0);
  Z = (alu_result == 0);
  dst_reg_value = pack_r64(N, Z, C, V);
  DEBUG_r64_possibilities.push_back(dst_reg_value);
  if (dst_reg_value == exec_info.dst_reg_value.value())
  {
    aluOp = TST;
    detected_op_count++;
  }

  // TEQ
  alu_result = op1 ^ op2;
  N = (alu_result < 0);
  Z = (alu_result == 0);
  dst_reg_value = pack_r64(N, Z, C, V);
  DEBUG_r64_possibilities.push_back(dst_reg_value);
  if (dst_reg_value == exec_info.dst_reg_value.value())
  {
    aluOp = TEQ;
    detected_op_count++;
  }

  // CMP
  alu_result = op1 - op2;
  N = (alu_result < 0);
  Z = (alu_result == 0);
  // C = (static_cast<uint64_t>(op1) - static_cast<uint64_t>(op2) > static_cast<uint64_t>(op1));
  // V = (alu_result > op1);
  dst_reg_value = pack_r64(N, Z, C, V);
  DEBUG_r64_possibilities.push_back(dst_reg_value);
  if (dst_reg_value == exec_info.dst_reg_value.value())
  {
    aluOp = CMP;
    detected_op_count++;
  }

  // CMN
  alu_result = op1 + op2;
  N = (alu_result < 0);
  Z = (alu_result == 0);
  // C = (static_cast<uint64_t>(op1) + static_cast<uint64_t>(op2) < static_cast<uint64_t>(op1));
  // V = (alu_result < op1);
  dst_reg_value = pack_r64(N, Z, C, V);
  DEBUG_r64_possibilities.push_back(dst_reg_value);
  if (dst_reg_value == exec_info.dst_reg_value.value())
  {
    aluOp = CMN;
    detected_op_count++;
  }

  if (DEBUG_FLAG)
  {
    std::cout << "\t\t\t TST | TEQ | CMP | CMN |\n";
    std::cout << "r64 values:  ";
    for (uint8_t r64_val : DEBUG_r64_possibilities)
    {
      std::cout << "0x" << std::hex << static_cast<int>(r64_val) << " | ";
    }
    std::cout << "\n";
    if (detected_op_count != 1)
      std::cout << "WARN:: reverse_engineer_aluOp(): Detected " << std::dec << detected_op_count << " possible operations. Couldn't identify unique alu operation.\n";
  }

  return aluOp;
}

void value_correlator(uint64_t pc_index, uint64_t load_val, const bool _resolve_dir, const bool pred_dir)
{

  int count = branch_table[pc_index].alu_result_entries[0].valid + branch_table[pc_index].alu_result_entries[1].valid + branch_table[pc_index].alu_result_entries[2].valid + branch_table[pc_index].alu_result_entries[3].valid;
  // std::cout << "pc index : " << pc_index << "count : " << count << "\n";
  // std::cout << "before count 0 \n";
  if (count == 0)
  {
    branch_table[pc_index].alu_type = (_resolve_dir) ? EQ : ENQ;
    if (_resolve_dir)
    {
      branch_table[pc_index].alu_result_entries[0].valid = 1;
      branch_table[pc_index].alu_result_entries[0].value = load_val;
      branch_table[pc_index].alu_result_entries[4].valid = 1;
      branch_table[pc_index].alu_result_entries[4].value = load_val;
      branch_table[pc_index].threshold = load_val;
    }
    else
    {
      branch_table[pc_index].alu_result_entries[2].valid = 1;
      branch_table[pc_index].alu_result_entries[2].value = load_val;
      branch_table[pc_index].alu_result_entries[5].valid = 1;
      branch_table[pc_index].alu_result_entries[5].value = load_val;
      branch_table[pc_index].threshold = load_val;
    }
  }
  // std::cout << "before count 1 \n";

  else if (count == 1)
  { // std::cout << "before count 1 \n";
    // we just bother assigning threshold and alu type here
    if (branch_table[pc_index].alu_result_entries[4].valid && _resolve_dir)
    {
      if (load_val != branch_table[pc_index].alu_result_entries[0].value)
      {
        branch_table[pc_index].alu_result_entries[1].value = load_val;
        branch_table[pc_index].alu_result_entries[1].valid = 1;
        branch_table[pc_index].alu_type = AT;
      }
    }
    else if (branch_table[pc_index].alu_result_entries[5].valid && !(_resolve_dir))
    {
      if (load_val != branch_table[pc_index].alu_result_entries[2].value)
      {
        branch_table[pc_index].alu_result_entries[3].value = load_val;
        branch_table[pc_index].alu_result_entries[3].valid = 1;
        branch_table[pc_index].alu_type = ANT;
      }
    }
    else if (branch_table[pc_index].alu_result_entries[4].valid && !(_resolve_dir))
    {
      branch_table[pc_index].alu_result_entries[2].valid = 1;
      branch_table[pc_index].alu_result_entries[2].value = load_val;
      branch_table[pc_index].alu_result_entries[5].valid = 1;
      branch_table[pc_index].alu_result_entries[5].value = load_val;
    }
    else if (branch_table[pc_index].alu_result_entries[5].valid && _resolve_dir)
    {
      branch_table[pc_index].alu_result_entries[0].valid = 1;
      branch_table[pc_index].alu_result_entries[0].value = load_val;
      branch_table[pc_index].alu_result_entries[4].valid = 1;
      branch_table[pc_index].alu_result_entries[4].value = load_val;
    }
  }
  // std::cout << "before count 2 \n";

  else if (count == 2)
  {
    // std::cout << "before count 2 \n";
    if (branch_table[pc_index].alu_result_entries[4].valid && branch_table[pc_index].alu_result_entries[5].valid) // case where there is one entry in both
    {
      if (_resolve_dir)
      {
        if (load_val != branch_table[pc_index].alu_result_entries[0].value)
        {
          branch_table[pc_index].alu_result_entries[1].value = load_val;
          branch_table[pc_index].alu_result_entries[1].valid = 1;
          branch_table[pc_index].alu_type = ENQ;
          branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[5].value;
        }
      }
      else
      {
        if (load_val != branch_table[pc_index].alu_result_entries[2].value)
        {
          branch_table[pc_index].alu_result_entries[3].value = load_val;
          branch_table[pc_index].alu_result_entries[3].valid = 1;
          branch_table[pc_index].alu_type = EQ;
          branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[4].value;
        }
      }
    }
    else if (branch_table[pc_index].alu_result_entries[4].valid && !(branch_table[pc_index].alu_result_entries[5].valid)) // case where both entries are in taken
    {
      if (_resolve_dir) // we ignore case where taken, since it isright now always taken
      {
      }
      else
      {
        branch_table[pc_index].alu_result_entries[2].valid = 1;
        branch_table[pc_index].alu_result_entries[2].value = load_val;
        branch_table[pc_index].alu_result_entries[5].valid = 1;
        branch_table[pc_index].alu_result_entries[5].value = load_val;
        branch_table[pc_index].alu_type = ENQ;
        branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[5].value;
      }
    }
    else if (!(branch_table[pc_index].alu_result_entries[4].valid) && branch_table[pc_index].alu_result_entries[5].valid) // case where both entries are in not taken
    {
      if (_resolve_dir)
      {
        branch_table[pc_index].alu_result_entries[0].valid = 1;
        branch_table[pc_index].alu_result_entries[0].value = load_val;
        branch_table[pc_index].alu_result_entries[4].valid = 1;
        branch_table[pc_index].alu_result_entries[4].value = load_val;
        branch_table[pc_index].alu_type = EQ;
        branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[4].value;
      }
    }
    else
    {
      // std::cout << "no condition held weird in count 2\n";
      branch_table[pc_index].override_alu = true;
      // exit(0);
    }
  }
  // std::cout << "before count 3 \n";

  else if (count == 3)
  {
    // std::cout << "before count 3 \n";
    if (pc_index == 58464 || pc_index == 33084 || pc_index == 56364)
    {
      // std::cout << "NOTE lets understand learning behaivour valid bits one after other" << branch_table[pc_index].alu_result_entries[0].valid << branch_table[pc_index].alu_result_entries[1].valid << branch_table[pc_index].alu_result_entries[2].valid << branch_table[pc_index].alu_result_entries[3].valid << branch_table[pc_index].alu_result_entries[4].valid << branch_table[pc_index].alu_result_entries[5].valid << "\n";
    }
    if (branch_table[pc_index].alu_result_entries[0].valid && branch_table[pc_index].alu_result_entries[1].valid) // 2 in taken, 1 in not taken
    {
      // if(pc_index ==11752) {std::cout << "non going mad print\n";}
      if (_resolve_dir) // we can keep predicting taken
      {
        if (pred_dir != _resolve_dir)
        {
          // if(pc_index ==11752) {std::cout << "mismatch is threshold and value " << branch_table[pc_index].threshold << " " << load_val << "\n";}
          branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[2].value;
          // if(pc_index ==11752) {std::cout << "updated threshold " << branch_table[pc_index].threshold << "\n";}
        }
      }
      else
      {
        if (load_val != branch_table[pc_index].alu_result_entries[2].value)
        {
          branch_table[pc_index].alu_result_entries[3].valid = 1;
          branch_table[pc_index].alu_result_entries[3].value = load_val;

          branch_table[pc_index].alu_type = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[2].value) ? HQ : LQ;
          if (pc_index == 11752)
          {
            // std::cout << "pc entered place to go to 4 count, should be assignned hq lq :" << branch_table[pc_index].alu_type << "\n";
          }
          if (branch_table[pc_index].alu_type == HQ)
          {
            branch_table[pc_index].alu_result_entries[4].value = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[1].value : branch_table[pc_index].alu_result_entries[0].value;
            branch_table[pc_index].alu_result_entries[5].value = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[2].value : branch_table[pc_index].alu_result_entries[3].value;
          }
          if (branch_table[pc_index].alu_type == LQ)
          {
            branch_table[pc_index].alu_result_entries[4].value = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[0].value : branch_table[pc_index].alu_result_entries[1].value;
            branch_table[pc_index].alu_result_entries[5].value = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[3].value : branch_table[pc_index].alu_result_entries[2].value;
          }
        }
        branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[4].value;
      }
    }
    else if (branch_table[pc_index].alu_result_entries[2].valid && branch_table[pc_index].alu_result_entries[3].valid)
    {
      if (_resolve_dir)
      {
        if (load_val != branch_table[pc_index].alu_result_entries[0].value)
        {
          branch_table[pc_index].alu_result_entries[1].valid = 1;
          branch_table[pc_index].alu_result_entries[1].value = load_val;

          branch_table[pc_index].alu_type = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[2].value) ? HQ : LQ;
          if (branch_table[pc_index].alu_type == HQ)
          {
            branch_table[pc_index].alu_result_entries[4].value = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[1].value : branch_table[pc_index].alu_result_entries[0].value;
            branch_table[pc_index].alu_result_entries[5].value = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[2].value : branch_table[pc_index].alu_result_entries[3].value;
          }
          if (branch_table[pc_index].alu_type == LQ)
          {
            branch_table[pc_index].alu_result_entries[4].value = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[0].value : branch_table[pc_index].alu_result_entries[1].value;
            branch_table[pc_index].alu_result_entries[5].value = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[3].value : branch_table[pc_index].alu_result_entries[2].value;
          }
        }
        branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[4].value;
      }
    }
    else
    {
      // std::cout << "something went wrong in count 3 not possible case\n";
      branch_table[pc_index].override_alu = true;
    }
  }
  // std::cout << "before count 4 \n";

  else if (count == 4)
  {
    // std::cout << "before count 4 \n";
    if (pc_index == 58464 || pc_index == 33084 || pc_index == 56364)
    {
      // std::cout << "the resolved and pred dir respectively are: " << _resolve_dir << " " << pred_dir << "the value is  " << load_val << " threshold is " << branch_table[pc_index].threshold << "\n";
    }

    if (_resolve_dir)
    {
      if (!(branch_table[pc_index].alu_result_entries[5].valid) || !(branch_table[pc_index].alu_result_entries[4].valid) || !(branch_table[pc_index].alu_result_entries[3].valid) || !(branch_table[pc_index].alu_result_entries[2].valid) || !(branch_table[pc_index].alu_result_entries[1].valid) || !(branch_table[pc_index].alu_result_entries[0].valid))
      {
        // std::cout << "something didnt get learnt!!!" << branch_table[pc_index].alu_result_entries[0].valid << branch_table[pc_index].alu_result_entries[1].valid << branch_table[pc_index].alu_result_entries[2].valid << branch_table[pc_index].alu_result_entries[3].valid << branch_table[pc_index].alu_result_entries[4].valid << branch_table[pc_index].alu_result_entries[5].valid << "\n";
        // exit(0);
      }

      uint64_t pivot = 0;
      if (branch_table[pc_index].alu_type == HQ)
      {
        pivot = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[1].value : branch_table[pc_index].alu_result_entries[0].value;

        if (load_val < pivot && load_val < branch_table[pc_index].alu_result_entries[4].value)
        {
          branch_table[pc_index].alu_result_entries[4].value = load_val;
          branch_table[pc_index].alu_result_entries[1].value = load_val;
        }
        else if (load_val > pivot && load_val < branch_table[pc_index].alu_result_entries[4].value)
        {
          // std::cout <<" something went wrong, a value is different from threshold for pc_index " << pc_index << "branch type is " << branch_table[pc_index].alu_type << " all four values are\n" << branch_table[pc_index].alu_result_entries[0].value << " " << branch_table[pc_index].alu_result_entries[1].value << " " << branch_table[pc_index].alu_result_entries[2].value << " " << branch_table[pc_index].alu_result_entries[3].value << " threshold is " << branch_table[pc_index].alu_result_entries[4].value << "\n";
          branch_table[pc_index].override_alu = true;
        }
      }
      else if (branch_table[pc_index].alu_type == LQ)
      {
        pivot = (branch_table[pc_index].alu_result_entries[0].value > branch_table[pc_index].alu_result_entries[1].value) ? branch_table[pc_index].alu_result_entries[0].value : branch_table[pc_index].alu_result_entries[1].value;

        if (load_val > pivot && load_val > branch_table[pc_index].alu_result_entries[4].value)
        {
          branch_table[pc_index].alu_result_entries[4].value = load_val;
          branch_table[pc_index].alu_result_entries[1].value = load_val;
        }
        else if (load_val < pivot && load_val > branch_table[pc_index].alu_result_entries[4].value)
        {
          branch_table[pc_index].override_alu = true;
          // std::cout <<" something went wrong, a value is different from threshold for pc_index " << pc_index << "branch type is " << branch_table[pc_index].alu_type << " all four values are\n" << branch_table[pc_index].alu_result_entries[0].value << " " << branch_table[pc_index].alu_result_entries[1].value << " " << branch_table[pc_index].alu_result_entries[2].value << " " << branch_table[pc_index].alu_result_entries[3].value << " threshold is " << branch_table[pc_index].alu_result_entries[4].value << "\n";
        }
      }
      else
      {
        // std::cout << "error in count 4, non lq hq branch\n";
        // exit(0);
        branch_table[pc_index].override_alu = true;
      }
    }
    else
    {
      if (!(branch_table[pc_index].alu_result_entries[5].valid) || !(branch_table[pc_index].alu_result_entries[4].valid) || !(branch_table[pc_index].alu_result_entries[3].valid) || !(branch_table[pc_index].alu_result_entries[2].valid) || !(branch_table[pc_index].alu_result_entries[1].valid) || !(branch_table[pc_index].alu_result_entries[0].valid))
      {
        // std::cout << "something didnt get learnt!!!" << branch_table[pc_index].alu_result_entries[0].valid << branch_table[pc_index].alu_result_entries[1].valid << branch_table[pc_index].alu_result_entries[2].valid << branch_table[pc_index].alu_result_entries[3].valid << branch_table[pc_index].alu_result_entries[4].valid << branch_table[pc_index].alu_result_entries[5].valid << "\n";
        branch_table[pc_index].override_alu = true;
      }

      uint64_t pivot2 = 0;
      if (branch_table[pc_index].alu_type == HQ)
      {
        pivot2 = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[2].value : branch_table[pc_index].alu_result_entries[3].value;

        if (load_val > pivot2 && load_val > branch_table[pc_index].alu_result_entries[5].value)
        {
          branch_table[pc_index].alu_result_entries[5].value = load_val;
          branch_table[pc_index].alu_result_entries[3].value = load_val;
        }
        else if (load_val < pivot2 && load_val > branch_table[pc_index].alu_result_entries[4].value)
        {
          branch_table[pc_index].override_alu = true;
          // std::cout <<" something went wrong, a value is different from threshold for pc_index " << pc_index << "branch type is " << branch_table[pc_index].alu_type << " all four values are\n" << branch_table[pc_index].alu_result_entries[0].value << " " << branch_table[pc_index].alu_result_entries[1].value << " " << branch_table[pc_index].alu_result_entries[2].value << " " << branch_table[pc_index].alu_result_entries[3].value << " threshold is " << branch_table[pc_index].alu_result_entries[4].value << "\n";
        }
      }
      else if (branch_table[pc_index].alu_type == LQ)
      {
        pivot2 = (branch_table[pc_index].alu_result_entries[2].value > branch_table[pc_index].alu_result_entries[3].value) ? branch_table[pc_index].alu_result_entries[3].value : branch_table[pc_index].alu_result_entries[2].value;

        if (load_val < pivot2 && load_val < branch_table[pc_index].alu_result_entries[5].value)
        {
          branch_table[pc_index].alu_result_entries[5].value = load_val;
          branch_table[pc_index].alu_result_entries[3].value = load_val;
        }
        else if (load_val > pivot2 && load_val < branch_table[pc_index].alu_result_entries[4].value)
        {
          branch_table[pc_index].override_alu = true;
          //  std::cout <<" something went wrong, a value is different from threshold for pc_index " << pc_index << "branch type is " << branch_table[pc_index].alu_type << " all four values are\n" << branch_table[pc_index].alu_result_entries[0].value << " " << branch_table[pc_index].alu_result_entries[1].value << " " << branch_table[pc_index].alu_result_entries[2].value << " " << branch_table[pc_index].alu_result_entries[3].value << " threshold is " << branch_table[pc_index].alu_result_entries[4].value << "\n";
        }
      }
      else
      {
        // std::cout << "error in count 4, non lq hq branch\n";
        branch_table[pc_index].override_alu = true;
      }
    }
    branch_table[pc_index].threshold = branch_table[pc_index].alu_result_entries[4].value;
  }
  else
  {
    branch_table[pc_index].override_alu = true;
  }
  // std::cout << "a count 4 \n";

  // if(pc_index == 11752)
  //{
  //	uint64_t tempo;
  //	if(branch_table[pc_index].alu_type == ENQ)
  //		tempo = branch_table[pc_index].alu_result_entries[1].value;
  //	else
  //		tempo = branch_table[pc_index].alu_result_entries[3].value;
  //	std::cout << "count: " << count << "threshold: " << branch_table[pc_index].threshold << " alu type " << branch_table[pc_index].alu_type << " val 1 " << branch_table[pc_index].alu_result_entries[0].value << " val 2 " << tempo  << " val 3 " << branch_table[pc_index].alu_result_entries[2].value << "\n";
  // }
}

//
// notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
//
// This function is called when any instructions(not just branches) gets committed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage commit information
uint64_t branch_inst_count = 0; // max to 1000
void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo &_exec_info, const uint64_t commit_cycle)
{
  if (is_cond_br(_exec_info.dec_info.insn_class))
  {
    const bool _resolve_dir = _exec_info.taken.value();

    uint64_t branch_table_tag = (pc >> 2) & BT_TAG_MASK;
    uint64_t learning_branch_table_idx = (pc >> 2) & BLT_MASK;
    uint64_t learning_branch_table_tag = ((pc >> 2) & BLT_TAG_MASK) >> BLT_BITS;

    // learn Hard To Predict branches
    if (_exec_info.dec_info.src_reg_info.size() > 0)
    {
      if (branch_learning_table[learning_branch_table_idx].tag == learning_branch_table_tag)
      {
        if (_resolve_dir != pred_dir)
        {
          if (branch_learning_table[learning_branch_table_idx].sat_ctr < BLT_SAT_COUNTER_MAX)
          {
            branch_learning_table[learning_branch_table_idx].sat_ctr += 1;
          }
        }
      }
      else
      {
        if (branch_learning_table[learning_branch_table_idx].sat_ctr == 0)
        {
          branch_learning_table[learning_branch_table_idx].tag = learning_branch_table_tag;
          branch_learning_table[learning_branch_table_idx].sat_ctr = 1;
        }
      }
    }

    int branch_table_idx = get_branch_table_idx(pc);
    // disable custom predictor if counter saturates
    if (branch_table_idx != -1)
    {
      if (_exec_info.dec_info.src_reg_info.size() > 0)
      {
        if (branch_table[branch_table_idx].is_linked)
        {
          if (_resolve_dir != pred_dir)
          {
            if (branch_table[branch_table_idx].sat_ctr < (BT_SAT_COUNTER_MAX - INC_VAL))
            {
              branch_table[branch_table_idx].sat_ctr += INC_VAL;
            }
          }
          else
          {
            if (branch_table[branch_table_idx].sat_ctr > 0)
            {
              branch_table[branch_table_idx].sat_ctr -= 1;
            }
          }

          if (branch_table[branch_table_idx].sat_ctr == BT_SAT_COUNTER_MAX)
          {
            branch_table[branch_table_idx].override_tage = false;
          }
        }
      }
    }

    // if highly mispredicted branch add to misprediction list
    if (branch_learning_table[learning_branch_table_idx].tag == learning_branch_table_tag && branch_learning_table[learning_branch_table_idx].sat_ctr == BLT_SAT_COUNTER_MAX)
    {
      // append full 64-bit pc to misprediction list
      // find next available entry in the branch table
      if (branch_table_idx == -1)
      {
        uint64_t new_branch_table_idx = -1;
        for (int i = 0; i < BT_SIZE; i++)
        {
          if (branch_table[i].valid == false)
          {
            new_branch_table_idx = i;
            break;
          }
        }
        // assert(new_branch_table_idx != -1);

        if (new_branch_table_idx != -1)
        {
          std::cout << "NOTE: Adding new entry to branch table for pc: 0x" << std::hex << pc << std::dec << " | at index: " << new_branch_table_idx << "\n";
          branch_table[new_branch_table_idx].tag = branch_table_tag;
          branch_table[new_branch_table_idx].sat_ctr = 0;
          branch_table[new_branch_table_idx].valid = true;
          branch_table[new_branch_table_idx].is_linked = false;
          branch_table[new_branch_table_idx].override_tage = false;
          branch_table[new_branch_table_idx].br_type = NA;
          branch_table[new_branch_table_idx].predicted_load_addr = 0xDEADBEAF;
        }
      }
    }

    branch_table_idx = get_branch_table_idx(pc);

    if (branch_table_idx != -1)
    {
      // append full 64-bit pc to misprediction list
      uint64_t dest_reg_val = RegFile[_exec_info.dec_info.src_reg_info[0]];
      // if(pc_index == 37824 )
      //{
      //	std::cout<< "src reg for weird branch!!" << _exec_info.dec_info.src_reg_info[0]<< "\n";
      // }
      if (_exec_info.dec_info.src_reg_info[0] != 64)
      {
        get_branch_bit_direction(branch_table_idx, dest_reg_val, _resolve_dir);
        // std::cout << " debug br type " << branch_table[pc_index].br_type;
      }
      else
      {
        learn_src_branch_behaivour(branch_table_idx, dest_reg_val, _resolve_dir);
        branch_table[branch_table_idx].flag_br = 1;
      }
      // learning branch direction and bit (TBZ vs CBZ)
      //  print the branch we are going to analyze
      //  std::cout << "---------------------------------------------------" << std::endl;
      //  std::cout << "Processing H2P Branch PC: 0x" << std::hex << pc << std::dec << " | " << _exec_info << std::endl;

      // loop thorugh last 8 entries in retire_op_queue and print out the instructions
      // std::cout << "Last 8 RetireOp Queue Entries:" << std::endl;
      uint64_t tracking_dep_reg_idx = _exec_info.dec_info.src_reg_info[0];
      bool found_immediate_inst_producing_branch_reg = false;
      bool found_load = false;
      RetireOp retire_op;
      for (int i = 0; i < retire_op_queue.size(); i++)
      {
        retire_op = retire_op_queue[i];
        // std::cout << "PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info << std::endl;
        if (!(retire_op.exec_info.dec_info.dst_reg_info.has_value()))
        {
          continue;
        }
        uint64_t dest_reg_idx = retire_op.exec_info.dec_info.dst_reg_info.value();
        // check if branch register is same as load producing register
        if (dest_reg_idx == tracking_dep_reg_idx)
        {
          // if the instruction is a load, we can stop
          if (retire_op.exec_info.dec_info.insn_class == InstClass::loadInstClass)
          {
            // std::cout << "Load PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info.dec_info << std::endl;
            found_load = true;
            // uint64_t load_addr = retire_op.exec_info.mem_va.value();
            // uint64_t load_value = retire_op.exec_info.dst_reg_value.value();
            break;
          }

          if (!SUPPORT_ALU_OPS)
            break; // disable support for ALU Operations

          if (found_immediate_inst_producing_branch_reg)
          {
            break;
          }

          // uint64_t dest_reg_val = retire_op.exec_info.dst_reg_value.value();
          // get_branch_bit_direction(pc_index, dest_reg_val, _resolve_dir);
          //  check if the instruction is a load
          //  std::cout << "Producer is not a load, skipping..." << std::endl;
          //  std::cout << "PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info << std::endl;
          //  std::cout << "\t";
          //  for (auto src_reg: retire_op.exec_info.dec_info.src_reg_info) {
          //    std::cout << " | r" << std::dec << src_reg << ": 0x" << std::hex << RegFile[src_reg];
          //  }
          //  std::cout << "\n";

          // ALU_Operation aluOp = reverse_engineer_aluOp(pc, retire_op.exec_info);

          if (retire_op.exec_info.dec_info.insn_class == InstClass::aluInstClass)
          {
            if (retire_op.exec_info.dec_info.src_reg_info.size() == 1)
            {
              // if the instruction is a single source operand instruction
              tracking_dep_reg_idx = retire_op.exec_info.dec_info.src_reg_info[0];
              // std::cout << "ALU PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info.dec_info << std::endl;
              found_immediate_inst_producing_branch_reg = true;
              branch_table[branch_table_idx].is_alu = true;
            }
            else
            {
              break;
            }
          }
          else
          {
            break;
          }

          // std::cout << "ALU_op = " << aluOp << "\n\n";

          // break;
        }
      }

      if (found_load)
      {
        // print out address
        uint64_t load_addr = retire_op.exec_info.mem_va.value();
        uint64_t load_value = retire_op.exec_info.dst_reg_value.value();
        // std::cout << "Load Address: " << std::hex << load_addr << std::dec << std::endl;
        if (branch_table[branch_table_idx].is_alu)
        {
          value_correlator(branch_table_idx, load_value, _resolve_dir, pred_dir);
        }
        // std::cout << "Training Value Prediction Map: "
        //           << "Sequence Number: " << seq_no << std::hex
        //           << " | PC 0x" << pc << " | Load Value: 0x" << load_value << std::dec
        //           << " | Prediction: " << _resolve_dir << std::endl;

        // check if address is in store table
        uint64_t store_table_addr_idx = (load_addr >> 2) & ST_MASK;
        uint64_t store_table_tag = ((load_addr >> 2) & ST_TAG_MASK) >> ST_BITS;
        if (store_table[store_table_addr_idx].tag == store_table_tag)
        {
          uint64_t store_pc = store_table[store_table_addr_idx].pc;
          uint64_t trigger_table_idx = (store_pc >> 2) & TT_MASK;
          uint64_t trigger_table_tag = ((store_pc >> 2) & TT_TAG_MASK) >> TT_BITS;

          bool store_trigger_found = false;
          if (trigger_table[trigger_table_idx].tag == trigger_table_tag)
          {
            // std::cout << "Already Exists for Address: " << std::hex << load_addr << std::dec << std::endl;
            // store exists already in trigger table
            store_trigger_found = true;
            trigger_table[trigger_table_idx].is_alu = branch_table[branch_table_idx].is_alu;
            trigger_table[trigger_table_idx].alu_type = branch_table[branch_table_idx].alu_type;
            trigger_table[trigger_table_idx].threshold = branch_table[branch_table_idx].threshold;
            trigger_table[trigger_table_idx].override_alu = branch_table[branch_table_idx].override_alu;
            if (branch_table[branch_table_idx].flag_br)
            {
              trigger_table[trigger_table_idx].flag_br = 1;
              // pc index " << store_pc_index << "setting flag br \n";
              // std::cout << "before known set\n";
              for (int k = 0; k < 16; k++)
              {
                trigger_table[trigger_table_idx].src_flag[k] = branch_table[branch_table_idx].src_flag[k];
              }
              // std::cout << "after known set\n";
            }
            else
            {
              trigger_table[trigger_table_idx].br_type = branch_table[branch_table_idx].br_type;
              trigger_table[trigger_table_idx].branch_bit_mask = branch_table[branch_table_idx].branch_bit_mask;
            }
          }

          // add load to the load table
          uint64_t load_pc = retire_op.pc;
          uint64_t load_table_idx = get_load_table_idx(load_pc);
          uint64_t load_table_tag = (load_pc >> 2) & LDT_TAG_MASK;
          if (load_table_idx == -1)
          {
            uint64_t new_load_table_idx = -1;
            for (int i = 0; i < LDT_SIZE; i++)
            {
              if (load_table[i].state == INVALID)
              {
                new_load_table_idx = i;
                break;
              }
            }

            // assert(new_load_table_idx != -1);
            if (new_load_table_idx != -1)
            {
              std::cout << "NOTE: Adding new entry to load table for pc: 0x" << std::hex << load_pc << std::dec << " | at index: " << new_load_table_idx << "\n";
              load_table[new_load_table_idx].tag = load_table_tag;
              load_table[new_load_table_idx].br_pc = pc;
              load_table[new_load_table_idx].last_addr = load_addr;
              load_table[new_load_table_idx].stride = -1;
              load_table[new_load_table_idx].state = TRAINING;
              if (LV_DEBUG_FLAG)
              {
                std::cout << "Load Table Entry Created: " << std::endl;
                std::cout << "Load PC: 0x" << std::hex << load_pc << std::dec << " | Load Addr: 0x" << std::hex << load_addr << std::dec
                          << " | BR PC: 0x" << std::hex << pc << std::dec << " | Stride: " << load_table[load_table_idx].stride << std::endl;
              }
            }
          }

          if (!store_trigger_found)
          {
            // add to the trigger table
            // add to the trigger table
            trigger_table[trigger_table_idx].tag = trigger_table_tag;
            trigger_table[trigger_table_idx].br_type = branch_table[branch_table_idx].br_type;
            trigger_table[trigger_table_idx].branch_bit_mask = branch_table[branch_table_idx].branch_bit_mask;
            trigger_table[trigger_table_idx].flag_br = branch_table[branch_table_idx].flag_br;
            trigger_table[trigger_table_idx].is_alu = branch_table[branch_table_idx].is_alu;
            trigger_table[trigger_table_idx].alu_type = branch_table[branch_table_idx].alu_type;
            trigger_table[trigger_table_idx].threshold = branch_table[branch_table_idx].threshold;
            trigger_table[trigger_table_idx].override_alu = branch_table[branch_table_idx].override_alu;
            // std::cout << "before unknown set\n";
            for (int k = 0; k < 16; k++)
            {
              trigger_table[trigger_table_idx].src_flag[k] = branch_table[branch_table_idx].src_flag[k];
            }

            // std::cout << "Making chain with PC: 0x" << std::hex << store_pc << std::dec << " --> Branch PC: 0x" << std::hex << pc << std::dec << std::endl;
            // std::cout << "before unknown set\n";
            // std::cout << "after  unknown set\n";
          }
          if (!branch_table[branch_table_idx].is_linked)
          {
            branch_table[branch_table_idx].sat_ctr = 0;
            branch_table[branch_table_idx].override_tage = true;
            branch_table[branch_table_idx].is_linked = true;
          }
        }
      }
    }

    // periodic reset of saturation counter
    if (branch_inst_count == 1000)
    {
      for (int i = 0; i < BLT_SIZE; i++)
      {
        if (branch_learning_table[i].sat_ctr < 15)
        {
          branch_learning_table[i].sat_ctr = 0;
        }
        else
        {
          branch_learning_table[i].sat_ctr -= 15;
        }
      }
      branch_inst_count = 0;
    }
    else
    {
      branch_inst_count++;
    }
  }

  // Update RetireOp Queue
  RetireOp retire_op;
  retire_op.pc = pc;
  retire_op.exec_info = _exec_info;

  if (retire_op_queue.size() == RETIRE_OP_QUEUE_SIZE)
  {
    retire_op_queue.pop_back();
  }

  retire_op_queue.push_front(retire_op);

  // Update RegFile
  if (_exec_info.dst_reg_value.has_value())
  {
    uint64_t dest_val = _exec_info.dst_reg_value.value();
    uint64_t dest_reg = _exec_info.dec_info.dst_reg_info.value();
    RegFile[dest_reg] = dest_val;
    if (dest_reg != 65)
    { // skip zero register
      RegFile[dest_reg] = dest_val;
    }
  }
}

//
// endCondDirPredictor()
//
// This function is called by the simulator at the end of simulation.
// It can be used by the contestant to print out other contestant-specific measurements.
//
void endCondDirPredictor()
{
  cbp2016_tage_sc_l.terminate();
  cond_predictor_impl.terminate();
}

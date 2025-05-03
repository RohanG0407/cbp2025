


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

#include "lib/my_addr_predictor.h"

#define DEBUG_FLAG false
#define LV_DEBUG_FLAG false
#define PERFECT_ADDR_PRED true
#define STUPID_VALUE 8898
//
// beginCondDirPredictor()
//
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//

// Branch Table Info
#define BT_SIZE 65535
#define BT_SAT_COUNTER_MAX 31
BranchTableEntry branch_table[BT_SIZE]; // 2^16 entries - 1
std::unordered_set<uint64_t> high_mispred_pc;

// Store Table Info
#define ST_SIZE 65535
StoreTableEntry store_table[ST_SIZE]; // 2^16 entries - 1

// Store Chain Info
#define SC_SIZE 65535
TriggerTableEntry trigger_table[SC_SIZE]; // 2^16 entries - 1

// Prediction Table Info
#define PT_SIZE 65535
PredictionTableEntry prediction_table[PT_SIZE]; // 2^16 entries - 1

// Value Predictor Load Table
#define LT_SIZE 65535
LoadTableEntry load_table[LT_SIZE]; // 2^16 entries - 1
std::unordered_map<uint64_t, SpeculativeInfo> speculation_map;

// RetireOp Queue
std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

uint64_t RegFile[66];
void beginCondDirPredictor()
{
    srand(time(NULL));
    // setup sample_predictor
    cbp2016_tage_sc_l.setup();
    cond_predictor_impl.setup();
    // initial branch_table setup
    for (int i = 0; i < BT_SIZE; i++) {
        branch_table[i].src_reg = 0;
        branch_table[i].sat_ctr = 0;
        branch_table[i].tag = 0;
        branch_table[i].override_tage_pred = false;
        for(int j = 0; j < 8; j++) {
            branch_table[i].store_triggers[j] = 0;
        }
        branch_table[i].num_triggers = 0;
        branch_table[i].is_linked = false;
        branch_table[i].br_type = NA;
        branch_table[i].predicted_load_addr = 0;
        branch_table[i].prev_value = STUPID_VALUE;
        branch_table[i].prev_taken = false;
        branch_table[i].branch_bit_mask = UINT64_MAX;
        branch_table[i].bit_position_matters = false;
        branch_table[i].direction_zero_match = true;
        branch_table[i].bit_flag = false;
    }

    // initial store_table setup
    for (int i = 0; i < SC_SIZE; i++) {
        store_table[i].tag = 0;
        store_table[i].pc = 0;
        store_table[i].value = 0;
    }

    // initial trigger_table setup
    for (int i = 0; i < ST_SIZE; i++) {
        trigger_table[i].tag = 0;
        trigger_table[i].value = 0;
        trigger_table[i].addr = 0;
        trigger_table[i].br_type = CBZ;
        trigger_table[i].branch_bit_mask = UINT64_MAX;
    }

    // initial prediction_table setup
    for (int i = 0; i < PT_SIZE; i++) {
        prediction_table[i].tag = 0;
        prediction_table[i].taken = false;
    }

    // initial load_table setup
    for (int i = 0; i < LT_SIZE; i++) {
      load_table[i].tag = 0;
      load_table[i].br_pc = 0;
      load_table[i].last_addr = 0;
      load_table[i].stride = 0;
      load_table[i].valid = false;
      load_table[i].inflight_loads = 0;
  }

    // initial retire_op_queue setup
    retire_op_queue.clear();

    // initial RegFile setp
    for (int i = 0; i < 66; i++) {
        RegFile[i] = 0;
    }
}

//
// notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
//
// This function is called when any instructions(not just branches) gets fetched.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction and fetch_cycle are also provided as inputs
//
void notify_instr_fetch(uint64_t seq_no, uint8_t piece, uint64_t pc, const uint64_t fetch_cycle)
{
  uint64_t predicted_addr = 0xdeadbeef;
  uint64_t load_pc_index = pc & 0xFFFF;
  uint64_t load_tag = (pc & 0xFFF0000) >> 16;
  if(load_table[load_pc_index].tag == load_tag && load_table[load_pc_index].valid) {
    if(PERFECT_ADDR_PRED) predicted_addr = load_addr;
    else predicted_addr = load_table[load_pc_index].last_addr + load_table[load_pc_index].stride;
    uint64_t branch_pc = load_table[load_pc_index].br_pc;
    uint64_t branch_pc_index = branch_pc & 0xFFFF;
    uint64_t branch_tag = (branch_pc & 0xFFF0000) >> 16;
    branch_table[branch_pc_index].predicted_load_addr = predicted_addr;
    speculation_map[seq_no] = { seq_no, pc, predicted_addr, true};
    load_table[load_pc_index].last_addr = predicted_addr;
    load_table[load_pc_index].inflight_loads += 1;

    if(LV_DEBUG_FLAG) {
      std::cout << "Address Predicting: Sequence Number: " << seq_no
                << " | PC: 0x" << std::hex << pc << std::dec
                << " | Predicted Addr: 0x" << std::hex << predicted_addr << std::dec
                << " | Load PC: 0x" << std::hex << load_table[load_pc_index].br_pc << std::dec
                << " | Inflight Loads: " << load_table[load_pc_index].inflight_loads
                << std::endl;
    }
  }
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
    const bool tage_sc_l_pred =  cbp2016_tage_sc_l.predict(seq_no, piece, pc);
    bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);

    uint64_t pc_index = pc & 0xFFFF;
    uint64_t tag = (pc & 0xFFF0000) >> 16;
    if(branch_table[pc_index].tag == tag && branch_table[pc_index].is_linked) {
      if(DEBUG_FLAG) {
        std::cout << "--------------------------------------------------" << std::endl;
        std::cout << "Branch Predicting: Sequence Number: " << seq_no
                  << " | PC: 0x" << std::hex << pc << std::dec
                  << " | Branch Type: " << branch_table[pc_index].br_type
                  << std::endl;
      }

      // our custom predictor
      uint64_t pred_load_addr = branch_table[pc_index].predicted_load_addr;
      if(DEBUG_FLAG) {
        std::cout << "Branch Predicting: Predicted Load Address: 0x" << std::hex << pred_load_addr << std::dec
                  << std::endl;
        }
      uint64_t addr_index = pred_load_addr & 0xFFFF;
      uint64_t addr_tag = (pred_load_addr & 0xFFF0000) >> 16;
      if(prediction_table[addr_index].tag == addr_tag) {
        my_prediction = prediction_table[addr_index].taken;
        //std::cout << "Branch Predicting with custom predictor!";
        //    std::cout << " | Predicted Addr: 0x" << std::hex << pred_load_addr << std::dec
        //              << " | Custom Prediction: " << my_prediction << "did position matter: "<< branch_table[pc_index].bit_position_matters << " branch type: " << branch_table[pc_index].br_type << std::endl;
        if(DEBUG_FLAG) {
          std::cout << "Branch Predicting with custom predictor!";
            std::cout << " | Predicted Addr: 0x" << std::hex << pred_load_addr << std::dec
                      << " | Custom Prediction: " << my_prediction << std::endl;
        }
      } else {
        if(DEBUG_FLAG) {
          std::cout << "Branch Predicting with TAGE!" << std::endl;
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
    switch(inst_class)
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

    if(inst_class == InstClass::condBranchInstClass)
    {
        cbp2016_tage_sc_l.history_update(seq_no, piece, pc, br_type, pred_dir, resolve_dir, next_pc);
        cond_predictor_impl.history_update(seq_no, piece, pc, resolve_dir, next_pc);

        uint64_t pc_index = pc & 0xFFFF;
        uint64_t tag = (pc & 0xFFF0000) >> 16;

        if(branch_table[pc_index].tag == tag && branch_table[pc_index].is_linked) {
          if(DEBUG_FLAG) {
            std::cout << "Branch Prediction Results: Sequence Number: " << seq_no
                      << " | Resolved Dir: " <<  resolve_dir
                      << " | Predicted Dir: " << pred_dir
                      << std::endl;
            std::cout << "--------------------------------------------------" << std::endl;
          }
        }
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
void notify_instr_decode(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t decode_cycle)
{
}

//
// notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
//
// This function is called when any load/store instructions complete agen.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, decode info, mem_va and mem_sz and agen_cycle are also provided as inputs
//
void notify_agen_complete(uint64_t seq_no, uint8_t piece, uint64_t pc, const DecodeInfo& _decode_info, const uint64_t mem_va, const uint64_t mem_sz, const uint64_t agen_cycle)
{

  if(pc == 0xfffff0d8f180 || pc == 0xfffff0d8f284) {
    uint64_t target_pc = pc;
    uint64_t load_addr = mem_va;
    uint64_t load_addr_index = load_addr & 0xFFFF;
    uint64_t load_addr_tag = (load_addr & 0xFFF0000) >> 16;
    if(prediction_table[load_addr_index].tag == load_addr_tag) {
      if(DEBUG_FLAG) {
        std::cout << "Target Load: Sequence Number: " << seq_no
          << " | PC: 0x" << std::hex << pc << std::dec
          << " | Address: 0x" << std::hex << load_addr << std::dec
          << " | Taken: " << prediction_table[load_addr_index].taken << std::dec << std::endl;
      }
    } else {
      if(DEBUG_FLAG) {
        std::cout << "Target Load: Sequence Number: " << seq_no
          << " | PC: 0x" << std::hex << pc << std::dec
          << " | Address: 0x" << std::hex << load_addr << std::dec
          << " | Taken: Unknown" << std::endl;
      }
    }
  }

  if(_decode_info.insn_class == InstClass::storeInstClass) {
    uint64_t src_reg_data_idx = 0;
    if(_decode_info.src_reg_info.size() == 3) {
      src_reg_data_idx = _decode_info.src_reg_info[2];
    } else if(_decode_info.src_reg_info.size() == 2){
      src_reg_data_idx = _decode_info.src_reg_info[1];
    } else {
      assert(_decode_info.src_reg_info.size());
    }
    uint64_t dest_val = RegFile[src_reg_data_idx];

    uint64_t store_addr = mem_va;
    uint64_t store_addr_index = store_addr & 0xFFFF;
    uint64_t store_addr_tag = (store_addr & 0xFFF0000) >> 16;

    uint64_t store_pc = pc;
    uint64_t store_pc_index = store_pc & 0xFFFF;
    uint64_t store_pc_tag = (store_pc & 0xFFF0000) >> 16;
    // check if in trigger table
    if(trigger_table[store_pc_index].tag == store_pc_tag) {
      // check if the value is in the trigger table
      trigger_table[store_pc_index].value = dest_val;
      trigger_table[store_pc_index].addr = store_addr;

      // if in the trigger table, update the prediction table
      prediction_table[store_addr_index].tag = store_addr_tag;
      std::cout << " the trigger table prediction is \n" << trigger_table[store_pc_index].br_type << "\n";
      if(trigger_table[store_pc_index].br_type == CBZ) {
        prediction_table[store_addr_index].taken = (dest_val == 0) ? true : false;
        std::cout << " the trigger table prediction cbz is \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if(trigger_table[store_pc_index].br_type == CBNZ) {
        prediction_table[store_addr_index].taken = (dest_val != 0) ? true : false;
        std::cout << " the trigger table prediction is cbnz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if(trigger_table[store_pc_index].br_type == TBZ) {
        prediction_table[store_addr_index].taken = ((dest_val & trigger_table[store_pc_index].branch_bit_mask) == 0) ? true : false;
        std::cout << " the trigger table prediction is tbz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if(trigger_table[store_pc_index].br_type == TBNZ) {
        prediction_table[store_addr_index].taken = ((dest_val & trigger_table[store_pc_index].branch_bit_mask) != 0) ? true : false;
        std::cout << " the trigger table prediction is tbnz \n" << trigger_table[store_pc_index].br_type << "  taken or not taken?" << prediction_table[store_addr_index].taken << "\n";
      }
      else if(trigger_table[store_pc_index].br_type == NA) {
        prediction_table[store_addr_index].taken = true;
        std::cout <<"printing tag, value, addr, br_type and branch_bit_mask respectively" << trigger_table[store_pc_index].tag << "\n" << trigger_table[store_pc_index].value << "\n" << trigger_table[store_pc_index].addr << "\n" << trigger_table[store_pc_index].br_type << "\n" << trigger_table[store_pc_index].branch_bit_mask << "\n";

        std::cout << "PROBLEM PROBLEM trigger table setting even without br type set!!!!!\n";
        //exit(0);
      }

      if(DEBUG_FLAG) {
        std::cout << "Trigger Store: Sequence Number: " << seq_no;
        std::cout << " | PC 0x:" << std::hex << pc  << " | Value: 0x" << std::hex << trigger_table[store_pc_index].value << std::dec
                  << " | Addr: 0x" << std::hex << trigger_table[store_pc_index].addr << std::dec
                  << " | BranchType: " << trigger_table[store_pc_index].br_type
                  << " | Stored Prediction: " << prediction_table[store_addr_index].taken << std::endl;
      }
    }


    if(store_table[store_addr_index].tag == store_addr_tag) {
      // check if the value is in the store table
      store_table[store_addr_index].pc = store_pc;
      store_table[store_addr_index].value = dest_val;
    } else {
      // add to the store table
      store_table[store_addr_index].tag = store_addr_tag;
      store_table[store_addr_index].pc = store_pc;
      store_table[store_addr_index].value = dest_val;
    }
  }

  // updates load predictor table when addr is ready
  if(speculation_map[seq_no].valid) {
    uint64_t pc = speculation_map[seq_no].pc;
    uint64_t pc_index = pc & 0xFFFF;
    uint64_t tag = (pc & 0xFFF0000) >> 16;
    if(load_table[pc_index].inflight_loads != 0) {
      load_table[pc_index].inflight_loads -= 1;
    }
    // check if the predicted address is correct
    if(speculation_map[seq_no].predicted_addr != mem_va) {
      if(load_table[pc_index].tag == tag) {

        load_table[pc_index].last_addr = mem_va + (load_table[pc_index].stride * load_table[pc_index].inflight_loads);
        uint64_t branch_pc = load_table[pc_index].br_pc;
        uint64_t branch_pc_index = branch_pc & 0xFFFF;
        uint64_t branch_tag = (branch_pc & 0xFFF0000) >> 16;
        branch_table[branch_pc_index].predicted_load_addr = load_table[pc_index].last_addr + load_table[pc_index].stride;
        if(LV_DEBUG_FLAG) {
          std::cout << "Address Predicting Resteer: Sequence Number: " << seq_no
                    << " | PC: 0x" << std::hex << pc << std::dec
                    << " | Actual Addr: 0x" << std::hex << mem_va << std::dec
                    << " | Inflight Loads: " << load_table[pc_index].inflight_loads
                    << " | Resteered Addr: 0x" << std::hex << load_table[pc_index].last_addr << std::dec
                    << " | Branch PC: 0x" << std::hex << branch_pc << std::dec
                    << std::endl;
        }
      }
    }
    speculation_map[seq_no].valid = false;
  }
}

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
//
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For conditional branches, we use this information to update the predictor.
// At the moment, we do not consider updating any other structure, but the contestants are allowed to  update any other predictor state.
long long int correct_counter = 0;
long long int incorrect_counter = 0;
void notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
{
    const bool is_branch = is_br(_exec_info.dec_info.insn_class);
    if(is_branch)
    {
        if (is_cond_br(_exec_info.dec_info.insn_class))
        {
            const bool _resolve_dir = _exec_info.taken.value();
            const uint64_t _next_pc = _exec_info.next_pc;
            cbp2016_tage_sc_l.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);
            cond_predictor_impl.update(seq_no, piece, pc, _resolve_dir, pred_dir, _next_pc);





            uint16_t pc_index = pc & 0xFFFF;
            uint64_t tag = (pc & 0xFFF0000) >> 16;
            if(tag == branch_table[pc_index].tag)
            {
                    if(branch_table[pc_index].is_linked)
                    {
                            if(pred_dir == _resolve_dir)
                            {
                                    branch_table[pc_index].correct_counter += 1;

                            }
                            else
                            {
                                    branch_table[pc_index].incorrect_counter +=1;
                        std::cout << " pc_index" << pc_index << " correct counter " << branch_table[pc_index].correct_counter << " incorrect counter " << branch_table[pc_index].incorrect_counter << "branch type " << branch_table[pc_index].br_type << "\n";
                            }
                    }
            }



        }
        else
        {
            assert(pred_dir);
        }
    }
}

//
// notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
//
// This function is called when any instructions(not just branches) gets committed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For the sample predictor implementation, we do not leverage commit information
void get_branch_bit_direction(uint16_t pc_index,  uint64_t dest_reg_val, const bool _resolve_dir)
{
      if(branch_table[pc_index].br_type > 5)
      {
        std::cout << "ERROR ERROR " << dest_reg_val << "\n";
        exit(0);
      }
      std::cout << "branch bit drection started for pc_index:" << pc_index <<" val : "<< dest_reg_val << "dir: "<< _resolve_dir << " \n";
      if(branch_table[pc_index].bit_position_matters == false)
      {

        if(dest_reg_val == 0)
        {
              std::cout << "under 0  check for  pc_index:" << pc_index << "br_type: " << branch_table[pc_index].br_type <<"\n";
              if(branch_table[pc_index].br_type == 0)
              {
                       branch_table[pc_index].br_type = _resolve_dir ? CBZ : CBNZ;
                      std::cout << "under 0 na  check for  pc_index:" << pc_index << "new br set" << branch_table[pc_index].br_type << "\n";

              }
              else if(branch_table[pc_index].br_type == 1)
              {
                      if(_resolve_dir)
                      {
                              std::cout << "branch type cbz found \n";
                              return; //same prediction

                      }
                      else
                      {       branch_table[pc_index].br_type = CBNZ;
                              branch_table[pc_index].bit_position_matters = true;
                                std::cout << "under zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";             
                      }
              }
              else if(branch_table[pc_index].br_type == 2)
              {
                      if(!(_resolve_dir))
                      {
                              std::cout << "branch type cbnz found \n";
                              return; //same prediction

                      }
                      else
                      {       branch_table[pc_index].br_type = CBZ;
                              branch_table[pc_index].bit_position_matters = true;
                              std::cout << "under zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
                      }

              }
        }
        else
        {
              std::cout << "under non-zero  check for  pc_index:" << pc_index << "br_type: " << branch_table[pc_index].br_type <<"\n";
              if(branch_table[pc_index].br_type == 0)
              {

                       branch_table[pc_index].br_type = _resolve_dir ? CBNZ : CBZ;
                       std::cout << "under non-zero with na check for  pc_index:" << pc_index << "new br type " << branch_table[pc_index].br_type << "\n";
              }
              else if(branch_table[pc_index].br_type == 2)
              {
                      std::cout << "entered cbnz check!!! \n";
                      if(_resolve_dir)
                      {
                              std::cout << " branch type cbnz found \n";
                              return; //same prediction
                      }
                      else
                      {
                              branch_table[pc_index].br_type = CBZ;
                              branch_table[pc_index].bit_position_matters = true;
                              std::cout << "under non-zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
                      }
              }
              else if(branch_table[pc_index].br_type == 1)
              {
                      std::cout << "entered cbz check!!! \n";
                      if(!(_resolve_dir))
                      {
                              std::cout << " branch type cbz found \n";
                              return; //same prediction
                      }
                      else
                      {
                              branch_table[pc_index].br_type = CBNZ;
                              branch_table[pc_index].bit_position_matters = true;
                              std::cout << "under non-zero  check POSITION CHANGE  for  pc_index:" << pc_index << "new value " << branch_table[pc_index].br_type << "\n";
                      }

              }

        }
      }
      if(branch_table[pc_index].bit_position_matters == true)
      {
         std::cout << "Have entered tbz test for branch pc_index " << pc_index << "branch bit mask is" << branch_table[pc_index].branch_bit_mask << "\n";
         if (branch_table[pc_index].prev_value != STUPID_VALUE) {
                uint64_t changed_bits = branch_table[pc_index].prev_value ^ dest_reg_val;
                uint64_t unchanged_bits = ~changed_bits;
                uint64_t branch_bit_mask = (_resolve_dir == branch_table[pc_index].prev_taken) ? unchanged_bits : changed_bits;
                std::cout << "Branch bit mask this cycle is " << branch_bit_mask << " branch_table[pc_index].prev_taken " << branch_table[pc_index].prev_taken << " _resolve_dir " << _resolve_dir << " unchanged_bits" << unchanged_bits << " changed_bits " << changed_bits;
                uint64_t prev_branch_bit_mask = branch_table[pc_index].branch_bit_mask;
                branch_table[pc_index].branch_bit_mask &= branch_bit_mask;
                std::cout << "previous value: " << branch_table[pc_index].prev_value << " this time value : " << dest_reg_val << " changed_bits: " << changed_bits << "unchanged_bits" << unchanged_bits << " branch_bit_mask " << branch_bit_mask << "branch_table[pc_index].branch_bit_mask" << branch_table[pc_index].branch_bit_mask << "\n";
                bool prev_direction = branch_table[pc_index].direction_zero_match;
                branch_table[pc_index].direction_zero_match = !(((dest_reg_val & branch_table[pc_index].branch_bit_mask) == 0) ^ _resolve_dir);
                bool prediction =!(((dest_reg_val & branch_table[pc_index].branch_bit_mask) == 0) ^ branch_table[pc_index].direction_zero_match);
                if(prev_direction != branch_table[pc_index].direction_zero_match && branch_table[pc_index].branch_bit_mask == prev_branch_bit_mask && prediction != _resolve_dir)
                {
                        std::cout << "have entered fatal phase prediction is " << prediction << " resolve dir is " <<  _resolve_dir << "prev direction " << prev_direction << " this direction " << branch_table[pc_index].direction_zero_match << "\n";
                        if(!(branch_table[pc_index].bit_flag))
                        {
                                branch_bit_mask = branch_table[pc_index].direction_zero_match? dest_reg_val: branch_table[pc_index].prev_value;
                                branch_table[pc_index].branch_bit_mask &= branch_bit_mask;
                        }
                        else
                        {
                                branch_bit_mask = branch_table[pc_index].direction_zero_match? branch_table[pc_index].prev_value : dest_reg_val;
                                branch_table[pc_index].branch_bit_mask &= branch_bit_mask;
                        }
                }
          }
        branch_table[pc_index].prev_value = dest_reg_val;
        branch_table[pc_index].prev_taken = _resolve_dir;


      uint64_t mask = branch_table[pc_index].branch_bit_mask;
      int bit_count = 0;
      for (int i = 0; i < 64; i++) {
          if (mask & (1ULL << i)) bit_count++;
      }
      if (bit_count > 0) {
          branch_table[pc_index].direction_zero_match = !(((dest_reg_val & branch_table[pc_index].branch_bit_mask) == 0) ^ _resolve_dir);

          if(bit_count == 1)
          {
                  std::cout << " TB direction found its " << branch_table[pc_index].direction_zero_match << "hope this helps \n";
          }
          if(branch_table[pc_index].direction_zero_match)
          {
                branch_table[pc_index].br_type = TBZ;
                std::cout << " TB direction set to  " << branch_table[pc_index].direction_zero_match << " br type is now " << branch_table[pc_index].br_type << "\n";
          }
          else
          {
                  branch_table[pc_index].br_type = TBNZ;
                  std::cout << " TB direction set to  " << branch_table[pc_index].direction_zero_match << " br type is now " << branch_table[pc_index].br_type << "\n";
          }
      }
      else
      {
              if(!(branch_table[pc_index].bit_flag))
              {
                      branch_table[pc_index].bit_flag = true;
                      branch_table[pc_index].branch_bit_mask = UINT64_MAX;
              }
              else
              {
                      std::cout << " FATAL DEBUG final mask : " << branch_table[pc_index].branch_bit_mask  << "branch_table[pc_index].bit_flag " << branch_table[pc_index].bit_flag << " bit_count " << bit_count << "\n";
             std::cout << "NOTE NOTE SOMETHING WENT WRONG!!!!! Branch type could not be determined!!!\n" << std::endl;

              }
      }
      }


}
void learn_src_branch_behaivour(uint16_t pc_index,  uint64_t dest_reg_val, const bool _resolve_dir)
{

}

uint64_t branch_inst_count = 0; // max to 1000
void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
{
    if (is_cond_br(_exec_info.dec_info.insn_class))
    {
      const bool _resolve_dir = _exec_info.taken.value();

      uint16_t pc_index = pc & 0xFFFF;
      uint64_t tag = (pc & 0xFFF0000) >> 16;

      // mechnaism to find high mispredction branches
      if(_exec_info.dec_info.src_reg_info.size() > 0) {
        if(tag == branch_table[pc_index].tag) {
          if(_resolve_dir != pred_dir) {
            if (branch_table[pc_index].sat_ctr < BT_SAT_COUNTER_MAX) {
              branch_table[pc_index].sat_ctr += 1;
            }
          }
        } else {
          if(branch_table[pc_index].sat_ctr == 0 && !(branch_table[pc_index].is_linked)) {
            branch_table[pc_index].tag = tag;
            branch_table[pc_index].src_reg = _exec_info.dec_info.src_reg_info[0];
            branch_table[pc_index].sat_ctr = 1;
            branch_table[pc_index].override_tage_pred = false;
            branch_table[pc_index].num_triggers = 0;
            branch_table[pc_index].is_linked = false;
          }
        }
      }

      // append to misprediction list of pc's where entry saturation counter = max
      if(branch_table[pc_index].tag == tag && branch_table[pc_index].sat_ctr == BT_SAT_COUNTER_MAX) {
        // append full 64-bit pc to misprediction list
        high_mispred_pc.insert(pc);
        uint64_t dest_reg_val = RegFile[_exec_info.dec_info.src_reg_info[0]];
        if(pc_index == 37824 )
        {
                std::cout<< "src reg for weird branch!!" << _exec_info.dec_info.src_reg_info[0]<< "\n";
        }
        if(_exec_info.dec_info.src_reg_info[0] != 64)
        {
         get_branch_bit_direction(pc_index, dest_reg_val, _resolve_dir);
        }
        else
        {
                learn_src_branch_behaivour(pc_index, dest_reg_val, _resolve_dir);
        }
        //learning branch direction and bit (TBZ vs CBZ)
        // print the branch we are going to analyze
        // std::cout << "---------------------------------------------------" << std::endl;
        // std::cout << "Processing H2P Branch PC: 0x" << std::hex << pc << std::dec << " | " << _exec_info << std::endl;

        // loop thorugh last 8 entries in retire_op_queue and print out the instructions
        //std::cout << "Last 8 RetireOp Queue Entries:" << std::endl;
        for(int i = 0; i < 8 && i < retire_op_queue.size(); i++) {
          RetireOp retire_op = retire_op_queue[i];
          //std::cout << "PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info << std::endl;
          if(!(retire_op.exec_info.dec_info.dst_reg_info.has_value())) {
            continue;
          }
          uint64_t dest_reg_idx = retire_op.exec_info.dec_info.dst_reg_info.value();
          // check if branch register is same as load producing register
          if(dest_reg_idx == _exec_info.dec_info.src_reg_info[0]) {
            //uint64_t dest_reg_val = retire_op.exec_info.dst_reg_value.value();
            //get_branch_bit_direction(pc_index, dest_reg_val, _resolve_dir);
            // check if the instruction is a load
            if(retire_op.exec_info.dec_info.insn_class != InstClass::loadInstClass) {
              // std::cout << "Producer is not a load, skipping..." << std::endl;
              // std::cout << "PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info << std::endl;
              break;
            }
            // print out address
            uint64_t load_addr = retire_op.exec_info.mem_va.value();
            // std::cout << "Load Address: " << std::hex << load_addr << std::dec << std::endl;

            // check if address is in store table
            uint64_t addr_index = load_addr & 0xFFFF;
            uint64_t addr_tag = (load_addr & 0xFFF0000) >> 16;
            if(store_table[addr_index].tag == addr_tag) {
              // print out store table entry
              // std::cout << "Store Table Entry: " << std::endl;
              // std::cout << "Index: " << addr_index << " | Valid: " << ST[addr_index].is_valid << " | Zero: " << ST[addr_index].is_zero << std::endl;
              // std::cout << "Linked to PC: 0x" << std::hex << ST[addr_index].pc << std::dec << std::endl;

              uint64_t store_pc = store_table[addr_index].pc;
              uint64_t store_value = store_table[addr_index].value;
              uint64_t store_pc_index = store_pc & 0xFFFF;
              uint64_t store_pc_tag = (store_pc & 0xFFF0000) >> 16;

              bool store_trigger_found = false;
              for(int j = 0; j < branch_table[pc_index].num_triggers; j++) {
                // check if the chain is already made
                if(branch_table[pc_index].store_triggers[j] == store_pc) {
                  store_trigger_found = true;
                  trigger_table[store_pc_index].br_type = branch_table[pc_index].br_type;
                  trigger_table[store_pc_index].branch_bit_mask = branch_table[pc_index].branch_bit_mask;
                  break;
                }
              }

              if(!store_trigger_found) {
                  // std::cout << "Making chain with PC: 0x" << std::hex << store_pc << std::dec << " --> Branch PC: 0x" << std::hex << pc <<std::endl;
                  // add the store pc to the chain
                  branch_table[pc_index].store_triggers[branch_table[pc_index].num_triggers] = store_pc;
                  branch_table[pc_index].num_triggers += 1;

                  // add load to the load table
                  uint64_t load_pc = retire_op.pc;
                  uint64_t load_pc_index = load_pc & 0xFFFF;
                  uint64_t load_pc_tag = (load_pc & 0xFFF0000) >> 16;
                  if(!load_table[load_pc_index].valid) {
                    // add to the load table
                    load_table[load_pc_index].tag = load_pc_tag;
                    load_table[load_pc_index].br_pc = pc;
                    load_table[load_pc_index].last_addr = load_addr;
                    load_table[load_pc_index].stride = 8;
                    load_table[load_pc_index].valid = true;
                    if(LV_DEBUG_FLAG) {
                      std::cout << "Load Table Entry Created: " << std::endl;
                      std::cout << "Load PC: 0x" << std::hex << load_pc << std::dec << " | Load Addr: 0x" << std::hex << load_addr << std::dec
                                << " | BR PC: 0x" << std::hex << pc << std::dec << " | Stride: " << load_table[load_pc_index].stride << std::endl;
                    }
                  }



                  // determine branch type
                  // branch was taken
                  //if(_resolve_dir) {
                  //  if(store_table[addr_index].value == 0) {
                  //    branch_table[pc_index].br_type = CBZ;
                  //  } else {
                  //    branch_table[pc_index].br_type = CBNZ;
                  //  }
                  // branch was not taken
                 // } else {
                 //   if(store_table[addr_index].value == 0) {
                 //     branch_table[pc_index].br_type = CBNZ;
                 //   } else {
                 //     branch_table[pc_index].br_type = CBZ;
                 //   }
                 // }

                  if(trigger_table[store_pc_index].tag == 0) {
                    // add to the trigger table
                    trigger_table[store_pc_index].tag = store_pc_tag;
                    trigger_table[store_pc_index].value = store_value;
                    trigger_table[store_pc_index].addr = load_addr;
                    trigger_table[store_pc_index].br_type = branch_table[pc_index].br_type;
                    trigger_table[store_pc_index].branch_bit_mask = branch_table[pc_index].branch_bit_mask;

                  } else {
                    // update the trigger table
                    trigger_table[store_pc_index].tag = store_pc_tag;
                    trigger_table[store_pc_index].addr = load_addr;
                  }

              }

              branch_table[pc_index].is_linked = true;
            }
            break;
          }
        }
      }

      // peridoic reset of saturation counter
      if(branch_inst_count == 1000) {
        // std::cout << "\nFinal unique PC list:\n";
        // for (uint64_t pc : high_mispred_pc) {
        //     std::cout << "0x" << std::hex << std::uppercase << pc << std::endl;
        // }
        for(int i = 0; i < BT_SIZE; i++) {
          if(branch_table[i].sat_ctr < 5) {
            branch_table[i].sat_ctr = 0;
          } else {
            branch_table[i].sat_ctr -= 5;
          }
        }
        branch_inst_count = 0;
      } else {
        branch_inst_count++;
      }
    }

    // Update RetireOp Queue
    RetireOp retire_op;
    retire_op.pc = pc;
    retire_op.exec_info = _exec_info;

    if(retire_op_queue.size() == RETIRE_OP_QUEUE_SIZE) {
      retire_op_queue.pop_back();
    }

    retire_op_queue.push_front(retire_op);


    // Update RegFile
    if(_exec_info.dst_reg_value.has_value()) {
      uint64_t dest_val = _exec_info.dst_reg_value.value();
      uint64_t dest_reg = _exec_info.dec_info.dst_reg_info.value();
      RegFile[dest_reg] = dest_val;
      if(dest_reg != 65) { // skip zero register
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
void endCondDirPredictor ()
{
    cbp2016_tage_sc_l.terminate();
    cond_predictor_impl.terminate();
}

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

//
// beginCondDirPredictor()
// 
// This function is called by the simulator before the start of simulation.
// It can be used for arbitrary initialization steps for the contestant's code.
//

// Branch Table Info
#define BT_SIZE 65535
#define BT_SAT_COUNTER_MAX 5
BranchTableEntry BT[BT_SIZE]; // 2^16 entries - 1
std::unordered_set<uint64_t> high_mispred_pc;

// RetireOp Queue
std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

// Store Table Info
#define ST_SIZE 65535
StoreTableEntry ST[ST_SIZE]; // 2^16 entries - 1

LoadTableEntry LT[66]; //load table
uint64_t RegFile[66];  
void beginCondDirPredictor()
{
    // setup sample_predictor
    cbp2016_tage_sc_l.setup();
    cond_predictor_impl.setup();
    // initial BT setup
    for (int i = 0; i < BT_SIZE; i++) {
        BT[i].src_reg = 0;
        BT[i].sat_counter = 0;
        BT[i].tag_entry = 0;
        BT[i].hard_to_predict = 0;
    }

    // initial ST setup
    for (int i = 0; i < ST_SIZE; i++) {
        ST[i].is_valid = false;
        ST[i].is_zero = true;
    }
    //initial LT setup 
    for (int i = 0; i < 66; i++) {
        LT[i].consumer_address = 0;
        LT[i].addr = 0;
    }

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
    const bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);

    
    //is the pc hart to predict? 
    uint16_t pc_ind = pc & 0xFFFF;
    uint16_t tag_entry = (pc & 0xFFFF0000) >> 16;
    if(BT[pc_ind].tag_entry == tag_entry)
    {
      if (BT[pc_ind].hard_to_predict){
        std::cout << "hard to predict entered\n";
        uint16_t reg_ind = BT[pc_ind].src_reg;    
        std::cout << LT[reg_ind].addr << "lt check"<< LT[reg_ind].consumer_address <<"\n";
        if(LT[reg_ind].addr){
          std::cout << "load table lookup\n";
          uint64_t store_addr = LT[reg_ind].consumer_address;
          uint64_t st_addr_index = store_addr & 0xFFFF;
          uint16_t st_addr_tag = (store_addr & 0xFFFF0000) >> 16;

          if(ST[st_addr_index].store_tag == st_addr_tag)
          std::cout << "store table lookup\n";
          {
            if(ST[st_addr_index].is_valid)
            {
              if(ST[st_addr_index].is_zero)
              {
                return false;
              }
              else
              {
                return true;
              }
            }

          }
        }

      }
    }
    //3 table lookup

    //value;
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
  // uint64_t target_addr = 0xffffefd1e6d8;
  // if(mem_va == target_addr) {
  //   std::cout << "Target instruction at PC: 0x" << std::hex << pc << std::dec
  //             << " | Cycle: " << agen_cycle
  //             << " | " << _decode_info  // Use the overloaded operator<< for ExecuteInfo
  //             << std::endl;
  // }
}

//
// notify_instr_execute_resolve(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t execute_cycle)
// 
// This function is called when any instructions(not just branches) gets executed.
// Along with the unique identifying ids(seq_no, piece), PC of the instruction, execute info and cycle are also provided as inputs
//
// For conditional branches, we use this information to update the predictor.
// At the moment, we do not consider updating any other structure, but the contestants are allowed to  update any other predictor state.
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


uint64_t branch_inst_count = 0; // max to 1000
void notify_instr_commit(uint64_t seq_no, uint8_t piece, uint64_t pc, const bool pred_dir, const ExecuteInfo& _exec_info, const uint64_t commit_cycle)
{
    // if(_exec_info.dec_info.insn_class == InstClass::loadInstClass || _exec_info.dec_info.insn_class == InstClass::storeInstClass) {
    //   uint64_t target_addr = 0xffffefd1e6d8;
    //   if(_exec_info.mem_va && _exec_info.mem_va == target_addr) 
    //     std::cout << "Committing instruction at PC: 0x" << std::hex << pc << std::dec
    //               << " | Cycle: " << commit_cycle
    //               << " | " << _exec_info  // Use the overloaded operator<< for ExecuteInfo
    //               << std::endl;
    // }

    if (is_cond_br(_exec_info.dec_info.insn_class))
    {
      const bool _resolve_dir = _exec_info.taken.value();
      const uint64_t target_pc = 0xFFFFF0D8F2CC;

      uint16_t pc_index = pc & 0xFFFF;
      uint16_t tag_entry = (pc & 0xFFFF0000) >> 16; 
      // print num src regs
      if(_exec_info.dec_info.src_reg_info.size() > 0) {
          if(BT[pc_index].tag_entry == tag_entry) {  //Future work : if flag is 0, and tag not match, replace entry
            if(BT[pc_index].src_reg == _exec_info.dec_info.src_reg_info[0]) {
              if(_resolve_dir != pred_dir) {
                if (BT[pc_index].sat_counter < BT_SAT_COUNTER_MAX) {
                  BT[pc_index].sat_counter += 1;
                }
              }
            }
           else {
            if(BT[pc_index].sat_counter == 0) {
              BT[pc_index].src_reg = _exec_info.dec_info.src_reg_info[0];
              BT[pc_index].sat_counter = 1;
              BT[pc_index].hard_to_predict = 0;
            }
          }
          } 
          else if(BT[pc_index].tag_entry == 0)
          {
            BT[pc_index].sat_counter == 0;
            BT[pc_index].src_reg = _exec_info.dec_info.src_reg_info[0];
            BT[pc_index].sat_counter = 1;
            BT[pc_index].tag_entry = tag_entry;
            BT[pc_index].hard_to_predict = 0;

          }
      }

      // append to misprediction list of pc's where entry saturation counter = max
      if(BT[pc_index].sat_counter == BT_SAT_COUNTER_MAX) {
        // append full 64-bit pc to misprediction list
        high_mispred_pc.insert(pc);
        BT[pc_index].hard_to_predict = 1;
        std::cout << "hard to predict branch added vin" << pc << std::endl;
      }

      if(branch_inst_count == 1000) {
        // std::cout << "\nFinal unique PC list:\n";
        // for (uint64_t pc : high_mispred_pc) {
        //     std::cout << "0x" << std::hex << std::uppercase << pc << std::endl;
        // }
        for(int i = 0; i < BT_SIZE; i++) {
          if(BT[i].sat_counter < 15) {
            BT[i].sat_counter = 0;
            BT[i].hard_to_predict = 0;
          } else {
            BT[i].sat_counter -= 15;
          }
        }
        branch_inst_count = 0;
      } else {
        branch_inst_count++;
      }
      
      // print the branch table for target_pc
      // if(pc == target_pc) {
      //   std::cout << "Branch Table for PC: 0x" << std::hex << pc << std::dec << std::endl;
      //   if(_resolve_dir != pred_dir) {
      //     std::cout << "Misprediction: " << " | Resolve Dir: " << _resolve_dir << " | Pred Dir: " << pred_dir << std::endl;
      //   }
      //   if(BT[0xF2CC].sat_counter > 0) {
      //     std::cout << "Index: " << 0xF2CC << " | Src Reg: " << BT[0xF2CC].src_reg << " | Sat Counter: " << BT[0xF2CC].sat_counter << std::endl;
      //   }
      // }
    }

    if(_exec_info.dec_info.insn_class == InstClass::storeInstClass) {
      uint64_t addr = _exec_info.mem_va.value();
      uint64_t addr_index = addr & 0xFFFF;
      uint64_t st_addr_tag = (addr & 0xFFFF0000) >> 16;
      uint64_t src_reg_data_idx = _exec_info.dec_info.src_reg_info[1];  //bug?
      uint64_t dest_val = RegFile[src_reg_data_idx];
      ST[addr_index].is_valid = true;
      ST[addr_index].pc = pc;
      ST[addr_index].store_tag = st_addr_tag;
      if(dest_val != 0) {
        ST[addr_index].is_zero = false;
      } else {
        ST[addr_index].is_zero = true;
      }
      // print pc and reg file values
      // if(pc == target_mem_pc) {
      //   std::cout << "Store Table for PC: 0x" << std::hex << pc << std::dec << std::endl;
      //   std::cout << "Src Reg: " << src_reg_data_idx << " | Dest Val: 0x" << std::hex << dest_val << << std::dec std::endl;
      //   std::cout << "Index: " << addr_index << " | Valid: " << ST[addr_index].is_valid << " | Zero: " << ST[addr_index].is_zero << std::endl;
      // }
    }

        if(_exec_info.dec_info.insn_class == InstClass::loadInstClass) {
      uint64_t ld_addr = _exec_info.mem_va.value();
      uint64_t ld_addr_index = ld_addr & 0xFFFF;
      uint64_t dst_reg_data_idx = _exec_info.dec_info.dst_reg_info.value();
      uint64_t dest_val = RegFile[dst_reg_data_idx];
      LT[dst_reg_data_idx].consumer_address = ld_addr;
      LT[dst_reg_data_idx].addr = 1;    //Future work add additional logic to directly get value if alu op
      // print pc and reg file values
      // if(pc == target_mem_pc) {
      //   std::cout << "Store Table for PC: 0x" << std::hex << pc << std::dec << std::endl;
      //   std::cout << "Src Reg: " << src_reg_data_idx << " | Dest Val: 0x" << std::hex << dest_val << << std::dec std::endl;
      //   std::cout << "Index: " << addr_index << " | Valid: " << ST[addr_index].is_valid << " | Zero: " << ST[addr_index].is_zero << std::endl;
      // }
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

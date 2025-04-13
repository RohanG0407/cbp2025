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
#define BT_SAT_COUNTER_MAX 31
BranchTableEntry BT[BT_SIZE]; // 2^16 entries - 1
std::unordered_set<uint64_t> high_mispred_pc;

// RetireOp Queue
std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

// Store Table Info
#define ST_SIZE 65535
StoreTableEntry ST[ST_SIZE]; // 2^16 entries - 1

// Seeker Buffer - An unordered list of PCs from where we need to build the DFG backwards
//std::unordered_set<uint64_t> seeker_buffer;
Seeker_Buffer seeker_buffer;
// PC -> uid mapping
const uint16_t num_uids = 256;
//PC_2_uid_map uid_map(num_uids);
PC_UID_Map uid_map(num_uids);
// Track Producer-Consumer relationship for registers
Producer_Consumer_Pairs_Register prodCons_reg(66);
// Prediction Table
Prediction_Table pred_table(16);
// Reservation Station
Reservation_Station reservation_station(num_uids);
// Track Producer-Consumer relationship for memory addresses
Producer_Consumer_Pairs_Memory prodCons_mem(1024);
// Trigger List
Trigger_Buffer trig_buffer(80);

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
    }

    // initial ST setup
    for (int i = 0; i < ST_SIZE; i++) {
        ST[i].is_valid = false;
        ST[i].is_zero = true;
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
    // akhilesh - comment this block if commit instruction stream is not required
        bool log_instruction_1 = true;
        bool log_instruction_2 = commit_cycle < 1000;
        bool log_instruction_3 = commit_cycle < 6000;
        bool log_instruction_4 = commit_cycle > 4000 && commit_cycle < 5000;
    // if (log_instruction_1) {
    //   std::cout << "PC: 0x" << std::hex << pc << std::dec
    //                 << " | Cycle: " << commit_cycle
    //                 << " | " << _exec_info  // Use the overloaded operator<< for ExecuteInfo
    //                 << std::endl;
    // }

    // Update producer-consumer tracking tables - register
      /* FIXME - If the same pc is consumer as well as producer then the logic and usage of the producer-consumer tracking mechanism is flawed. Need to do something about that situation.
          In the current implementation, the table is guaranteed to give the correct picture only if the register entry is checked when the instruction is a consumer of the register. Otherwise, can't say.
          The simulation trick to handle this situation is to record a pc as producer after all required accesses to this table are done... i.e. towards the end of the simulation of this cycle
          Of course this won't directly translate to hardware but a simple solution would be to have a 1-bit signal indicating if a PC is simultaneously the producer and consumer of a register.
          Or delay the update to producer PC by one cycle. Good luck doing this with multi-fetch architectures
      */
    // if (pc == 0x402694) {
    //   std::cout << "At pc - 0x" << std::hex << pc;
    //   std::cout << " | #src_reg = " << _exec_info.dec_info.src_reg_info.size()
    //             << ", src_reg[0] - " << _exec_info.dec_info.src_reg_info[0]
    //             << ", src_reg[1] - " << _exec_info.dec_info.src_reg_info[1] << "\n";
    // }
    for (int i = 0; i < _exec_info.dec_info.src_reg_info.size(); i++) {
      prodCons_reg.record_consumer(_exec_info.dec_info.src_reg_info[i], pc);
    }

    // Update producer-consumer tracking tables - memory
    if (is_store(_exec_info.dec_info.insn_class)) {
      prodCons_mem.record_consumer(_exec_info.mem_va.value(), pc);
    }
    if (is_load(_exec_info.dec_info.insn_class)) {
      prodCons_mem.record_consumer(_exec_info.mem_va.value(), pc);
    }

    // if (pc == 0x449D8C) {
    //   prodCons_mem.printState(_exec_info.mem_va.value());
    // }
    // if (pc == 0x800002f0) {
    //   std::cout << "\tSource reg info: " << _exec_info.dec_info.src_reg_info.size()
    //             << "\n\tDest. reg info: " << _exec_info.dec_info.dst_reg_info.has_value() << "\n";
    //   prodCons_reg.printState(0);
    //   prodCons_reg.printState(3);
    // }

    if (is_cond_br(_exec_info.dec_info.insn_class))
    {
      const bool _resolve_dir = _exec_info.taken.value();
      //const uint64_t target_pc = 0xFFFFF0D8F2CC;
      // from sample_traces/fp/sample_fp_trace.gz
      const uint64_t target_pc = 0x449d9c;

      uint16_t pc_index = pc & 0xFF;
      // print num src regs
      if(_exec_info.dec_info.src_reg_info.size() > 0) {
        if(BT[pc_index].src_reg == _exec_info.dec_info.src_reg_info[0]) {
          if(_resolve_dir != pred_dir) {
            if (BT[pc_index].sat_counter < BT_SAT_COUNTER_MAX) {
              BT[pc_index].sat_counter += 1;
            }
          }
        } else {
          if(BT[pc_index].sat_counter == 0) {
            BT[pc_index].src_reg = _exec_info.dec_info.src_reg_info[0];
            BT[pc_index].sat_counter = 1;
          }
        }
      }

      // append to misprediction list of pc's where entry saturation counter = max
      if(BT[pc_index].sat_counter == BT_SAT_COUNTER_MAX) {
        // append full 64-bit pc to misprediction list
        high_mispred_pc.insert(pc);
        std::cout << "akhilesh - Marked as highly_mispredicted: " << pc << "\n";
      }

      // append highly mispredicted branch to seeker buffer
      if(pc == target_pc) {
        //std::cout << "akhilesh - assuming branch at " << pc << " is highly mispredicted\n";
        if (!pred_table.is_learnt(pc))
          seeker_buffer.insert(pc);
        //seeker_buffer.printState();
      }

      if(branch_inst_count == 1000) {
        // std::cout << "\nFinal unique PC list:\n";
        // for (uint64_t pc : high_mispred_pc) {
        //     std::cout << "0x" << std::hex << std::uppercase << pc << std::endl;
        // }
        for(int i = 0; i < BT_SIZE; i++) {
          if(BT[i].sat_counter < 15) {
            BT[i].sat_counter = 0;
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
      uint64_t addr_index = addr & 0xFF;
      uint64_t src_reg_data_idx = _exec_info.dec_info.src_reg_info[1];
      uint64_t dest_val = RegFile[src_reg_data_idx];
      ST[addr_index].is_valid = true;
      ST[addr_index].pc = pc;
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

    // check if current PC is sought to learn the DFG
    if (seeker_buffer.exists(pc)) {
      //std::cout << "akhilesh - Learn DFG before current pc " << pc << "\n";
      
      if (is_load(_exec_info.dec_info.insn_class)) {
        std::cout << "INFO:: Load instruction detected at pc 0x" << std::hex << pc
                  << " | Mem VA: 0x" << _exec_info.mem_va.value() << "\n";
        
        uint64_t memVA = _exec_info.mem_va.value();
        //prodCons_mem.printState(memVA);
        if (prodCons_mem.is_tracked(memVA)) {
          if (prodCons_mem.is_traced(memVA)) {
            uint16_t uid_currPC = uid_map.get_uid(pc);

            uint64_t producerPC = prodCons_mem.get_producerPC(memVA);
            uint16_t uid_producerPC = uid_map.get_uid(producerPC);

            uint64_t src_value = _exec_info.dst_reg_value.value();

            // Add Load instruction to Reservation Station
            reservation_station.add_entry(uid_currPC, _exec_info.dec_info.insn_class, uid_producerPC, src_value);
            reservation_station.printState();

            // Add Store instruction to Trigger List
            trig_buffer.add_entry(producerPC, uid_producerPC);
            trig_buffer.printState();

            // Remove load instruction from Seeker Buffer
            seeker_buffer.remove(pc);
            seeker_buffer.printState();
          }
        } else {  // Add memory address to tracker
          prodCons_mem.add_memVA(memVA, pc);
          prodCons_mem.printState(memVA);
        }
      } else {
        std::cout << "INFO:: Non load instruction detected at pc 0x" << std::hex << pc
                  << "\nSource Registers: { " << std::dec;
        for (int i = 0; i < _exec_info.dec_info.src_reg_info.size(); i++)
          std::cout << _exec_info.dec_info.src_reg_info[i] << " ";
        std::cout << "}\n";
        for (int i = 0; i < _exec_info.dec_info.src_reg_info.size(); i++)
          prodCons_reg.printState(_exec_info.dec_info.src_reg_info[i]);

        // Get all information regarding source registers
        uint8_t src_reg;
        uint64_t producerPC;
        std::vector<uint64_t> producerPC_vector;
        std::vector<uint16_t> uid_producerPC_vector;
        std::vector<uint64_t> src_reg_value_vector;
        for (int i = 0; i < _exec_info.dec_info.src_reg_info.size(); i++) {
          src_reg = _exec_info.dec_info.src_reg_info[i];
          producerPC = prodCons_reg.get_producerPC(src_reg);
          producerPC_vector.push_back(producerPC);
          uid_producerPC_vector.push_back(uid_map.get_uid(producerPC));
          src_reg_value_vector.push_back(RegFile[src_reg]);
        }
        // Assumption - at max 3 input registers. Else rethink the reservation Station
        if (_exec_info.dec_info.src_reg_info.size() > 3)
          std::cout << "ERROR:: Expected 3 or less input registers. " << _exec_info.dec_info.src_reg_info.size() << " detected for instruction at pc 0x" << std::hex << pc;

        if (is_cond_br(_exec_info.dec_info.insn_class)) {
          std::cout << "INFO:: Cond. Branch instruction detected at pc 0x" << std::hex << pc << "\n";

          // Branch instructions go in the branch prediction table
          pred_table.add_entry(pc, uid_producerPC_vector[0]);
          pred_table.printState();
        } else {
          std::cout << "akhilesh - Non load, Non cond. branch instruction detected at pc 0x" << std::hex << pc << "\n";
          
          uint16_t uid_currPC = uid_map.get_uid(pc);
          //std::cout << "akhilesh - uid is " << pc << "-->" << uid_currPC << "\n";
          
          // Non-branch instructions go to Reservation Station
          reservation_station.add_entry(uid_currPC, _exec_info.dec_info.insn_class, uid_producerPC_vector, src_reg_value_vector);
          reservation_station.printState();
        }

        // Update Seeker Buffer
        seeker_buffer.remove(pc);
        for (uint64_t prod_pc: producerPC_vector)
          seeker_buffer.insert(prod_pc);
        seeker_buffer.printState();
      }
    }

    // Simulation trick to handle scenarios when the same PC is the consumer and producer of a register
    if (_exec_info.dec_info.dst_reg_info.has_value()) {
      prodCons_reg.record_producer(_exec_info.dec_info.dst_reg_info.value(), pc);
    }

    // if (pc == 0x800002f0) {
    // //  std::cout << "\tSource reg info: " << _exec_info.dec_info.src_reg_info.size()
    // //             << "\n\tDest. reg info: " << _exec_info.dec_info.dst_reg_info.has_value() << "\n";
    //   prodCons_reg.printState(0);
    //   prodCons_reg.printState(3);
    // }  
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

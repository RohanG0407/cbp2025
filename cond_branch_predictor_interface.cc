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
#define VARIANT 2
#define SUBVARIANT 2
#define STUPID_VALUE 8888
BranchTableEntry BT[BT_SIZE]; // 2^16 entries - 1
std::unordered_set<uint64_t> high_mispred_pc;

// RetireOp Queue
std::deque<RetireOp> retire_op_queue;
#define RETIRE_OP_QUEUE_SIZE 64

// Store Table Info for now it is 2^16 might require tag bits? vin
#define ST_SIZE 65535
StoreTableEntry ST[ST_SIZE]; // 2^16 entries - 1

// Store Chain Info stores same number of entries right now, but makes sense to reduce no of bits? vin
#define SC_SIZE 65535
StoreChainEntry SCT[SC_SIZE]; // 2^16 entries - 1

// Store Chain Info confused on the utility vin
uint64_t linked_store_table[ST_SIZE]; // 2^16 entries - 1

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
        BT[i].tag = 0;
        BT[i].override_tage_pred = false;
        BT[i].st_table_index = 0; 
        BT[i].valid_chains = 0;
	BT[i].direction_matched = false;
	//VARIANT 2
	for (int k = 0; k < PHT_SIZE; k++) {
        	for (int j = 0; j < NUM_CHAINS; j++) {
            		BT[i].pht[k][j] = 0;
			BT[i].actual_chain = 0;
        	}
    	}	
	
	BT[i].prev_value = STUPID_VALUE; 
        BT[i].prev_taken = false;         // Previous branch outcome
        BT[i].branch_bit_mask = UINT64_MAX; // Mask with 1 at the branch bit
        BT[i].bit_position = 0;  //need to improve
    
    }

    // initial retire_op_queue setup
    retire_op_queue.clear();

    // initial SC setup
    for (int i = 0; i < SC_SIZE; i++) {
        SCT[i].tag = 0;
        SCT[i].is_valid = false;
        SCT[i].predict_taken = false;
//        SCT[i].direction_matched = false;
	SCT[i].value = 0;
    }

    // initial ST setup
    for (int i = 0; i < ST_SIZE; i++) {
        ST[i].is_valid = false;
        ST[i].is_zero = true;
        ST[i].pc = 0;
        ST[i].predict_taken = false;
//        ST[i].direction_matched = false;
        ST[i].link_made = false;
	ST[i].value = 0;
        linked_store_table[i] = 0;
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
    uint64_t pc_index = pc & 0xFFFF;
    uint64_t tag = (pc & 0xFFF0000) >> 16;
    // use custom predictor to predict the branch direction
    const bool tage_sc_l_pred =  cbp2016_tage_sc_l.predict(seq_no, piece, pc);
    bool my_prediction = cond_predictor_impl.predict(seq_no, piece, pc, tage_sc_l_pred);
    if(BT[pc_index].tag == tag)
    {
	    if(BT[pc_index].override_tage_pred)
	    {
		    if(BT[pc_index].valid_chains == 0)
		    {
			    std::cout <<"no valid chains for predict branch!!!!\n";
		    }
		    if(BT[pc_index].valid_chains == 1)
		    {
			BT[pc_index].st_table_index = BT[pc_index].dependence_chains[0];
		        my_prediction = SCT[BT[pc_index].st_table_index].predict_taken;
			return my_prediction;		
		    }
		    else if(VARIANT == 1) // Using votes from each of the dependent chains to choose the winner prediction.
		    {
			int decider = 0;
			for(int i= 0; i < BT[pc_index].valid_chains; i++)
			{
				uint64_t temp_sc_index = BT[pc_index].dependence_chains[i];
				decider =+ SCT[temp_sc_index].predict_taken;    //if more than half chains predict taken we predict taken.
			}
			if(decider >= (BT[pc_index].valid_chains/2))
			{
				my_prediction = 1;
				return my_prediction;
			}
			else
			{
				my_prediction = 0;
				return my_prediction;
			}	
		    }
		    else if(VARIANT == 2) //2 level predictor for dependence chain. GHR has been added to all the 65k lines of BT table, size reduction possible, but that would make lookup more expensive, since only HTP PCs will have to be an assosiative lookup. Or will have to create another table with 16 HTP branches with seperate UID indexes with a hash lookup.
		    {
			        int index = BT[pc_index].ghr;  // Use GHR as PHT index (0 to 255)
    				int max_chain = 0;  // Default to chain 0
    				int max_count = BT[pc_index].pht[index][0];  // Start with counter for chain 0

    				// Find the chain with the highest counter
    				for (int i = 1; i < NUM_CHAINS; i++) {
        				if (BT[pc_index].pht[index][i] > max_count) {
            					max_count = BT[pc_index].pht[index][i];
            					max_chain = i;
        				}
    				}
				BT[pc_index].actual_chain = max_chain;			
    				// max_chain is the predicted dependency chain. 
				uint64_t temp_sc_index = BT[pc_index].dependence_chains[max_chain];
				return SCT[temp_sc_index].predict_taken;
		    }
	    }
    }
    // if(BT[pc_index].tag == tag && BT[pc_index].override_tage_pred) {
    //   uint64_t st_index = BT[pc_index].st_table_index;
    //   my_prediction = SCT[st_index].predict_taken;
    //   // std::cout << "Using custom predictor for PC: 0x" << std::hex << pc << std::dec << " | Prediction: " << my_prediction << std::endl;
    // }
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
	    //VARIANT 2
	    //if(VARIANT == 2) commenting this for now to do this updating anyway for now. 
	    //{
	    if(SUBVARIANT == 1)  //we will mostly not use this, subvariant 2 is default, can ignore this. Here we are updating just based on if prediction was correct or not, with no visibility into load addr generated. (For corner case, where all the dependence chains do get called within the retire ops. 
	    {
	        uint16_t pc_index = pc & 0xFFFF;
      		uint64_t tag = (pc & 0xFFF0000) >> 16;
	        int index = BT[pc_index].ghr;  // Current GHR value as PHT index
		int actual_chain = BT[pc_index].actual_chain;
		if(BT[pc_index].tag == tag)
		{
    			// Increment counter for actual chain (up to MAX_COUNTER)
			if(_resolve_dir == pred_dir)
			{
				if (BT[pc_index].pht[index][actual_chain] < MAX_COUNTER ) {
        				BT[pc_index].pht[index][actual_chain]++;
    				}

    				// Decrement counters for other chains (down to 0)
    				for (int i = 0; i < NUM_CHAINS; i++) {
        				if (i != actual_chain && BT[pc_index].pht[index][i] > 0) {
            					BT[pc_index].pht[index][i]--;
        				}
    				}
			}
			else
			{
				if (BT[pc_index].pht[index][actual_chain] < MAX_COUNTER ) {
                                        BT[pc_index].pht[index][actual_chain]--;
                                }

                                // Decrement counters for other chains (down to 0)
                                for (int i = 0; i < NUM_CHAINS; i++) {
                                        if (i != actual_chain && BT[pc_index].pht[index][i] > 0) {
                                                BT[pc_index].pht[index][i]++;
                                        }
                                }
			}	

    		// Update GHR: Shift left by 2 bits, add actual_chain, mask to 8 bits
    			BT[pc_index].ghr = ((BT[pc_index].ghr << 2) | (actual_chain & 0x3)) & ((1 << GHR_SIZE) - 1);
		}
	    }
		//}
	}//vineeth updating required?
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
void update_chain_predictor(uint16_t pc_index, int j)
{
	if(SUBVARIANT == 2)   //updating two level predictor based on retire ops address match, since we will most likely use this, removing the if condition itself.
	{
		int index = BT[pc_index].ghr;
		if (BT[pc_index].pht[index][j] < MAX_COUNTER ) {
                	BT[pc_index].pht[index][j]++;
                }

                // Decrement counters for other chains (down to 0)
                for (int i = 0; i < NUM_CHAINS; i++) {
                	if (i != j && BT[pc_index].pht[index][i] > 0) {
                         	BT[pc_index].pht[index][i]--;
                        }
                }

	BT[pc_index].ghr = ((BT[pc_index].ghr << 2) | (j  & 0x3)) & ((1 << GHR_SIZE) - 1);
	}

}

void get_branch_bit(uint16_t pc_index, uint16_t sct_index, const bool _resolve_dir)
{
      if(BT[pc_index].bit_position != -1)
      {
	 if (BT[pc_index].prev_value != STUPID_VALUE) {
        	uint64_t changed_bits = BT[pc_index].prev_value ^ SCT[sct_index].value;
        	uint64_t unchanged_bits = ~changed_bits;
        	uint64_t branch_bit_mask = (_resolve_dir == BT[pc_index].prev_taken) ? unchanged_bits : changed_bits;
        	BT[pc_index].branch_bit_mask &= branch_bit_mask;
    	  }
      	BT[pc_index].prev_value = SCT[sct_index].value;
      	BT[pc_index].prev_taken = _resolve_dir;
      }
}


void get_branch_type(uint16_t pc_index, uint16_t sct_index, const bool _resolve_dir)
{
	uint64_t mask = BT[pc_index].branch_bit_mask;
        int bit_count = 0;
        for (int i = 0; i < 64; i++) {
            if (mask & (1ULL << i)) bit_count++;
        }
        if (bit_count != 0) {
            BT[pc_index].direction_matched = !(((SCT[sct_index].value & BT[pc_index].branch_bit_mask) == 0) ^ _resolve_dir);
        }
	else
	{
		BT[pc_index].direction_matched = ST[sct_index].is_zero && _resolve_dir;
		BT[pc_index].bit_position = -1;	
	}


}

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

      uint16_t pc_index = pc & 0xFFFF;
      uint64_t tag = (pc & 0xFFF0000) >> 16;
      
      // mechnaism to find high mispredction branches
      if(_exec_info.dec_info.src_reg_info.size() > 0) {
        if(tag == BT[pc_index].tag && BT[pc_index].src_reg == _exec_info.dec_info.src_reg_info[0]) {
          if(_resolve_dir != pred_dir) {
            if (BT[pc_index].sat_counter < BT_SAT_COUNTER_MAX) {
              BT[pc_index].sat_counter += 1;
            }
          }
        } else {
          if(BT[pc_index].sat_counter == 0) {
            BT[pc_index].src_reg = _exec_info.dec_info.src_reg_info[0];
            BT[pc_index].sat_counter = 1;
            BT[pc_index].tag = tag;
            BT[pc_index].override_tage_pred = false;
            BT[pc_index].st_table_index = 0;
            BT[pc_index].valid_chains = 0;
          }
        }
      }

      // append to misprediction list of pc's where entry saturation counter = max
      if(BT[pc_index].tag == tag && BT[pc_index].sat_counter == BT_SAT_COUNTER_MAX) {
        // append full 64-bit pc to misprediction list
        high_mispred_pc.insert(pc);
// Now when we have a commit we want to update our retire OP queue. 
        // print the branch we are going to analyze
        // std::cout << "---------------------------------------------------" << std::endl;
        // std::cout << "Processing H2P Branch PC: 0x" << std::hex << pc << std::dec << " | " << _exec_info << std::endl;
// why last 8 entries, 64 is better no? Rohan
        // loop thorugh last 8 entries in retire_op_queue and print out the instructions
        //std::cout << "Last 8 RetireOp Queue Entries:" << std::endl;
        for(int i = 0; i < 8 && i < retire_op_queue.size(); i++) {
          RetireOp retire_op = retire_op_queue[i];
          //std::cout << "PC: 0x" << std::hex << retire_op.pc << std::dec << " | " << retire_op.exec_info << std::endl;
          // check if retire_op is a load with dest reg idx == branch src reg
//Now after each commit we want to see if in the new queue added, we have any loads and branch registers match. Not a smart thing to do? Vineeth only then updating my ST and SC tables feels off. What if there is a branch right outside the fetch width? Better implementation would be to either check if there are any matching PCs/registers in the entire BT or update this with Branch calls only. 
          if(retire_op.exec_info.dec_info.insn_class == InstClass::loadInstClass) {
            uint64_t dest_reg_idx = retire_op.exec_info.dec_info.dst_reg_info.value();
            // check if branch register is same as load producing register
            if(dest_reg_idx == _exec_info.dec_info.src_reg_info[0]) {
              // print out address
              uint64_t load_addr = retire_op.exec_info.mem_va.value();
               std::cout << "Load Address: " << std::hex << load_addr << std::dec << std::endl;

              // check if address is in store table
	      // Is he assuming that there wont be any store learning seperately? vineeth
              uint64_t addr_index = load_addr & 0xFFFF;
              if(ST[addr_index].is_valid && !(ST[addr_index].link_made)) {
                // print out store table entry
                // std::cout << "Store Table Entry: " << std::endl;
                // std::cout << "Index: " << addr_index << " | Valid: " << ST[addr_index].is_valid << " | Zero: " << ST[addr_index].is_zero << std::endl;
                // std::cout << "Linked to PC: 0x" << std::hex << ST[addr_index].pc << std::dec << std::endl;
//Draw all this maybe to ensure what all is updated.
                uint64_t sct_index = (ST[addr_index].pc) & 0xFFFF;
                uint64_t sct_tag = (ST[addr_index].pc & 0xFFF0000) >> 16; 

                bool chain_found = false;
                for(int j = 0; j < BT[pc_index].valid_chains; j++) {
                  // check if the chain is already made
                  if(BT[pc_index].dependence_chains[j] == ST[addr_index].pc) {
		    update_chain_predictor(pc_index, j); //no need to check tag, already in the if condition, updating two level predictor based on actualdependence chain that probably" got used. 
                    chain_found = true;
                    break;
                  }
                }

                if(!chain_found) {
                    // std::cout << "Making chain with PC: 0x" << std::hex << ST[addr_index].pc << std::dec << std::endl;
                    BT[pc_index].dependence_chains[BT[pc_index].valid_chains] = ST[addr_index].pc;
                    BT[pc_index].valid_chains += 1;
                }
//Now that chains have been set, lets find the branch type and bit here before setting direction 
		get_branch_bit(pc_index, sct_index, _resolve_dir);
		get_branch_type(pc_index, sct_index, _resolve_dir);
                SCT[sct_index].is_valid = true;
                SCT[sct_index].tag = sct_tag;
                ST[addr_index].link_made = true;
                BT[pc_index].override_tage_pred = true;
                BT[pc_index].st_table_index = sct_index;
              }
              
              break;
            }
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
          if(BT[i].sat_counter < 5) {
            BT[i].sat_counter = 0;
          } else {
            BT[i].sat_counter -= 5;
          }
        }
        branch_inst_count = 0;
      } else {
        branch_inst_count++;
      }
      
      uint64_t target_pc = 0xfffff0d8f288;
      // print the branch table for target_pc
      // if((pc == target_pc)) {
      //   // print the entire instruction
      //   std::cout << "Seq no: " << seq_no << " PC: 0x" << std::hex << pc << std::dec << " | " << _exec_info << std::endl;
      //   //if(_resolve_dir != pred_dir) {
      //     std::cout << "Misprediction: " << " | Resolve Dir: " << _resolve_dir << " | Pred Dir: " << pred_dir << std::endl;
      //   //}
      // }
    }
//He's directly storing predicted values of 0 and not 0, instead of values as well. 
    if(_exec_info.dec_info.insn_class == InstClass::storeInstClass) {
      uint64_t src_reg_data_idx = _exec_info.dec_info.src_reg_info[1];
      uint64_t dest_val = RegFile[src_reg_data_idx];

      uint64_t addr = _exec_info.mem_va.value();
      uint64_t addr_index = addr & 0xFFFF;
      // check if address is in store table with chain made
      if(ST[addr_index].is_valid && (ST[addr_index].pc == pc) && ST[addr_index].link_made) {
	  ST[addr_index].value = dest_val;
          ST[addr_index].is_zero = dest_val == 0;
          ST[addr_index].predict_taken = ST[addr_index].is_zero;  //this is wrong need to decide based on where to place predict taken
	  //ST[addr_index].predict_taken = BT[pc_index].direction_matched ? ST[addr_index].is_zero : !(ST[addr_index].is_zero);
      } else {
        ST[addr_index].is_valid = true;
        ST[addr_index].pc = pc;
	ST[addr_index].value = dest_val;
        ST[addr_index].is_zero = dest_val == 0;
        ST[addr_index].predict_taken = false;
        ST[addr_index].link_made = false;
      }

      uint64_t stc_index = (ST[addr_index].pc) & 0xFFFF;
      uint64_t tag = (pc & 0xFFF0000) >> 16;
//For now all in ST are in STC vin
      if(SCT[stc_index].is_valid && (SCT[stc_index].tag == tag)) {
	SCT[stc_index].value = dest_val;
	SCT[stc_index].predict_taken = (dest_val == 0); ////this is wrong need to decide based on where to place predict taken
        //SCT[stc_index].predict_taken = BT[pc_index].direction_matched ? dest_val == 0 : !(dest_val == 0);
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

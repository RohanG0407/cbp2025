#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <unordered_set>
#include <cmath>
#include "my_cond_branch_predictor.h"

uint64_t hash(const uint64_t ip_num, uint64_t index_width) {
    //return ip_num; // Don't  hash. Use the full address.

    uint64_t mask = (1 << index_width) - 1;

    uint64_t remaining_num = ip_num >> 3; // Ignore the lower 3 bits
    uint64_t hashed_num = 0;
    while (remaining_num != 0) {
        hashed_num = hashed_num ^ (remaining_num & mask);
        remaining_num = remaining_num >> index_width;
    }

    return hashed_num;
}


uint16_t Trigger_Buffer::get_dst (uint64_t pc) {
    for (int i = 0; i < num_entries; i++) {
        if (trigger_buffer[i].valid && trigger_buffer[i].pc == pc)
            return trigger_buffer[i].dst_uid;
    }
    return 0xFFFF;  // Using this to indicate that match is not found
}

Trigger_Buffer::Trigger_Buffer (uint16_t num_entries) {
    this->num_entries = num_entries;
    trigger_buffer = new Trigger_Buffer_Entry[num_entries];
    wr_ptr = 0;

    // Mark all entries as invalid
    for (int i = 0; i < num_entries; i++) {
        trigger_buffer[i].valid = false;
    }
}

Trigger_Buffer::~Trigger_Buffer () {
    if (trigger_buffer)
        delete[] trigger_buffer;
}

void Trigger_Buffer::add_entry (uint64_t pc, uint16_t dst_uid) {
    uint16_t existing_dst_uid = get_dst(pc);
    if (existing_dst_uid != dst_uid) {
        // Entry does not exist for this dst_uid AND pc. Add it
        // Don't invalidate any existing entry. A single store can trigger multiple DFGs
        trigger_buffer[wr_ptr].valid = true;
        trigger_buffer[wr_ptr].pc = pc;
        trigger_buffer[wr_ptr].dst_uid = dst_uid;
        trigger_buffer[wr_ptr].active_src = true;

        wr_ptr++;
        wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
    }
}

void Trigger_Buffer::trigger (uint64_t pc, uint64_t data, Reservation_Station& rs, Prediction_Table& pred_table) {
    for (int i = 0; i < num_entries; i++) {
        if (trigger_buffer[i].valid && trigger_buffer[i].pc == pc && trigger_buffer[i].active_src) {
            if (DEBUG_MODE) std::cout << "Broadcast: Set off pre-computation of DFG from pc: 0x" << std::hex << pc << " | value: 0x" << data << "\n";

            rs.receive_broadcast(trigger_buffer[i].dst_uid, data);
            pred_table.receive_broadcast(trigger_buffer[i].dst_uid, data);
        }
    }
}

void Trigger_Buffer::printState (bool DEBUG_MODE) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Trigger Buffer:\n";
    for (int i = 0; i < num_entries; i++) {
        if (trigger_buffer[i].valid)
            std::cout << "PC: 0x" << std::hex << trigger_buffer[i].pc
                    << " | dst_uid: " << std::dec << trigger_buffer[i].dst_uid
                    << " | active_src: " << trigger_buffer[i].active_src << "\n";
    }
}


Reservation_Station::Reservation_Station (uint16_t num_entries) {
    this->num_entries = num_entries;
    RS = new Reservation_Station_Entry[num_entries];
    wr_ptr = 0;

    for (int i = 0; i < num_entries; i++) {
        RS[i].valid = false;
        for (Source_Field src: RS[i].src_info) {
            src.valid = false;
        }
    }
}

Reservation_Station::~Reservation_Station () {
    if (RS)
        delete[] RS;
}

void Reservation_Station::add_entry (const uint16_t dst_uid, const InstClass instr_class, const std::vector<uint16_t> src_uid_vector, const std::vector<uint64_t> src_reg_value_vector) {
    Reservation_Station_Entry& entry = RS[wr_ptr];
    entry.valid = true;
    entry.opcode = instr_class;
    entry.dest_uid = dst_uid;
    for (int i = 0; i < src_uid_vector.size(); i++) {
        entry.src_info[i].valid = true;
        entry.src_info[i].src_uid = src_uid_vector[i];
        entry.src_info[i].value = src_reg_value_vector[i];
        entry.src_info[i].updated = false;
    }
    entry.any_update = false;

    wr_ptr++;
    wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
}

// Wrapper around above function, to support a simpler syntax for single source dependencies.
void Reservation_Station::add_entry (const uint16_t dst_uid, const InstClass instr_class, const uint16_t src_uid, const uint64_t src_value) {
    std::vector<uint16_t> src_uid_vector = { src_uid };
    std::vector<uint64_t> src_value_vector = { src_value };
    add_entry (dst_uid, instr_class, src_uid_vector, src_value_vector);
}

void Reservation_Station::receive_broadcast(uint16_t src_uid, uint64_t value) {
    bool any_update_inEntry;
    bool any_update_inTable = false;

    for (int i = 0; i < num_entries; i++) {
        if (!RS[i].valid)
            continue;

        any_update_inEntry = false;

        for (Source_Field& src: RS[i].src_info) {
            if (src.valid && src.src_uid == src_uid) {
                src.value = value;
                src.updated = true;
                any_update_inEntry = true;
                any_update_inTable = true;
            }
        }
        RS[i].any_update |= any_update_inEntry; // If this flag is set, do not un-set it just because none of its source was updated in this cycle.
    }

    if (any_update_inTable)
        printState_updated(DEBUG_MODE);
}

void Reservation_Station::evaluate(Prediction_Table& pred_table) {
    // Pick any one entry which has an update, and evaluate it
    for (int i = num_entries-1; i >= 0; i--) {
        if (!RS[i].valid)
            continue;

        uint64_t output_value;
        if (RS[i].any_update) {
            // No idea what this instruction is. Just pick the updated value and pass it on as output.
            for (Source_Field& src: RS[i].src_info) {
                if(src.valid && src.updated) {
                    output_value = src.value;
                    src.updated = false;
                }
            }

            RS[i].any_update = false;
            uint16_t dst_uid = RS[i].dest_uid;
            if (DEBUG_MODE) std::cout << "Broadcast: Evaluated RS entry: " << std::dec << i << "\n";
            receive_broadcast(dst_uid, output_value);
            pred_table.receive_broadcast(dst_uid, output_value);
            break;
        }
    }
}

void Reservation_Station::printState(bool DEBUG_MODE) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Reservation Station:\n";
    for (int i = 0; i < num_entries; i++) {
        if (RS[i].valid)
            printState_line(i);
    }
}

void Reservation_Station::printState(bool DEBUG_MODE, uint16_t uid) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Reservation Station:\n";
    bool any_entryForUID = false;
    for (int i = 0; i < num_entries; i++) {
        if (RS[i].valid and RS[i].dest_uid == uid) {
            any_entryForUID = true;
            printState_line(i);
        }
    }

    if (!any_entryForUID)
        std::cout << "\tNo valid lines for dst_uid: " << std::dec << uid << "\n";
}

void Reservation_Station::printState_updated(bool DEBUG_MODE) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Reservation Station:\n";
    for (int i = 0; i < num_entries; i++) {
        if (RS[i].valid and RS[i].any_update)
            printState_line(i);
    }
}

void Reservation_Station::printState_line(uint16_t index) {
    Reservation_Station_Entry entry = RS[index];
    std::cout << "\tdst_uid: " << std::dec << entry.dest_uid
            << " | Opcode: " << cInfo[static_cast<uint8_t>(entry.opcode)]
            << " | any_update: " << entry.any_update;
    for (Source_Field src: entry.src_info) {
        if (src.valid)
            std::cout << " || src_uid: " << std::dec << src.src_uid
                    << " | src_value: 0x" << std::hex << src.value;
    }
    std::cout << "\n";
}


Prediction_Table::Prediction_Table (uint16_t num_entries) {
    this->num_entries = num_entries;
    wr_ptr = 0;
    
    prediction_table = new PredictionTable_Entry[num_entries];

    for (int i = 0; i < num_entries; i++) {
        prediction_table[i].valid = false;
    }
}

Prediction_Table::~Prediction_Table() {
    if (prediction_table)
        delete[] prediction_table;
}

bool Prediction_Table::is_learnt(uint64_t pc) {
    // Check if an entry already exists.
    for (int i = 0; i < num_entries; i++) {
        auto entry = prediction_table[i];
        if (entry.valid && entry.pc == pc)
            return true;
    }
    return false;
}

bool Prediction_Table::trcSim_is_predicted(uint64_t pc) {
    for (int i = 0; i < num_entries; i++) {
        auto entry = prediction_table[i];
        if (entry.valid && entry.pc == pc)
            return !entry.trcSim_infeasible;
    }
    return false;
}

void Prediction_Table::add_entry(uint64_t pc, uint16_t src_uid, uint64_t value) {
    // Check if an entry already exists. If it does then invalidate it
    for (int i = 0; i < num_entries; i++) {
        PredictionTable_Entry& entry = prediction_table[i];
        if (entry.valid && entry.pc == pc)
            entry.valid = false;
    }

    prediction_table[wr_ptr].pc = pc;
    prediction_table[wr_ptr].valid = 1;
    prediction_table[wr_ptr].src_uid = src_uid;
    prediction_table[wr_ptr].trcSim_zero_val = (value == 0);
    //prediction_table[wr_ptr].brnz = false;  // This should ideally be trivial to know but is not possible in this trace based simulator.
    prediction_table[wr_ptr].trcSim_infeasible = false;

    wr_ptr++;
    wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
}

void Prediction_Table::learn_branch_type(uint64_t pc, bool taken) {
    for (int i = 0; i < num_entries; i++) {
        PredictionTable_Entry& entry = prediction_table[i];
        if (entry.valid && entry.pc == pc)
            entry.brnz = entry.trcSim_zero_val ^ taken;
    }
}

void Prediction_Table::receive_broadcast(uint16_t src_uid, uint64_t value) {
    bool any_update_inTable = false;

    for (int i = 0; i < num_entries; i++) {
        if (!prediction_table[i].valid)
            continue;

        if(prediction_table[i].src_uid == src_uid) {
            prediction_table[i].trcSim_zero_val == (value == 0);
            prediction_table[i].prediction = (value == 0) ^ prediction_table[i].brnz;
            any_update_inTable = true;
        }
    }

    if (any_update_inTable)
        printState(DEBUG_MODE);
}

bool Prediction_Table::get_prediction(uint64_t pc) {
    for (int i = 0; i < num_entries; i++) {
        PredictionTable_Entry& entry = prediction_table[i];
        if (entry.valid && entry.pc == pc)
            return entry.prediction;
    }
    return true; // dummy value returned. You shouldn't be using this
}

void Prediction_Table::printState(bool DEBUG_MODE) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Prediction Table:\n";
    for (int i = 0; i < num_entries; i++) {
        if (prediction_table[i].valid)
            std::cout << "\tBr. PC: 0x" << std::hex << prediction_table[i].pc
                        << " | Src. uid: " << std::dec << prediction_table[i].src_uid
                        << " | Pred: " << prediction_table[i].prediction
                        << " | BRnz: " << prediction_table[i].brnz
                        << " | Zero_value: " << prediction_table[i].trcSim_zero_val
                        << " | Infeasible: " << prediction_table[i].trcSim_infeasible << "\n";
    }
}

void Prediction_Table::trcSim_infeasible (uint16_t src_uid) {
    for (int i = 0; i < num_entries; i++) {
        if (prediction_table[i].valid && prediction_table[i].src_uid == src_uid)
        prediction_table[i].trcSim_infeasible = true;
    }
}


// uint64_t Producer_Consumer_Pairs_Memory::hash(const uint64_t ip_num) {
//     return ip_num; // Don't  hash. Use the full address.

//     uint64_t index_width = static_cast<int>(std::log2(num_entries));
//     uint64_t mask = (1 << index_width) - 1;

//     uint64_t remaining_num = ip_num >> 3; // Ignore the lower 3 bits
//     uint64_t hashed_num = 0;
//     while (remaining_num != 0) {
//         hashed_num = hashed_num ^ (remaining_num & mask);
//         remaining_num = remaining_num >> index_width;
//     }

//     return hashed_num;
// }

Producer_Consumer_Pairs_Memory::Producer_Consumer_Pairs_Memory (uint16_t num_entries, uint64_t hashedVA_width) {
    this->num_entries = num_entries;
    this->hashedVA_width = hashedVA_width;
    ProdCons_Table = new ProducerConsumer_Entry_Memory[num_entries];
    wr_ptr = 0;
    
    // Mark all entries as invalid
    for (int i = 0; i < num_entries; i++) {
        ProdCons_Table[i].store_observed = false;
        ProdCons_Table[i].prodCons_info.valid_pair = false;
    }
}

Producer_Consumer_Pairs_Memory::~Producer_Consumer_Pairs_Memory () {
    if (ProdCons_Table)
        delete[] ProdCons_Table;
}

bool Producer_Consumer_Pairs_Memory::is_tracked (uint64_t virt_addr) {
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width))
            return true;
    }
    return false;
}

bool Producer_Consumer_Pairs_Memory::is_traced (uint64_t virt_addr) {
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width) && ProdCons_Table[i].prodCons_info.valid_pair)
            return true;
    }
    return false;
}

void Producer_Consumer_Pairs_Memory::add_memVA (uint64_t virt_addr, uint64_t consumer_pc) {
    ProdCons_Table[wr_ptr].virtual_address = hash(virt_addr, hashedVA_width);
    ProdCons_Table[wr_ptr].prodCons_info.consumer_pc = consumer_pc;
    ProdCons_Table[wr_ptr].prodCons_info.valid_pair = false;

    wr_ptr++;
    wr_ptr = (wr_ptr == num_entries) ? 0 : wr_ptr;
}

void Producer_Consumer_Pairs_Memory::record_producer(uint64_t virt_addr, uint64_t producer_pc) {
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width)){
            ProdCons_Table[i].store_observed = true;
            ProdCons_Table[i].prodCons_info.producer_pc = producer_pc;
            ProdCons_Table[i].prodCons_info.valid_pair = false;
            break;  // Each memory address can have atmost one entry
        }
    }
}

void Producer_Consumer_Pairs_Memory::record_consumer(uint64_t virt_addr, uint64_t consumer_pc) {
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width)) {
            ProdCons_Table[i].prodCons_info.consumer_pc = consumer_pc;
            /* The pair isn't valid if store is never ovserved
                    This additional flag wasn't required for registers because every register is tracked throughout the program ...
                    ... but memory is only tracked after a load is observed. A store may never happen to that memory again.
            */
            ProdCons_Table[i].prodCons_info.valid_pair = true & ProdCons_Table[i].store_observed;
            break;  // Each memory address can have atmost one entry
        }
    }
}

uint64_t Producer_Consumer_Pairs_Memory::get_producerPC (uint64_t virt_addr) {
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width) && ProdCons_Table[i].prodCons_info.valid_pair) {
            return ProdCons_Table[i].prodCons_info.producer_pc;
        }
    }
    return 0xFFFFFFFFFFFFFFFF; // this should never be returned becuase the function should only be called after verifying that the virt_addr is_tracked.
}

void Producer_Consumer_Pairs_Memory::printState (bool DEBUG_MODE, uint64_t virt_addr) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Producer Consumer Table - Memory:\n";

    bool is_tracked = false;
    for (int i = 0; i < num_entries; i++) {
        if (ProdCons_Table[i].virtual_address == hash(virt_addr, hashedVA_width)) {
            std::cout << "VA: 0x" << std::hex << virt_addr
                    << " | hash: 0x" << hash(virt_addr, hashedVA_width)
                    << " | Prod. PC: 0x" << ProdCons_Table[i].prodCons_info.producer_pc
                    << " | Cons. PC: 0x" << ProdCons_Table[i].prodCons_info.consumer_pc
                    << " | Valid: " << std::dec << ProdCons_Table[i].prodCons_info.valid_pair
                    << " | Store Observed?: "<< ProdCons_Table[i].store_observed << "\n";
            is_tracked = true;
            break;  // Each memory address can have atmost one entry
        }
    }

    if (!is_tracked) {
        std::cout << "\t Not tracking VA: " << std::hex << virt_addr
                << " | hash: 0x" << hash(virt_addr, hashedVA_width) << "\n";
    }
}


Producer_Consumer_Pairs_Register::Producer_Consumer_Pairs_Register (uint16_t num_entries) {
    ProdCons_Table = new ProducerConsumer_Entry[num_entries];

    // Mark all entries as invalid
    for (int i = 0; i < num_entries; i++) {
        ProdCons_Table[i].valid_pair = false;
    }
}

Producer_Consumer_Pairs_Register::~Producer_Consumer_Pairs_Register () {
    if (ProdCons_Table)
        delete[] ProdCons_Table;
}

uint64_t Producer_Consumer_Pairs_Register::get_producerPC (uint8_t src_reg) {
    return ProdCons_Table[src_reg].producer_pc;
}

void Producer_Consumer_Pairs_Register::record_producer(uint8_t src_reg, uint64_t pc) {
    ProdCons_Table[src_reg].producer_pc = pc;
    ProdCons_Table[src_reg].valid_pair = false;
}

void Producer_Consumer_Pairs_Register::record_consumer(uint8_t dst_reg, uint64_t pc) {
    ProdCons_Table[dst_reg].consumer_pc = pc;
    ProdCons_Table[dst_reg].valid_pair = true;
}

void Producer_Consumer_Pairs_Register::printState(bool DEBUG_MODE, uint8_t reg) {
    if (!DEBUG_MODE)
        return;

    std::cout << "\tr" << std::dec << unsigned(reg)
            << " | Producer - 0x" << std::hex << ProdCons_Table[reg].producer_pc
            << " | Consumer - 0x" << std::hex << ProdCons_Table[reg].consumer_pc
            << " | Valid - " << ProdCons_Table[reg].valid_pair << "\n";
}


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


PC_UID_Map::PC_UID_Map (uint16_t num_entries) {
    this->num_entries = num_entries;

    PC_UID_Map_Table = new PC_UID_Map_Entry[num_entries];

    // Mark all entries as invalid
    for (int i = 0; i < num_entries; i++) {
        PC_UID_Map_Table[i].valid = false;
    }

    next_uid = 0;
}

PC_UID_Map::~PC_UID_Map () {
    if (PC_UID_Map_Table)
        delete[] PC_UID_Map_Table;
}

uint16_t PC_UID_Map::get_uid(uint64_t pc) {
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


void Seeker_Buffer::insert(uint64_t elem) {
    seeker_buffer.insert(elem);
}

void Seeker_Buffer::remove(uint64_t elem) {
    seeker_buffer.erase(elem);
}

bool Seeker_Buffer::exists(uint64_t elem) {
    return (seeker_buffer.count(elem) != 0);
}

void Seeker_Buffer::printState(bool DEBUG_MODE) {
    if (!DEBUG_MODE)
        return;

    std::cout << "Seeker Buffer State:\n";
    for (uint64_t pc : seeker_buffer) {
        std::cout << "\t0x" << std::hex << std::uppercase << pc << std::endl;
    }
}

Store_Table::Store_Table (uint64_t num_entries) {
    this->num_entries = num_entries;
    this->index_width = static_cast<int>(std::log2(num_entries));
    ST_Table = new Store_Table_Entry[num_entries];
    
    // Mark all entries as invalid
    for (int i = 0; i < num_entries; i++) {
        ST_Table[i].valid = false;
    }
}

Store_Table::~Store_Table () {
    if (ST_Table)
        delete[] ST_Table;
}

void Store_Table::record (uint64_t memVA, uint64_t pc) {
    uint64_t index = hash(memVA, index_width);
    ST_Table[index].valid = true;
    ST_Table[index].producer_pc = pc;
}

bool Store_Table::is_recorded(uint64_t memVA) {
    uint64_t index = hash(memVA, index_width);
    return ST_Table[index].valid;
}

uint64_t Store_Table::get_pc(uint64_t memVA) {
    uint64_t index = hash(memVA, index_width);
    return ST_Table[index].producer_pc;
}

// SampleCondPredictor::SampleCondPredictor (void)
// {
// }

// void SampleCondPredictor::setup()
// {
// }

// void SampleCondPredictor::terminate()
// {
// }

// // sample function to get unique instruction id
// uint64_t SampleCondPredictor::get_unique_inst_id(uint64_t seq_no, uint8_t piece) const
// {
//     assert(piece < 16);
//     return (seq_no << 4) | (piece & 0x000F);
// }

// bool SampleCondPredictor::predict (uint64_t seq_no, uint8_t piece, uint64_t PC, bool tage_pred)
// {
//     active_hist.tage_pred = tage_pred;
//     // checkpoint current hist
//     pred_time_histories.emplace(get_unique_inst_id(seq_no, piece), active_hist);
//     bool pred_taken = predict_using_given_hist(seq_no, piece, PC, active_hist, true/*pred_time_predict*/);
//     return pred_taken;
// }

// bool SampleCondPredictor::predict_using_given_hist (uint64_t seq_no, uint8_t piece, uint64_t PC, const SampleHist& hist_to_use, bool pred_time_predict)
// {
//     return hist_to_use.tage_pred;
// }

// void SampleCondPredictor::history_update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool taken, uint64_t nextPC)
// {
//     active_hist.ghist = active_hist.ghist << 1;
//     if(taken)
//     {
//         active_hist.ghist |= 1;
//     }
// }

// void SampleCondPredictor::update (uint64_t seq_no, uint8_t piece, uint64_t PC, bool resolveDir, bool predDir, uint64_t nextPC)
// {
//     const auto pred_hist_key = get_unique_inst_id(seq_no, piece);
//     const auto& pred_time_history = pred_time_histories.at(pred_hist_key);
//     update(PC, resolveDir, predDir, nextPC, pred_time_history);
//     pred_time_histories.erase(pred_hist_key);
// }

// void SampleCondPredictor::update (uint64_t PC, bool resolveDir, bool pred_taken, uint64_t nextPC, const SampleHist& hist_to_use)
// {
// }

// =================
// Predictor End
// =================

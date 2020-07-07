#include "focused_vp.h"
#include <cinttypes>
#include <cstdio>
#include <algorithm>
#include <cassert>
#include <iostream>

MyPredictor predictor;
TraceInfo trace;

bool getPrediction(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t &predicted_value) 
{

    //std::cout << "GetPrediction called for PC = " << std::hex << pc << std::endl;

	// Allocate instruction into traceInfo
	trace.info[seq_no].pc = pc;

	// Check VT and predict if the confidence is saturated
	
	uint8_t vtIdx = pc % VT_SIZE;

	if(predictor.VT.entry[vtIdx].tag == pc) {
       // std::cout << "GetPrediction for PC = " << std::hex << pc << std::dec << " hit in VT with confidence = " << predictor.VT.entry[vtIdx].confidence << std::endl;
		
        if(predictor.VT.entry[vtIdx].confidence == 8 && predictor.VT.entry[vtIdx].no_predict != 4) {
			predicted_value = predictor.VT.entry[vtIdx].data;
            //std::cout << "Prediction Taken" << std::endl;
			return 1;
		}
	}

	return 0;
}

void speculativeUpdate(uint64_t seq_no,			  // dynamic micro-instruction # (starts at 0 and increments indefinitely)
					   bool eligible,			  // true: instruction is eligible for value prediction. false: not eligible.
					   uint8_t prediction_result, // 0: incorrect, 1: correct, 2: unknown (not revealed)
					   // Note: can assemble local and global branch history using pc, next_pc, and insn.
					   uint64_t pc,
					   uint64_t next_pc,
					   InstClass insn,
					   uint8_t piece,
					   // Note: up to 3 logical source register specifiers, up to 1 logical destination register specifier.
					   // 0xdeadbeef means that logical register does not exist.
					   // May use this information to reconstruct architectural register file state (using log. reg. and value at updatePredictor()).
					   uint64_t src1,
					   uint64_t src2,
					   uint64_t src3,
					   uint64_t dst) 
{

    //std::cout << "SpeculativeUpdate called for PC = " << std::hex << pc << std::endl;

	// seq no: instr_id
	// eligible: True if insn = loadInstClass, else false.
	// prediction result: same as described
	// piece : ????
	// src1, src2, src3, dst: All source and destination specifiers, 0xdeadbeef if DNE

	// Add the remaining information to TraceInfo
	assert(trace.info[seq_no].pc == pc);
	trace.info[seq_no].next_pc = next_pc;
	trace.info[seq_no].insttype = insn;
	trace.info[seq_no].src1 = src1;
	trace.info[seq_no].src2 = src2;
	trace.info[seq_no].src3 = src3;
	trace.info[seq_no].dst = dst;
	trace.info[seq_no].prediction_result = prediction_result; 

	/*
	RAT : Update the RAT for the destination register with the current pc
	*/

	predictor.RAT.entry[dst].pc = pc;


	/*
	VT : If the instruction hits in VT, update the confidence and utility metrics according to the prediction_result
	*/

	uint8_t vtIdx = pc % VT_SIZE;
	if(predictor.VT.entry[vtIdx].tag == pc) {
		if(prediction_result == 1) {
			predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
			predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
			predictor.VT.entry[vtIdx].no_predict = 0;
            //std::cout << "confidence and utility incremented" << std::endl;
		}
		else if(prediction_result == 0) {
			predictor.VT.entry[vtIdx].confidence = 0;
			predictor.VT.entry[vtIdx].utility = 0;
			predictor.VT.entry[vtIdx].no_predict++;
		}
	}

}

void updatePredictor(uint64_t seq_no,		// dynamic micro-instruction #
					 uint64_t actual_addr,	// load or store address (0xdeadbeef if not a load or store instruction)
					 uint64_t actual_value, // value of destination register (0xdeadbeef if instr. is not eligible for value prediction)
					 uint64_t actual_latency,
					 bool critical)			// If distance from ROB head during execution was less than the retire width of the processor.


{

	// Retrieve instruction information from the traceInfo

	uint64_t pc = trace.info[seq_no].pc;
	bool eligible = (trace.info[seq_no].insttype == loadInstClass);
	uint64_t src1 = trace.info[seq_no].src1;
	uint64_t src2 = trace.info[seq_no].src2;
	uint64_t src3 = trace.info[seq_no].src3;
	uint8_t prediction_result = trace.info[seq_no].prediction_result;

    //std::cout << "UpdatePredictor called for PC = " << std::hex << pc << " critical = " << std::dec << critical << std::endl;

	/*
	VT : If the instruction hits in the VT, then update the confidence and utility metrics, and the values stored
	*/

	uint8_t vtIdx = pc % VT_SIZE;

	if(predictor.VT.entry[vtIdx].tag == pc) {

        //std::cout << "PC = " << std::hex << pc << " hit in VT at idx = " << std::dec << vtIdx << std::endl;

		if(prediction_result == 2) {	// Mostly means value was not predicted. Still, depending on the actual value, update the confidence and utility
			if(predictor.VT.entry[vtIdx].data == actual_value) {	// The value hasn't changed
				predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
				predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
                predictor.VT.entry[vtIdx].no_predict = 0;
                //std::cout << "confidence and utility incremented" << std::endl;
			}
			else {		// The value has changed
				predictor.VT.entry[vtIdx].confidence = 0;
				predictor.VT.entry[vtIdx].utility = 0;
				predictor.VT.entry[vtIdx].no_predict++;
				predictor.VT.entry[vtIdx].data = actual_value;
			}
		}
		else {		// Means prediction was taken. Confidence and utility already updated in speculativeUpdate. Just update the data
			predictor.VT.entry[vtIdx].data = actual_value;
		}

	}
    else {

        /* 
        LT : If the instruction hits in the LT, then move it to the VT if its a load. If it's not a load, then look up the src instructions and
        add them to the LT. In either case, remove the entry from the LT.
        */

        uint8_t ltIdx = pc % LT_SIZE;
        if(predictor.LT.entry[ltIdx].pc == pc) {
            migrateLTtoVT(ltIdx, seq_no, eligible, actual_value);
        }


        /*
        CIT : a) if(critical) then index into CIT with PC. If slot is empty, enter with confidence=0 & utility=4. If slot is filled with a
                different PC, then decrement the utility of that entry by 1. If utility is 0, evict the old entry and enter the current one. If
                slot already has the same PC, then increment confidence by 1 until saturation (at 4, as 2-bit confidence). If confidence saturates,
                then migrate the CIT entry to the VT.
        */

        if(critical) {
            // Hash into CIT with PC
            uint8_t citIdx = pc % CIT_SIZE;

            // If slot is empty
            if(predictor.CIT.entry[citIdx].tag == 0xdeadbeef) {
                // Enter the current pc into the CIT
                predictor.CIT.entry[citIdx].set(pc, 0, 4);	// Utility is saturated initially
                //std::cout << "PC = " << std::hex << pc << std::dec << " filled in CIT Empty slot idx = " << citIdx << " confidence = " << predictor.CIT.entry[citIdx].confidence << std::endl;
            }
            // If slot already contains the current pc
            else if(predictor.CIT.entry[citIdx].tag == pc) {
                predictor.CIT.entry[citIdx].confidence = std::min(predictor.CIT.entry[citIdx].confidence+1,4);
                //std::cout << "PC = " << std::hex << pc << std::dec << " exists in CIT slot idx = " << citIdx << " confidence = " << predictor.CIT.entry[citIdx].confidence << std::endl;

                if(predictor.CIT.entry[citIdx].confidence == 4) {	// Confidence has saturated
                    // Try to migrate the entry to VT
                    assert(predictor.VT.entry[vtIdx].tag != pc);

                    bool migration_complete = migrateCITtoVT(citIdx, eligible, actual_value);

                    /*
                    LT : While migrating the critical instruction to the VT, lookup its source PCs and add them to the LT
                    */
                    addParentsToLT(src1, src2, src3);
                }
            }
            // Otherwise the slot contains a different PC
            else { 		
                predictor.CIT.entry[citIdx].utility--;
                //std::cout << "PC = " << std::hex << pc << std::dec << " clashes in CIT slot idx = " << citIdx << " new utility = " << predictor.CIT.entry[citIdx].utility << std::endl;

                // If utility is 0, it can be evicted and current pc can be added
                if(predictor.CIT.entry[citIdx].utility == 0) {
                    predictor.CIT.entry[citIdx].set(pc, 0, 4);	// Utility is saturated initially
                    //std::cout << "PC = " << std::hex << pc << " replaced old PC = " << predictor.CIT.entry[citIdx].tag << std::dec << " in CIT slot idx = " << citIdx << " confidence = " << predictor.CIT.entry[citIdx].confidence << std::endl;
                }
            }
        }
    }
    return;
}

void beginPredictor(int argc_other, char **argv_other)
{
    return;
}

void endPredictor() 
{
    return;
}

bool migrateCITtoVT(uint8_t citIdx, bool eligible, uint64_t actual_value) {

	uint64_t pc = predictor.CIT.entry[citIdx].tag;
	uint8_t vtIdx = pc % VT_SIZE;

    //std::cout << "PC = " << std::hex << pc << std::dec << " Migrated from CIT to VT" << std::endl;
    
	assert(predictor.VT.entry[vtIdx].tag != pc);

	// Try inserting into the VT. Will be successful if either the slot is empty or the utility of existing entry has become 0.

	bool migration_complete = false;
	if(predictor.VT.entry[vtIdx].tag == 0xdeadbeef) {	// VT entry is empty
		predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, eligible?0:4); // Utility is saturated initially, Initialize to "not predictable" if not eligible
		migration_complete = true;
	}
	else {	// VT entry contains some other PC. It cannot contain current PC as specified by assert statement above.
		predictor.VT.entry[vtIdx].utility--; // Decrement utility
		if(predictor.VT.entry[vtIdx].utility == 0) {	// Utility has dropped to 0 so it can be evicted
			predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, eligible?0:4); // Utility is saturated initially, Initialize to "not predictable" if not eligible
			migration_complete = true;
		}
	}

	if(migration_complete) {	// If entry successfully sent to VT then evict from CIT
		predictor.CIT.entry[citIdx].clear();
	}

	return migration_complete;
}

bool migrateLTtoVT(uint8_t ltIdx, uint64_t seq_no, bool eligible, uint64_t actual_value) {

	uint64_t pc = predictor.LT.entry[ltIdx].pc;
	uint64_t vtIdx = pc % VT_SIZE;

    //std::cout << "PC = " << std::hex << pc << std::dec << " Migrated from LT to VT" << std::endl;

	assert(predictor.VT.entry[vtIdx].tag != pc);

	// Try inserting into the VT. Will be successful if either the slot is empty or the utility of existing entry has become 0.

	bool migration_complete = false;

	if(eligible) {		// Migrate to VT only if its a load
		if(predictor.VT.entry[vtIdx].tag == 0xdeadbeef) {	// VT entry is empty
			predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, 0); // Utility is saturated initially.
			migration_complete = true;
		}
		else {	// VT entry contains some other PC. It cannot contain current PC as specified by assert statement above.
			predictor.VT.entry[vtIdx].utility--; // Decrement utility
			if(predictor.VT.entry[vtIdx].utility == 0) {	// Utility has dropped to 0 so it can be evicted
				predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, 0); // Utility is saturated initially.
				migration_complete = true;
			}
		}
		if(migration_complete) {	// If entry successfully sent to VT then evict from LT
			predictor.LT.entry[ltIdx].pc = 0xdeadbeef;
		}
	}
	else {	// If it's not a load, find the parent instructions and add them to the LT

		// First evict the LT entry
		predictor.LT.entry[ltIdx].pc = 0xdeadbeef;

		// Retrieve src register IDs from TraceInfo
		uint64_t src1 = trace.info[seq_no].src1;
		uint64_t src2 = trace.info[seq_no].src2;
		uint64_t src3 = trace.info[seq_no].src3;

		addParentsToLT(src1, src2, src3);

	}

	return migration_complete;
}


void addParentsToLT(uint64_t src1, uint64_t src2, uint64_t src3) {

	uint64_t src1_pc = predictor.RAT.entry[src1].pc;
	uint64_t src2_pc = predictor.RAT.entry[src2].pc;
	uint64_t src3_pc = predictor.RAT.entry[src3].pc;

	// No utility metrics in LT, so the current parents simply replace the existing instruction in the LT slot. May cause thrashing?

	if(src1_pc != 0xdeadbeef) {
		uint8_t ltIdx1 = src1_pc % LT_SIZE;
		predictor.LT.entry[ltIdx1].pc = src1_pc; 
	}
	if(src2_pc != 0xdeadbeef) {
		uint8_t ltIdx2 = src2_pc % LT_SIZE;
		predictor.LT.entry[ltIdx2].pc = src2_pc; 
	}
	if(src3_pc != 0xdeadbeef) {
		uint8_t ltIdx3 = src3_pc % LT_SIZE;
		predictor.LT.entry[ltIdx3].pc = src3_pc; 
	}
}

int rand16() {

	// rand()%2 returns a random bit. ANDing 4 bits together will give a 1 with 1/16th probability

	return (rand()%2) & (rand()%2) & (rand()%2) & (rand()%2);
}
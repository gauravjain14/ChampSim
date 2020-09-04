#include "focused_vp.h"
#include <cinttypes>
#include <cstdio>
#include <algorithm>
#include <cassert>
#include <iostream>

MyPredictor predictor;
TraceInfo trace;

uint64_t num_getPred = 0;
uint64_t num_specUpd = 0;
uint64_t num_updPred = 0;
uint32_t num_bhr_updates = 0;

void updateBHR(bool taken) {
	num_bhr_updates++;
	predictor.bhr = (predictor.bhr << 1) | taken;
}

bool getPrediction(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t &predicted_value, bool &markForCVP)
{
	num_getPred++;
	
	// Allocate instruction into traceInfo
	trace.info[seq_no].pc = pc;
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	uint8_t branchVTIdx = (pc ^ predictor.bhr) & (VT_SIZE - 1);

	/* The paper says this: If the instruction is not predictable by Last value prediction
	mark it for context value prediction and then after execution record the instruction
	at (pc ^ BHR).
	Q) What do we do if the instruction is not found in the VT at (pc)? We don't bother about
	adding it for Context value prediction, right? */

	if (checkVT(pc, vtIdx)) {
		if (predictor.VT.entry[vtIdx].confidence == 8) { // && predictor.VT.entry[vtIdx].no_predict != 4) {
			predicted_value = predictor.VT.entry[vtIdx].data;
			return true;
		}

		markForCVP = (predictor.VT.entry[vtIdx].no_predict == 4);
	} 

	// if the entry was not predictable by last value prediction:
	// Do we set markForCVP = false or we carry this information so that when we are updating
	// the VT we know that we don't need to bother about checking for the last value prediction entry?

	if (checkVT(pc, branchVTIdx)) {
		if(predictor.VT.entry[branchVTIdx].confidence == 8) { // && predictor.VT.entry[vtIdx].no_predict != 4) {
			//std::cout << "Using context value prediction" << std::endl;
			predicted_value = predictor.VT.entry[branchVTIdx].data;
			return true;
		}
	}

	return false;
}

// do we still need traceInfo?
void populateTraceInfo(uint64_t seq_no,			  // dynamic micro-instruction # (starts at 0 and increments indefinitely)
					   uint8_t prediction_result, // 0: incorrect, 1: correct, 2: unknown (not revealed)
					   uint64_t pc,
					   uint64_t next_pc,
					   InstClass insn,
					   uint64_t src1,
					   uint64_t src2,
					   uint64_t src3,
					   uint64_t dst) {
	
	//assert(trace.info[seq_no].pc == pc);
	/*
	RAT : Update the RAT for the destination register with the current pc
	*/
	trace.info[seq_no].next_pc = next_pc;
	trace.info[seq_no].insttype = insn;
	trace.info[seq_no].src1 = src1;
	trace.info[seq_no].src2 = src2;
	trace.info[seq_no].src3 = src3;
	trace.info[seq_no].dst = dst;
	trace.info[seq_no].prediction_result = prediction_result;
}

void updateRAT(uint64_t pc, uint8_t dst) {
	predictor.RAT.entry[dst].pc = pc;
}

void updateVT(bool critical, // we need this for context-value-prediction
			bool eligible,
			uint64_t pc,
			uint64_t seq_no,
			uint64_t actual_addr,
			uint64_t actual_value,
			uint64_t actual_latency,
			bool markForCVP)
{
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	uint8_t branchVtIdx = ((pc ^ predictor.bhr)) & (VT_SIZE - 1);

	if (markForCVP && eligible) {
		if (checkVT(pc, branchVtIdx)) {
			if (predictor.VT.entry[branchVtIdx].data == actual_value) {
				// we know data is repeating
				predictor.VT.entry[branchVtIdx].confidence = std::min(predictor.VT.entry[branchVtIdx].confidence+rand16(), 8);
				predictor.VT.entry[branchVtIdx].utility = std::min(predictor.VT.entry[branchVtIdx].utility+1, 4);
				predictor.VT.entry[branchVtIdx].no_predict = (predictor.VT.entry[branchVtIdx].confidence == 8) ?
																0 : predictor.VT.entry[branchVtIdx].no_predict;
			} else {
				predictor.VT.entry[branchVtIdx].confidence = 0;
				predictor.VT.entry[branchVtIdx].utility = 0;
				predictor.VT.entry[branchVtIdx].no_predict = std::min(predictor.VT.entry[branchVtIdx].no_predict+1, 4);
				predictor.VT.entry[branchVtIdx].data = actual_value;
			}				
		} else { // add the instruction or decrease the utility
			if (predictor.VT.entry[branchVtIdx].tag == 0xdeadbeef) {
				predictor.VT.entry[branchVtIdx].set(pc, 0, 4, actual_value, 0);
			} else {
				predictor.VT.entry[branchVtIdx].utility = std::max(predictor.VT.entry[vtIdx].utility-1,0); // Decrement utility
				if(predictor.VT.entry[branchVtIdx].utility == 0) {	// Utility has dropped to 0 so it can be evicted
					predictor.VT.entry[branchVtIdx].set(pc, 0, 4, actual_value, 0); // Utility is saturated initially.
				}
			}
		}
	} else if (checkVT(pc, vtIdx) && eligible) {
		if (predictor.VT.entry[vtIdx].data == actual_value) {
			// we know data is repeating
			predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
			predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
			predictor.VT.entry[vtIdx].no_predict = (predictor.VT.entry[vtIdx].confidence == 8) ? 0 : predictor.VT.entry[vtIdx].no_predict;
		} else {
			predictor.VT.entry[vtIdx].confidence = 0;
			predictor.VT.entry[vtIdx].utility = 0;
			predictor.VT.entry[vtIdx].no_predict = std::min(predictor.VT.entry[vtIdx].no_predict+1, 4);
			predictor.VT.entry[vtIdx].data = actual_value;
		}
	} else if (checkLT(pc)) {
		migrateLTtoVT(pc, seq_no, eligible, actual_value);
	}
}

bool addToCIT(uint64_t pc) {
	uint64_t citIdx = pc & (CIT_SIZE - 1);
	uint64_t tag = bitExtract<uint64_t>(pc, (uint8_t)(log2(CIT_SIZE)), 11);
	
	if (predictor.CIT.entry[citIdx].tag == tag) {
		predictor.CIT.entry[citIdx].utility = std::min(predictor.CIT.entry[citIdx].utility+1, 4);
		predictor.CIT.entry[citIdx].confidence = std::min(predictor.CIT.entry[citIdx].confidence+1, 4);

		if (predictor.CIT.entry[citIdx].confidence == 4)
			return true;
	} else {
		if (predictor.CIT.entry[citIdx].utility == 0) {
			predictor.CIT.entry[citIdx].set(tag, 1, 1);
		} else {
			predictor.CIT.entry[citIdx].utility--;
		}
	}

	return false;
}

bool getFromCIT(uint64_t pc) {
	uint64_t citIdx = pc & (CIT_SIZE - 1);
	uint64_t tag = bitExtract<uint64_t>(pc, (uint8_t)(log2(CIT_SIZE)), 11);

	if (predictor.CIT.entry[citIdx].tag == tag) {
		if (predictor.CIT.entry[citIdx].confidence == 4) 
			return true;
	}

	return false;
}

bool checkLT(uint64_t pc) {
	uint8_t ltindex = pc & (LT_SIZE - 1);
	// entry is there in the LT
	return (predictor.LT.entry[ltindex].pc == pc);
}

bool checkVT(uint64_t pc, uint8_t index) {
	// entry is there in the VT
	return (predictor.VT.entry[index].tag == bitExtract<uint64_t>(pc, LOG2_VT_SIZE, VT_TAG_SIZE));
}

void addToLT(uint64_t pc, InstClass type, uint8_t *source_registers, uint32_t num_src_regs) {
    /* When adding to ROB, check if the instruction is already in the learning
    table. If yes, check if it's a load instruction or not and decide whether
    or not we want to go up the relation chain to find the source-generating
    instructions that need to be predicted. Also check the CIT. If critical,
    check the RAT to get the PCs of the source-generating instructions.
    
    Update: If a load instruction is marked not-predictable in the Value Table,
    we add its sources also in the Learning Table.*/
    bool isCritical = getFromCIT(pc);
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	bool isNotPredictableLoad = checkVT(pc, vtIdx) && (predictor.VT.entry[vtIdx].no_predict == 4);

    if (isCritical || (checkLT(pc) && type != InstClass::loadInstClass) ||
        (type == InstClass::loadInstClass && isNotPredictableLoad)) {
        for (int i=0; i<num_src_regs; i++) {
            if (source_registers[i])
				addToLT(source_registers[i]);        
        }
    }
}

void addToLT(uint8_t src_reg) {
	// get the pc from the RAT
	uint64_t pc = predictor.RAT.entry[src_reg].pc;
	if (pc != 0xdeadbeef) {
		uint32_t ltindex = pc & (LT_SIZE-1);

		// we are trying to overwrite an entry in the LT. Is that legal? What about thrashing?
		predictor.LT.entry[ltindex].pc = pc;
	}
}

bool migrateLTtoVT(uint64_t pc, uint64_t seq_no, bool eligible, uint64_t actual_value) {
	uint8_t ltIdx = pc % LT_SIZE;
	uint64_t vtIdx = pc % VT_SIZE;

	/* It is possible that we already have the same instruction in the VT, right?
	We need not bother about non-load instructions in this scenario. But what happens
	when we are updating a previous load instruction?

	Nothing, I guess, because in our case, we have already changed the utility and confidence
	in the speculative update function for Load instructions that are predictable. Right?
	*/

	bool migration_complete = false;
	uint8_t utility = (eligible) ? 4 : 0; // what do we do for non-load instructions? Set them
	uint8_t no_predict = (!eligible) ? 4 : 0;
	if (!checkVT(pc, vtIdx)) {
		if (predictor.VT.entry[vtIdx].tag == 0xdeadbeef) {
			predictor.VT.entry[vtIdx].set(pc, 0, utility, actual_value, no_predict);
		} else {
			predictor.VT.entry[vtIdx].utility = std::max(predictor.VT.entry[vtIdx].utility-1,0); // Decrement utility
			if(predictor.VT.entry[vtIdx].utility == 0) {	// Utility has dropped to 0 so it can be evicted
				predictor.VT.entry[vtIdx].set(pc, 0, utility, actual_value, 0); // Utility is saturated initially.
				migration_complete = true;
			}
		}
	}

	if (migration_complete) {
		// evict entry from LT
		predictor.LT.entry[ltIdx].pc = 0xdeadbeef;
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

void endPredictor() {
	std::cout << "Number of times BHR called " << num_bhr_updates << std::endl;
}

int rand16() {

	// rand()%2 returns a random bit. ANDing 4 bits together will give a 1 with 1/16th probability
	return (rand()%2) & (rand()%2) & (rand()%2) & (rand()%2);
}
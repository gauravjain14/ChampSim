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

bool getPrediction(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t &predicted_value) 
{
	num_getPred++;
    //std::cout << "GetPrediction called for PC = " << std::hex << pc << std::endl;

	// Allocate instruction into traceInfo
	trace.info[seq_no].pc = pc;
	if (checkVT(pc)) {
		uint8_t vtIdx = pc & (VT_SIZE-1);
		if(predictor.VT.entry[vtIdx].confidence == 8) { // && predictor.VT.entry[vtIdx].no_predict != 4) {
			predicted_value = predictor.VT.entry[vtIdx].data;
			return 1;
		}
	}

	return 0;
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
	
	assert(trace.info[seq_no].pc == pc);
	/*
	RAT : Update the RAT for the destination register with the current pc
	*/
	predictor.RAT.entry[dst].pc = pc;
	trace.info[seq_no].next_pc = next_pc;
	trace.info[seq_no].insttype = insn;
	trace.info[seq_no].src1 = src1;
	trace.info[seq_no].src2 = src2;
	trace.info[seq_no].src3 = src3;
	trace.info[seq_no].dst = dst;
	trace.info[seq_no].prediction_result = prediction_result;
}

void updateVT(bool critical, // we need this for context-value-prediction
			bool eligible,
			uint64_t pc,
			uint64_t seq_no,
			uint64_t actual_addr,
			uint64_t actual_value,
			uint64_t actual_latency)
{
	if (checkVT(pc) && eligible) {
		uint8_t vtIdx = pc & (VT_SIZE - 1);
		/* This should cover all the cases whether this VT entry was already making
		prediction or was being given the confidence boost or being penalized */
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

void beginPredictor(int argc_other, char **argv_other)
{
    return;
}

void endPredictor() 
{

	printf("GetPredictions = %ld, SpeculativeUpdates = %ld, UpdatePredictors = %ld", num_getPred, num_specUpd, num_updPred);

    return;
}

bool addToCIT(uint64_t pc) {
	uint64_t citIdx = pc & CIT_SIZE;
	uint64_t tag = (pc >> (uint64_t)(log2(CIT_SIZE))) && 0x7ff; // 11-bit tag
	
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
	uint64_t citIdx = pc & CIT_SIZE;
	uint64_t tag = (pc >> (uint64_t)(log2(CIT_SIZE))) && 0x7ff; // 11-bit tag

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

bool checkVT(uint64_t pc) {
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	// entry is there in the VT
	return (predictor.VT.entry[vtIdx].tag == bitExtract<uint64_t>(pc, LOG2_VT_SIZE, VT_TAG_SIZE));
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
	bool isNotPredictableLoad = checkVT(pc) && (predictor.VT.entry[vtIdx].no_predict == 4);

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
	uint32_t ltindex = pc & (LT_SIZE-1);

	// we are trying to overwrite an entry in the LT. Is that legal? What about thrashing?
	predictor.LT.entry[ltindex].pc = pc;
}

bool migrateCITtoVT(uint8_t citIdx, bool eligible, uint64_t actual_value) {

	uint64_t pc = predictor.CIT.entry[citIdx].tag;
	uint8_t vtIdx = pc % VT_SIZE;

    //std::cout << "PC = " << std::hex << pc << std::dec << " Migrated from CIT to VT" << std::endl;
    
	assert(predictor.VT.entry[vtIdx].tag != pc);

	// Try inserting into the VT. Will be successful if either the slot is empty or the utility of existing entry has become 0.
	bool migration_complete = false;
	if(predictor.VT.entry[vtIdx].tag == 0xdeadbeef) {	// VT entry is empty
		predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, eligible ? 0 : 4); // Utility is saturated initially, Initialize to "not predictable" if not eligible
		migration_complete = true;
	}
	else {	// VT entry contains some other PC. It cannot contain current PC as specified by assert statement above.
		predictor.VT.entry[vtIdx].utility = std::max(predictor.VT.entry[vtIdx].utility-1,0); // Decrement utility
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
	uint8_t utility = (!eligible) ? 4 : 0; // what do we do for non-load instructions? Set them
	uint8_t no_predict = (!eligible) ? 4 : 0;
	if (!checkVT(pc)) {
		if (predictor.VT.entry[vtIdx].tag == 0xdeadbeef) {
			predictor.VT.entry[vtIdx].set(pc, 0, utility, actual_value, no_predict);
		} else {
			predictor.VT.entry[vtIdx].utility = std::max(predictor.VT.entry[vtIdx].utility-1,0); // Decrement utility
			if(predictor.VT.entry[vtIdx].utility == 0) {	// Utility has dropped to 0 so it can be evicted
				predictor.VT.entry[vtIdx].set(pc, 0, 4, actual_value, 0); // Utility is saturated initially.
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

int rand16() {

	// rand()%2 returns a random bit. ANDing 4 bits together will give a 1 with 1/16th probability
	return (rand()%2) & (rand()%2) & (rand()%2) & (rand()%2);
}
#include "focused_vp.h"
#include <cinttypes>
#include <cstdio>
#include <algorithm>
#include <cassert>
#include <iostream>

MyPredictor predictor;
TraceInfo trace;

uint64_t num_getPred = 0;
uint64_t num_lvp = 0;
uint64_t num_cvp = 0;

uint64_t num_lvp_entries = 0;
uint64_t num_lvp_hits = 0;

uint64_t num_cvp_entries = 0;
uint64_t num_cvp_hits = 0;

uint64_t num_upd_bhr = 0;

void updateBHR(bool taken) {
	predictor.bhr = (predictor.bhr << 1) | taken;
	num_upd_bhr++;
}

bool getPrediction(uint64_t seq_no, uint64_t pc, uint8_t piece, uint64_t &predicted_value, bool &lvp_not_cvp) 
{
	num_getPred++;
	
	// Allocate instruction into traceInfo
	trace.info[seq_no].pc = pc;
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	uint8_t branchVTIdx = (pc ^ predictor.bhr) & (VT_SIZE - 1);

	/*
	// use last value prediction first. If not found, check context value prediction
	if (checkVT(pc, vtIdx)) {
		if(predictor.VT.entry[vtIdx].confidence == 8) { // && predictor.VT.entry[vtIdx].no_predict != 4) {
			predicted_value = predictor.VT.entry[vtIdx].data;
			num_lvp++;
			return true;
		}
	} else if (checkVT(pc, branchVTIdx)) {
		if(predictor.VT.entry[branchVTIdx].confidence == 8) { // && predictor.VT.entry[vtIdx].no_predict != 4) {
			predicted_value = predictor.VT.entry[branchVTIdx].data;
			num_cvp++;
			return true;
		}
	}
	*/

	if(checkVT(pc, vtIdx) && predictor.VT.entry[vtIdx].no_predict != 4) {
		if(predictor.VT.entry[vtIdx].confidence == 8) {
			predicted_value = predictor.VT.entry[vtIdx].data;
			lvp_not_cvp = true;
			num_lvp++;
			return true;
		}
	}
	else if(checkVT(pc, branchVTIdx)) {
		if(predictor.VT.entry[branchVTIdx].confidence == 8) {
			predicted_value = predictor.VT.entry[branchVTIdx].data;
			lvp_not_cvp = false;
			num_cvp++;
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
	
	assert(trace.info[seq_no].pc == pc);
	
	//RAT : Update the RAT for the destination register with the current pc
	
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
			bool speculated,
			bool lvp_not_cvp,
			uint64_t pc,
			uint64_t seq_no,
			uint64_t actual_addr,
			uint64_t actual_value,
			uint64_t actual_latency,
			InstClass insttype,
			uint8_t *source_registers, 
			uint32_t num_src_regs)
{

	/*
	uint8_t vtIdx = pc & (VT_SIZE - 1);
	if (checkVT(pc, vtIdx) && eligible) {
		// if the entry is not predictable and critical, use context value prediction
		if (predictor.VT.entry[vtIdx].no_predict == 4 && critical) {
			// XOR the PC with the BHR to get the hash
			uint8_t branchVtIdx = ((pc ^ predictor.bhr)) & (VT_SIZE - 1);
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
			}
		} else {
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
		}
	} else if (checkLT(pc)) {
		migrateLTtoVT(pc, seq_no, eligible, actual_value);
	}
	*/

	uint8_t vtIdx = pc & (VT_SIZE - 1);
	uint8_t branchvtIdx = (pc ^ predictor.bhr) & (VT_SIZE - 1);

	// Do what's needed at the Last Value Prediction slot
	if(checkVT(pc, vtIdx)) {
		num_lvp_hits++;
		if(speculated && lvp_not_cvp) {
			if (predictor.VT.entry[vtIdx].data == actual_value) {
				// we know data is repeating
				predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
				predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
				predictor.VT.entry[vtIdx].no_predict = (predictor.VT.entry[vtIdx].confidence == 8) ? 0 : predictor.VT.entry[vtIdx].no_predict;
			} else {
				// Data has changed, misprediction has happened
				predictor.VT.entry[vtIdx].confidence = 0;
				predictor.VT.entry[vtIdx].utility = 0;
				predictor.VT.entry[vtIdx].no_predict = std::min(predictor.VT.entry[vtIdx].no_predict+1, 4);
				predictor.VT.entry[vtIdx].data = actual_value;
			} 
		} else if(speculated && !lvp_not_cvp) {
			//assert(predictor.VT.entry[vtIdx].no_predict == 4);
		} else if(!speculated) {
			if (predictor.VT.entry[vtIdx].data == actual_value) {
				// we know data is repeating
				predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
				predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
				predictor.VT.entry[vtIdx].no_predict = (predictor.VT.entry[vtIdx].confidence == 8) ? 0 : predictor.VT.entry[vtIdx].no_predict;
			} else {
				// Data has changed, misprediction has happened
				predictor.VT.entry[vtIdx].confidence = 0;
				predictor.VT.entry[vtIdx].utility = 0;
				predictor.VT.entry[vtIdx].no_predict = std::min(predictor.VT.entry[vtIdx].no_predict+1, 4);
				predictor.VT.entry[vtIdx].data = actual_value;
			} 
		}
	}
	
	// Do what's needed at the Context Value Prediction slot
	if(checkVT(pc, branchvtIdx)) {
		num_cvp_hits++;
		if(speculated && !lvp_not_cvp) {
			if (predictor.VT.entry[branchvtIdx].data == actual_value) {
				// we know data is repeating
				predictor.VT.entry[branchvtIdx].confidence = std::min(predictor.VT.entry[branchvtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
				predictor.VT.entry[branchvtIdx].utility = std::min(predictor.VT.entry[branchvtIdx].utility+1, 4);
				predictor.VT.entry[branchvtIdx].no_predict = (predictor.VT.entry[branchvtIdx].confidence == 8) ? 0 : predictor.VT.entry[branchvtIdx].no_predict;
			} else {
				// Data has changed, misprediction has happened
				predictor.VT.entry[branchvtIdx].confidence = 0;
				predictor.VT.entry[branchvtIdx].utility = 0;
				predictor.VT.entry[branchvtIdx].no_predict = std::min(predictor.VT.entry[branchvtIdx].no_predict+1, 4);
				predictor.VT.entry[branchvtIdx].data = actual_value;
			} 
		} else if(speculated && lvp_not_cvp) {
			//assert(checkVT(pc, vtIdx) && predictor.VT.entry[vtIdx].no_predict != 4);
		} else if(!speculated) {
			if (predictor.VT.entry[branchvtIdx].data == actual_value) {
				// we know data is repeating
				predictor.VT.entry[branchvtIdx].confidence = std::min(predictor.VT.entry[branchvtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
				predictor.VT.entry[branchvtIdx].utility = std::min(predictor.VT.entry[branchvtIdx].utility+1, 4);
				predictor.VT.entry[branchvtIdx].no_predict = (predictor.VT.entry[branchvtIdx].confidence == 8) ? 0 : predictor.VT.entry[branchvtIdx].no_predict;
			} else {
				// Data has changed, misprediction has happened
				predictor.VT.entry[branchvtIdx].confidence = 0;
				predictor.VT.entry[branchvtIdx].utility = 0;
				predictor.VT.entry[branchvtIdx].no_predict = std::min(predictor.VT.entry[branchvtIdx].no_predict+1, 4);
				predictor.VT.entry[branchvtIdx].data = actual_value;
			} 
		}
	}

	// Add to the Context Value Prediction slot if it's not predictable by LVP
	if(checkVT(pc, vtIdx) && predictor.VT.entry[vtIdx].no_predict == 4 && !checkVT(pc, branchvtIdx)) {
		bool added_for_cvp = addToVT(pc, branchvtIdx, actual_value, eligible);
		if(added_for_cvp) num_cvp_entries++;
	}

	// Add the LT entry to VT if there is a hit in LT
	if(checkLT(pc) && !checkVT(pc, vtIdx) && !checkVT(pc, branchvtIdx)) {
		bool added_to_vt = addToVT(pc, vtIdx, actual_value, eligible);
		if(added_to_vt) {
			uint8_t ltindex = pc & (LT_SIZE - 1);
			predictor.LT.entry[ltindex].pc = 0xdeadbeef;
			num_lvp_entries++;
		}
	}

	// Update the CIT

	uint8_t citIdx = pc & (CIT_SIZE - 1);

	if(checkCIT(pc) && !checkVT(pc, vtIdx) && !checkVT(pc, branchvtIdx)) {
		if(critical) {
			predictor.CIT.entry[citIdx].utility = std::min(predictor.CIT.entry[citIdx].utility+1, 4);
			predictor.CIT.entry[citIdx].confidence = std::min(predictor.CIT.entry[citIdx].confidence+1, 4);
		}

		if(predictor.CIT.entry[citIdx].confidence == 4) {
			uint8_t vtIdx = pc & (VT_SIZE - 1);
			uint8_t branchvtIdx = (pc ^ predictor.bhr) & (VT_SIZE - 1);

			assert(!checkVT(pc,vtIdx) && !checkVT(pc, branchvtIdx)) ;
			bool added_to_VT = addToVT(pc, vtIdx, actual_value, eligible);
			if(added_to_VT) {
				addToLT(pc, insttype, source_registers, num_src_regs);
				predictor.CIT.entry[citIdx].clear(); // Remove from CIT if it successfully goes to VT	
				num_lvp_entries++;
			}
		}
	} else {
		if(critical) {
			bool added_to_CIT = addToCIT(pc); 
		}
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

bool checkCIT(uint64_t pc) {
	uint8_t citIdx = pc & (CIT_SIZE - 1);
	uint64_t tag = bitExtract<uint64_t>(pc, (uint8_t)(log2(CIT_SIZE)), 11);

	return (predictor.CIT.entry[citIdx].tag == tag);
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
	uint8_t branchvtIdx = (pc ^ predictor.bhr) & (VT_SIZE - 1);
	bool isNotPredictable = checkVT(pc, branchvtIdx) && (predictor.VT.entry[branchvtIdx].no_predict == 4);

    if (isCritical || isNotPredictable) {
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

void updateRAT(uint64_t pc, uint8_t *destination_registers, uint32_t num_dst_regs) {

	for(int i = 0; i < num_dst_regs; i++) {
		predictor.RAT.entry[destination_registers[i]].pc = pc;
	}
}

bool addToVT(uint64_t pc, uint8_t index, uint64_t actual_value, bool eligible) {
	
	assert(!checkVT(pc, index));	// Do not call this function when the entry already exists there

	uint64_t tag = bitExtract<uint64_t>(pc, LOG2_VT_SIZE, VT_TAG_SIZE);

	if(predictor.VT.entry[index].tag == 0xdeadbeef) {	// VT entry is empty
		predictor.VT.entry[index].set(pc, 0, 4, actual_value, eligible?0:4); // Utility is saturated initially, Initialize to "not predictable" if not eligible
		return true;
	}
	else {	// VT entry not empty
		predictor.VT.entry[index].utility = std::max(predictor.VT.entry[index].utility-1,0); // Decrement utility
		if(predictor.VT.entry[index].utility == 0) {	// Utility has dropped to 0 so it can be evicted
			predictor.VT.entry[index].set(pc, 0, 4, actual_value, eligible?0:4); // Utility is saturated initially, Initialize to "not predictable" if not eligible
			return true;
		}
	}

	return false;
}

/*
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
	*/ /*

	bool migration_complete = false;
	uint8_t utility = (!eligible) ? 4 : 0; // what do we do for non-load instructions? Set them
	uint8_t no_predict = (!eligible) ? 4 : 0;
	if (!checkVT(pc, vtIdx)) {
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
*/

/*
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
*/

int rand16() {

	// rand()%2 returns a random bit. ANDing 4 bits together will give a 1 with 1/16th probability
	return (rand()%2) & (rand()%2) & (rand()%2) & (rand()%2);
}

void beginPredictor(int argc_other, char **argv_other)
{
    return;
}

void endPredictor() 
{

	printf("GetPredictions = %ld, LastValuePs = %ld, ContextValuePs = %ld\n", num_getPred, num_lvp, num_cvp);
	printf("LVP --> Entries = %ld, Hits = %ld\n", num_lvp_entries, num_lvp_hits);
	printf("CVP --> Entries = %ld, Hits = %ld\n", num_cvp_entries, num_cvp_hits);
	printf("num_upd_bhr = %ld\n", num_upd_bhr);

    return;
}

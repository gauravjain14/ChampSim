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

	num_specUpd++;

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
	bool pcInVT = checkVT(pc);

	if (insn == InstClass::loadInstClass && pcInVT && (prediction_result != 2)) {
		if (!prediction_result) {
			predictor.VT.entry[vtIdx].confidence = 0;
			predictor.VT.entry[vtIdx].utility = 0;
			predictor.VT.entry[vtIdx].no_predict = std::min(predictor.VT.entry[vtIdx].no_predict+1, 4);
		} else {
			// incrementing confidence is anyways redundant since it'd already be saturated if are making predictions
			predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
			predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
			predictor.VT.entry[vtIdx].no_predict = (predictor.VT.entry[vtIdx].confidence == 8) ? 0 : predictor.VT.entry[vtIdx].no_predict;
		}
	}
}

void updatePredictor(uint64_t seq_no,		// dynamic micro-instruction #
					 uint64_t actual_addr,	// load or store address (0xdeadbeef if not a load or store instruction)
					 uint64_t actual_value, // value of destination register (0xdeadbeef if instr. is not eligible for value prediction)
					 uint64_t actual_latency,
					 bool critical)			// If distance from ROB head during execution was less than the retire width of the processor.
{
	num_updPred++;
	// Retrieve instruction information from the traceInfo
	uint64_t pc = trace.info[seq_no].pc;
	bool eligible = (trace.info[seq_no].insttype == loadInstClass);
	uint64_t src1 = trace.info[seq_no].src1;
	uint64_t src2 = trace.info[seq_no].src2;
	uint64_t src3 = trace.info[seq_no].src3;
	uint8_t prediction_result = trace.info[seq_no].prediction_result;

	/* 
	LT : If the instruction hits in the LT, then move it to the VT if its a load. If it's not a load, 
	then look up the src instructions and add them to the LT. In either case, remove the entry from the LT.
	*/
	uint8_t ltIdx = pc % LT_SIZE;
	if (checkLT(pc)) {
		migrateLTtoVT(pc, seq_no, eligible, actual_value);
	}

    return;
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

void updateVT(uint64_t pc, uint64_t actual_value) {
	if (checkVT(pc)) {
		uint8_t vtIdx = pc & (VT_SIZE - 1);
		if (predictor.VT.entry[vtIdx].data == actual_value) {
			// we know data is repeating
			predictor.VT.entry[vtIdx].confidence = std::min(predictor.VT.entry[vtIdx].confidence+rand16(), 8); // Confidence incremented with a 1/16 probability
			predictor.VT.entry[vtIdx].utility = std::min(predictor.VT.entry[vtIdx].utility+1, 4);
			predictor.VT.entry[vtIdx].no_predict = (predictor.VT.entry[vtIdx].confidence == 8) ? 0 : predictor.VT.entry[vtIdx].no_predict;
		}
		else {
			predictor.VT.entry[vtIdx].confidence = 0;
			predictor.VT.entry[vtIdx].utility = 0;
			// don't push it towards not-predictable even before it has started making predictions
			//predictor.VT.entry[vtIdx].no_predict = std::min(predictor.VT.entry[vtIdx].no_predict+1, 4);
		}
	}
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

	if (vtIdx != (pc & (VT_SIZE-1))){
		std::cout << "What's happening vtIdx " << vtIdx << " pc & (VT_SIZE-1)" << (pc & (VT_SIZE-1)) << std::endl;
	}

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

	/* This doesn't look right. As per my understanding we should add to LT when
	the instruction is being added to the ROB and obtain the PC from the RAT 
	then and there. 
	
	One major reason for that is the scenario where the entry corresponding to either
	of these sources in the RAT may have been updated with some other PC because
	we are storing the Architectural Registers in the RAT.
	
	By the time the current instruction, which is a non-load instruction has 
	finished execution, the architectural source register(s) it was	dependent on,
	might see another instruction(s), thus giving us a wrong PC*/

	/*else {	// If it's not a load, find the parent instructions and add them to the LT

		// First evict the LT entry
		predictor.LT.entry[ltIdx].pc = 0xdeadbeef;

		// Retrieve src register IDs from TraceInfo
		uint64_t src1 = trace.info[seq_no].src1;
		uint64_t src2 = trace.info[seq_no].src2;
		uint64_t src3 = trace.info[seq_no].src3;

		addParentsToLT(src1, src2, src3);

	}*/

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
#include <cinttypes>
#include <unordered_map>
#include <bitset>
#include <cmath>
#ifndef _CVP_H
#include "cvp.h"
#endif

#define CIT_SIZE 32
#define VT_SIZE 64
#define LOG2_VT_SIZE 6
#define RAT_SIZE 64
#define LT_SIZE 8
#define MRT_SIZE 136
#define MRVF_SIZE 40
#define VT_TAG_SIZE 11 // 11 bits

template<typename T>
T bitExtract(T data, uint8_t start, uint8_t numBits) {
	return (data >> start) & ((T)(pow(2,numBits)) - 1);
}

struct TraceInfo {

	struct insn_info {
		uint64_t pc;
		uint64_t next_pc;
		InstClass insttype;
		uint64_t src1;
		uint64_t src2;
		uint64_t src3;
		uint64_t dst;
		uint8_t prediction_result;
	};

	std::unordered_map<uint64_t, insn_info> info;

};

struct MyPredictor {

	struct CIT_entry {
		uint64_t tag : 11;
		uint8_t confidence : 3;
		uint8_t utility : 3;

		CIT_entry() : tag(0xdeadbeef), confidence(0), utility(0){}
		void clear() {
			tag = 0xdeadbeef;
			confidence = 0;
			utility = 0;
		}
		void set(uint64_t in_tag, uint8_t in_confidence, uint8_t in_utility) {
			tag = in_tag;
			confidence = in_confidence;
			utility = in_utility;
		}
	};

	struct CIT {
		CIT_entry entry[CIT_SIZE];
	};

	struct VT_entry {
		uint64_t tag;
		uint8_t confidence;
		uint8_t utility;
		uint64_t data;
		uint8_t no_predict;

		VT_entry() : tag(0xdeadbeef), confidence(0), utility(0), data(0xdeadbeef), no_predict(0) {}

		// All the 64-bits of the PC shouldn't be used as the tag, right? At least that's 
		// what the paper on FVP says
		void set(uint64_t in_tag, uint8_t in_confidence, uint8_t in_utility, uint64_t in_data, uint8_t in_no_predict) {
			tag = bitExtract<uint64_t>(in_tag, LOG2_VT_SIZE, VT_TAG_SIZE); // in_tag 
			confidence = in_confidence;
			utility = in_utility;
			data = in_data;
			no_predict = in_no_predict;
		}
		void clear() {
			tag = 0xdeadbeef;
			confidence = 0;
			utility = 0;
			data = 0xdeadbeef;
			no_predict = 0;
		}
	};

	struct VT {
		VT_entry entry[VT_SIZE];
	};

	struct RAT_entry {
		uint64_t pc;

		RAT_entry() : pc(0xdeadbeef) {}
	};

	struct RAT {
		RAT_entry entry[RAT_SIZE];
	};

	struct LT_entry {
		uint64_t pc;

		LT_entry() : pc(0xdeadbeef) {}
	};

	struct LT {
		LT_entry entry[LT_SIZE];
	};

	struct MRT_entry {
		uint64_t tag;
		uint8_t confidence;
		uint8_t lru;
	};

	struct MRT {
		MRT_entry entry[MRT_SIZE];
	};

	struct MRVF_entry {
		uint64_t data;
		uint8_t store_id;
	};

	struct MRVF {
		MRT_entry entry[MRVF_SIZE];
	};

	// All the tables
	CIT CIT;
	VT VT;
	RAT RAT;
	LT LT;
	MRT MRT;
	MRVF MRVF;

	// Branch history register
	uint32_t bhr;
};

void updateBHR(bool taken);

void populateTraceInfo(uint64_t seq_no,			  // dynamic micro-instruction # (starts at 0 and increments indefinitely)
					   uint8_t prediction_result, // 0: incorrect, 1: correct, 2: unknown (not revealed)
					   uint64_t pc,
					   uint64_t next_pc,
					   InstClass insn,
					   uint64_t src1,
					   uint64_t src2,
					   uint64_t src3,
					   uint64_t dst);

void updateVT(bool critical,
			bool eligible,
			uint64_t pc,
			uint64_t seq_no,
			uint64_t actual_addr,
			uint64_t actual_value,
			uint64_t actual_latency);
#include <cinttypes>
#include <unordered_map>
#ifndef _CVP_H
#include "cvp.h"
#endif

#define CIT_SIZE 32
#define VT_SIZE 48
#define RAT_SIZE 64
#define LT_SIZE 8
#define MRT_SIZE 136
#define MRVF_SIZE 40

/*
enum InstClass : uint8_t
{
  aluInstClass = 0,
  loadInstClass = 1,
  storeInstClass = 2,
  condBranchInstClass = 3,
  uncondDirectBranchInstClass = 4,
  uncondIndirectBranchInstClass = 5,
  fpInstClass = 6,
  slowAluInstClass = 7,
  undefInstClass = 8
};
*/

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
		uint64_t tag;
		uint8_t confidence;
		uint8_t utility;

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
		void set(uint64_t in_tag, uint8_t in_confidence, uint8_t in_utility, uint64_t in_data, uint8_t in_no_predict) {
			tag = in_tag;
			confidence = in_confidence;
			utility = in_utility;
			data = in_data;
			no_predict = in_no_predict;
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
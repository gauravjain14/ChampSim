#ifndef __CVP_DEFAULT_HH__
#define __CVP_DEFAULT_HH__

#include <vector>
#include <deque>

struct window_t
{
  uint64_t retire_cycle;
  uint64_t seq_no;
  uint64_t addr;
  uint64_t value;
  uint64_t latency;
};

struct store_queue_t
{
  uint64_t exec_cycle; // store's execution cycle
  uint64_t ret_cycle;  // store's commit cycle
};

struct MyPredictor
{
  struct StrideEntry
  {
    uint64_t tag;
    uint64_t lastValue;
    uint64_t stride;
    uint64_t inflight;
    uint8_t conf;

    StrideEntry() : tag(0), lastValue(0), stride(0), inflight(0), conf(0) {}
  };

  std::vector<StrideEntry> strideTable;

  struct FirstLevelEntry
  {
    uint64_t predictTimeIndex;
    uint64_t commitTimeIndex;
    uint64_t inflight;
    uint64_t fetchHistory[4];
    uint64_t commitHistory[4];
    bool inflightLo;

    void computeCommitIndex(uint64_t insert)
    {
      for (unsigned i = 3; i != 0; i--)
        commitHistory[i] = commitHistory[i - 1];

      commitHistory[0] = (insert ^ (insert >> 16) ^ (insert >> 32) ^ (insert >> 48)) & 0xFFFF;

      commitTimeIndex = (commitHistory[0] & 0xFFFF) ^ ((commitHistory[1] << 4) & 0xFFF0) ^ ((commitHistory[2] << 8) & 0xFF00) ^ ((commitHistory[3] << 12) & 0xF000);
    }

    void computePredictIndex(uint64_t insert)
    {
      for (unsigned i = 3; i != 0; i--)
        fetchHistory[i] = fetchHistory[i - 1];

      fetchHistory[0] = (insert ^ (insert >> 16) ^ (insert >> 32) ^ (insert >> 48)) & 0xFFFF;

      predictTimeIndex = (fetchHistory[0] & 0xFFFF) ^ ((fetchHistory[1] << 4) & 0xFFF0) ^ ((fetchHistory[2] << 8) & 0xFF00) ^ ((fetchHistory[3] << 12) & 0xF000);
    }

    FirstLevelEntry() : predictTimeIndex(0), commitTimeIndex(0), inflight(0), inflightLo(false) {}
  };

  std::vector<FirstLevelEntry> firstLevelTable;

  struct SecondLevelEntry
  {
    uint64_t pred;
    uint8_t conf;
    uint64_t tag;

    SecondLevelEntry() : pred(0), conf(0){};
  };

  std::vector<SecondLevelEntry> secondLevelTable;

  struct InflightInfo
  {
    uint64_t seqNum;
    uint64_t firstLevelIndex;
    uint64_t secondLevelIndex;
    uint64_t secondLevelTag;

    InflightInfo() : seqNum(0), firstLevelIndex(0) {}
    InflightInfo(uint64_t sn, uint64_t fidx, uint64_t sidx, uint64_t stag) : seqNum(sn), firstLevelIndex(fidx), secondLevelIndex(sidx), secondLevelTag(stag) {}
  };

  std::deque<InflightInfo> inflightPreds;

  uint64_t globalHistoryRegister;
  uint64_t pathHistoryRegister;
  uint64_t addressHistoryRegister;

  uint64_t firstLevelMask;
  uint64_t lastPrediction;
  uint64_t lastStridePrediction;
  int choice;

public:
  MyPredictor() : globalHistoryRegister(0), pathHistoryRegister(0), addressHistoryRegister(0)
  {
    firstLevelTable.resize(4096 * 16);
    secondLevelTable.resize(4096 * 16);
    strideTable.resize(4096 * 16);

    firstLevelMask = (4096 * 16) - 1;
    lastPrediction = 0xdeadbeef;
  }
};

#endif

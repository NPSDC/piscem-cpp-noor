#ifndef HIT_SEARCHER_HPP
#define HIT_SEARCHER_HPP

#include "CanonicalKmer.hpp"
#include "CanonicalKmerIterator.hpp"
#include "projected_hits.hpp"
#include "reference_index.hpp"
#include "query/contig_info_query_canonical_parsing.cpp"
//#include "Util.hpp"
//#include "dictionary.hpp"

#include <algorithm>
#include <iostream>
#include <iterator>

namespace mindex {
class hit_searcher {
enum class ExpansionTerminationType : uint8_t { MISMATCH = 0, CONTIG_END, READ_END };  

public:
  explicit hit_searcher(reference_index* pfi) : pfi_(pfi) { 
    k = static_cast<size_t>(pfi_->k()); 
  }
  
  bool get_raw_hits_sketch(std::string &read,
                  sshash::contig_info_query_canonical_parsing& qc,
                  bool isLeft=false,
                  bool verbose=false);

void clear();

void setAltSkip(uint32_t altSkip);

inline std::vector<std::pair<int, projected_hits>>& get_left_hits() { 
  return left_rawHits;
}
inline std::vector<std::pair<int, projected_hits>>& get_right_hits() {
  return right_rawHits;
}

private:
  reference_index* pfi_;
  size_t k;
  uint32_t altSkip{3};

  bool isSingleEnd = false;
  std::vector<std::pair<int, projected_hits>> left_rawHits;
  std::vector<std::pair<int, projected_hits>> right_rawHits;
};
}
#endif // HIT_SEARCHER
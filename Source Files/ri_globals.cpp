#include "ri_globals.h"
#include "ri_mismatches.h"

// Global data structures for validation and mismatch tracking:
std::unordered_set<int> validMastersIn;               // Valid master indices from input
std::unordered_set<int> validMastersDb;               // Valid master indices from database
std::unordered_set<MismatchEntry> mismatchedEntries;  // Collection of mismatched records
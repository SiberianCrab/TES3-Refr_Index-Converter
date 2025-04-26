#pragma once
#include "ri_mismatches.h"
#include <json.hpp>
#include <unordered_set>

// Define an alias for ordered_json type from the nlohmann library
using ordered_json = nlohmann::ordered_json;

// Global data structures for validation and mismatch tracking:
extern std::unordered_set<int> validMastersIn;
extern std::unordered_set<int> validMastersDb;
extern std::unordered_set<MismatchEntry> mismatchedEntries;
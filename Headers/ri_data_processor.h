#pragma once
#include <unordered_set>
#include <optional>

#include "ri_database.h"
#include "ri_globals.h"
#include "ri_mismatches.h"
#include "ri_options.h"

// Define an enumeration for fetch modes
enum FetchMode {
    FETCH_DB_ID,
    FETCH_OPPOSITE_REFR_INDEX
};

// Function to fetch the refr_index from the database
std::optional<int> fetchRefIndex(const Database& db, const std::string& query,
    int refrIndexJson, const std::string& idJson);

// Template function to fetch ID from the database based on the fetch mode
template <FetchMode mode>
auto fetchID(const Database& db, int refrIndexJson, int mastIndex,
    const std::unordered_set<int>& validMastersDb, int conversionChoice);

// Function to process replacements and mismatches
int processReplacementsAndMismatches(const Database& db, const ProgramOptions& options, const std::string& query, ordered_json& inputData,
    int conversionChoice, int& replacementsFlag,
    const std::unordered_set<int>& validMastersDb,
    std::unordered_set<MismatchEntry>& mismatchedEntries,
    std::ofstream& logFile);
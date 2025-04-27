#include "ri_data_processor.h"
#include "ri_logger.h"
#include "ri_options.h"
#include "ri_user_interaction.h"

// Function to fetch the refr_index from the database
std::optional<int> fetchRefIndex(const Database& db, const std::string& query, int refrIndexJson, const std::string& idJson) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt_ptr(stmt, sqlite3_finalize);
    sqlite3_bind_int(stmt, 1, refrIndexJson);
    sqlite3_bind_text(stmt, 2, idJson.c_str(), static_cast<int>(idJson.length()), SQLITE_STATIC);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        return sqlite3_column_int(stmt, 0);
    }
    return std::nullopt;
}

// Template function to fetch ID from the database based on the fetch mode
template <FetchMode mode>
auto fetchID(const Database& db, int refrIndexJson, int mastIndex, const std::unordered_set<int>& validMastersDb, int conversionChoice) {
    std::string query;

    // Determine the query based on the conversion choice and fetch mode
    switch (conversionChoice) {
    case 1:
        query = (mode == FETCH_DB_ID)
            ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?"
            : "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ?";
        break;
    case 2:
        query = (mode == FETCH_DB_ID)
            ? "SELECT ID FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?"
            : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ?";
        break;
    default:
        if constexpr (mode == FETCH_DB_ID) return std::string();
        else return -1;
    }

    // Append conditions to the query based on the valid masters
    if (validMastersDb.count(1)) {
        if (mastIndex == 2) query += " AND Master = 'Tribunal'";
        else if (mastIndex == 3) query += " AND Master = 'Bloodmoon'";
    }
    else if (validMastersDb.count(2)) query += " AND Master = 'Tribunal'";
    else if (validMastersDb.count(3)) query += " AND Master = 'Bloodmoon'";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        if constexpr (mode == FETCH_DB_ID) return std::string();
        else return -1;
    }

    sqlite3_bind_int(stmt, 1, refrIndexJson);

    // Fetch the value based on the fetch mode
    if constexpr (mode == FETCH_DB_ID) {
        std::string idDb;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* idJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (idJson) idDb = idJson;
        }
        sqlite3_finalize(stmt);
        return idDb;
    }
    else {
        int refrIndexDb = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            refrIndexDb = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return refrIndexDb;
    }
}

// Function to process replacements and mismatches
int processReplacementsAndMismatches(const Database& db, const ProgramOptions& options, const std::string& query, ordered_json& inputData,
    int conversionChoice, int& replacementsFlag,
    const std::unordered_set<int>& validMastersDb,
    std::unordered_set<MismatchEntry>& mismatchedEntries,
    std::ofstream& logFile) {

    // Validate root JSON structure
    if (!inputData.is_array()) {
        logMessage("\nERROR - input JSON is not an array, unable to process!", logFile);
        return -1;
    }

    // Process each cell in the JSON array
    for (auto cellIter = inputData.begin(); cellIter != inputData.end(); ++cellIter) {

        // Skip non-cell entries or cells without proper type
        if (!cellIter->contains("type") || (*cellIter)["type"] != "Cell") continue;

        // Extract references array from cell
        auto& cellReferences = (*cellIter)["references"];
        if (!cellReferences.is_array()) continue;

        // Process individual references in cell
        for (auto refIter = cellReferences.begin(); refIter != cellReferences.end(); ++refIter) {
            auto& referenceData = *refIter;

            // Validate reference structure
            if (!referenceData.contains("refr_index") || !referenceData["refr_index"].is_number_integer() ||
                !referenceData.contains("id") || !referenceData["id"].is_string()) {
                continue;
            }

            // Extract reference data
            int inputRefIndex = referenceData["refr_index"];
            std::string inputId = referenceData["id"];
            int inputMastIndex = referenceData.value("mast_index", -1);

            // Valid Parent Master files check
            if (!validMastersIn.count(inputMastIndex)) {
                //if (!options.silentMode) {
                    //logMessage("Skipping object (invalid master index): " + inputId, logFile);
                //}
                continue;
            }

            // Handle replacements
            if (auto foundRefIndex = fetchRefIndex(db, query, inputRefIndex, inputId)) {
                referenceData["refr_index"] = *foundRefIndex;
                if (!options.silentMode) {
                    logMessage("Replaced JSON refr_index " + std::to_string(inputRefIndex) +
                               " with DB refr_index " + std::to_string(*foundRefIndex) +
                               " for JSON id " + inputId, logFile);
                }
                replacementsFlag = 1;
            }

            // Handle mismatches
            else {
                const int refrIndexDb = fetchID<FETCH_OPPOSITE_REFR_INDEX>(db, inputRefIndex, inputMastIndex, validMastersDb, conversionChoice);

                // Skip if no matching record found in DB
                if (refrIndexDb == -1) {
                    //if (!options.silentMode) {
                        //logMessage("Skipping object (no match in DB): JSON refr_index " + std::to_string(inputRefIndex) +
                        //           " and JSON id " + inputId, logFile);
                    //}
                    continue;
                }

                const std::string idDb = fetchID<FETCH_DB_ID>(db, inputRefIndex, inputMastIndex, validMastersDb, conversionChoice);

                // Only proceed with mismatch handling if we have valid DB data
                if (!options.silentMode) {
                    logMessage("Mismatch found for JSON refr_index " + std::to_string(inputRefIndex) +
                               " and JSON id " + inputId + " with DB refr_index " + std::to_string(refrIndexDb) +
                               " and DB id " + idDb, logFile);
                }

                // Handle duplicated mismatches
                if (auto [it, inserted] = mismatchedEntries.insert(
                    MismatchEntry{ inputRefIndex, inputId, idDb, refrIndexDb }); !inserted) {
                    logMessage("WARNING - skipped duplicate mismatch entry for JSON refr_index " + std::to_string(inputRefIndex) +
                               " and JSON id " + inputId, logFile);
                }
            }
        }
    }

    // Handle user choice for mismatched entries
    if (!mismatchedEntries.empty()) {
        int mismatchChoice = getUserMismatchChoice(logFile, options);

        if (mismatchChoice == 1) {
            // Apply replacements for all tracked mismatches
            for (const auto& entry : mismatchedEntries) {
                for (auto& cell : inputData) {
                    if (!cell.contains("references") || !cell["references"].is_array()) continue;

                    // Find and update matching references
                    for (auto& reference : cell["references"]) {
                        if (reference["refr_index"] == entry.refrIndexJson &&
                            reference.value("id", "") == entry.idJson) {
                            reference["refr_index"] = entry.refrIndexDb;
                            if (!options.silentMode) {
                                logMessage("Replaced mismatched JSON refr_index " + std::to_string(entry.refrIndexJson) +
                                           " with DB refr_index " + std::to_string(entry.refrIndexDb) +
                                           " for JSON id " + entry.idJson, logFile);
                            }
                            replacementsFlag = 1;
                        }
                    }
                }
            }
        }
        else {
            logMessage("Mismatched entries will remain unchanged...", logFile);
        }
    }
    else {
        logMessage("No mismatched entries found - skipping mismatch handling...", logFile);
    }

    return 0;
}
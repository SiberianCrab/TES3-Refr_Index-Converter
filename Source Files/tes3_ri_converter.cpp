#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <limits>
#include <string>

#include "ri_data_processor.h"
#include "ri_database.h"
#include "ri_file_processor.h"
#include "ri_logger.h"
#include "ri_options.h"
#include "ri_user_interaction.h"

// Main function
int main(int argc, char* argv[]) {
    // Parse command line arguments
    ProgramOptions options = parseArguments(argc, argv);

    // Display program information
    if (!options.silentMode) {
        std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n"
                  << PROGRAM_AUTHOR << "\n\n" << PROGRAM_TESTER << "\n\n";
    }

    // Log file initialisation
    std::ofstream logFile("tes3_ri.log", std::ios::app);
    if (!logFile.is_open()) {
        logErrorAndExit("ERROR - failed to open log file!\n", logFile);
    }

    // Clear log file
    logClear();
    if (!options.silentMode) {
        logMessage("Log file cleared...", logFile);
    }

    // Check if the database file exists
    if (!std::filesystem::exists("tes3_ri_en-ru_refr_index.db")) {
        logErrorAndExit("ERROR - database file 'tes3_ri_en-ru_refr_index.db' not found!\n", logFile);
    }

    Database db("tes3_ri_en-ru_refr_index.db");

    // Log successful connection if not in silent mode
    if (!options.silentMode) {
        logMessage("Database opened successfully...", logFile);
    }

    // Check if the converter executable exists
    if (!std::filesystem::exists(TES3CONV_COMMAND)) {
        logErrorAndExit("ERROR - tes3conv not found! Please download the latest version from\n"
                        "github.com/Greatness7/tes3conv/releases and place it in the same directory\n"
                        "with this program.\n", logFile);
    }

    if (!options.silentMode) {
        logMessage("tes3conv found...\n"
                   "Initialisation complete...\n"
                   "(\\/)Oo(\\/)", logFile);
    }

    // Get the conversion choice
    if (options.conversionType == 0) {
        options.conversionType = getUserConversionChoice(logFile);
    }
    else if (!options.silentMode) {
        logMessage("\nConversion type set from arguments: " + std::string(options.conversionType == 1 ? "RU to EN" : "EN to RU"), logFile);
    }

    // Get the input file path(s)
    auto inputPaths = getInputFilePaths(options, logFile);

    // Time start
    auto programStart = std::chrono::high_resolution_clock::now();

    // Sequential processing of each file
    for (const auto& pluginImportPath : inputPaths) {
        // Time file start
        auto fileStart = std::chrono::high_resolution_clock::now();

        // Clear data
        validMastersIn.clear();
        validMastersDb.clear();
        mismatchedEntries.clear();

        logMessage("Processing file: " + pluginImportPath.string(), logFile);

        try {
            // Define the output file path
            std::filesystem::path jsonImportPath = pluginImportPath.parent_path() / (pluginImportPath.stem().string() + ".json");

            // Convert the input file to .JSON
            std::ostringstream convCmd;
            convCmd << TES3CONV_COMMAND << " "
                    << std::quoted(pluginImportPath.string()) << " "
                    << std::quoted(jsonImportPath.string());

            if (std::system(convCmd.str().c_str()) != 0) {
                logMessage("ERROR - converting to .JSON failed for file: " + pluginImportPath.string() + "\n", logFile);
                continue;
            }
            if (!options.silentMode) {
                logMessage("Conversion to .JSON successful: " + jsonImportPath.string(), logFile);
            }

            // Load the generated JSON file
            std::ifstream inputFile(jsonImportPath, std::ios::binary);
            if (!inputFile.is_open()) {
                logMessage("ERROR - failed to open JSON file: " + jsonImportPath.string() + "\n", logFile);
                continue;
            }

            inputFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

            ordered_json inputData;
            try {
                inputFile >> inputData;

                if (inputData.is_discarded()) {
                    logMessage("ERROR - parsed JSON is invalid or empty: " + jsonImportPath.string() + "\n", logFile);
                    continue;
                }
            }
            catch (const std::exception& e) {
                logMessage("ERROR - failed to parse JSON (" + jsonImportPath.string() + "): " + e.what() + "\n", logFile);
                continue;
            }

            inputFile.close();

            // Check if file was already converted
            if (hasConversionTag(inputData, pluginImportPath, logFile)) {
                std::filesystem::remove(jsonImportPath);
                logMessage("ERROR - file " + pluginImportPath.string() + " was already converted - conversion skipped...", logFile);
                if (options.silentMode) {
                    logMessage("", logFile);
                }
                else {
                    logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                }

                continue;
            }

            // Check the dependency order
            auto [isValid, validMasters] = checkDependencyOrder(inputData, logFile);
            if (!isValid) {
                std::filesystem::remove(jsonImportPath);
                logMessage("ERROR - required Parent Masters not found for file: " + pluginImportPath.string() + " - conversion skipped...", logFile);
                if (options.silentMode) {
                    logMessage("", logFile);
                }
                else {
                    logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                }

                continue;
            }

            // Initialize the replacements flag
            int replacementsFlag = 0;

            // Initialize the query based on conversion choice
            std::string dbQuery = (options.conversionType == 1)
                ? "SELECT refr_index_EN FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_RU = ? AND id = ?;"
                : "SELECT refr_index_RU FROM [tes3_T-B_en-ru_refr_index] WHERE refr_index_EN = ? AND id = ?;";

            // Process replacements and mismatches
            if (processReplacementsAndMismatches(db, options, dbQuery, inputData, options.conversionType, replacementsFlag, validMasters, mismatchedEntries, logFile) == -1) {
                logMessage("ERROR - processing failed for file: " + pluginImportPath.string() + "\n", logFile);
                continue;
            }

            // Check if any replacements were made
            if (replacementsFlag == 0) {
                std::filesystem::remove(jsonImportPath);
                logMessage("No replacements found for file: " + pluginImportPath.string() + " - conversion skipped...", logFile);
                if (options.silentMode) {
                    logMessage("", logFile);
                }
                else {
                    logMessage("Temporary .JSON file deleted: " + jsonImportPath.string() + "\n", logFile);
                }

                continue;
            }

            // Define conversion prefix
            std::string convPrefix = (options.conversionType == 1) ? "RU->EN" : "EN->RU";

            // Add conversion tag to header
            if (!addConversionTag(inputData, convPrefix, options, logFile)) {
                logMessage("ERROR - could not find or modify header description\n", logFile);
                continue;
            }

            // Save the modified data to .JSON file
            auto newJsonName = std::format("TEMP_{}{}", pluginImportPath.stem().string(), ".json");
            std::filesystem::path jsonExportPath = pluginImportPath.parent_path() / newJsonName;

            if (!saveJsonToFile(jsonExportPath, inputData, options, logFile)) {
                logMessage("ERROR - failed to save modified data to .JSON file: " + jsonExportPath.string() + "\n", logFile);
                continue;
            }

            // Create backup before modifying original file
            if (!createBackup(pluginImportPath, options, logFile)) {
                std::filesystem::remove(jsonImportPath);
                if (!options.silentMode) {
                    logMessage("Temporary .JSON file deleted: " + jsonImportPath.string(), logFile);
                }

                continue;
            }

            // Save converted file with original name
            if (!convertJsonToEsp(jsonExportPath, pluginImportPath, options, logFile)) {
                logMessage("ERROR - failed to convert .JSON back to .ESP|ESM: " + pluginImportPath.string() + "\n", logFile);
                continue;
            }

            // Clean up temporary .JSON files
            std::filesystem::remove(jsonImportPath);
            std::filesystem::remove(jsonExportPath);
            if (!options.silentMode) {
                logMessage("Temporary .JSON files deleted: " + jsonImportPath.string() + "\n" +
                           "                          and: " + jsonExportPath.string(), logFile);
            }

            // Time file total
            auto fileEnd = std::chrono::high_resolution_clock::now();
            auto fileDuration = fileEnd - fileStart;
            auto seconds = std::chrono::duration<double>(fileDuration).count();
            if (!options.silentMode) {
                logMessage(std::format("\nFile converted in: {:.3f} seconds\n", seconds), logFile);
            }
        }
        catch (const std::exception& e) {
            // Time error
            auto fileEnd = std::chrono::high_resolution_clock::now();
            auto fileDuration = std::chrono::duration_cast<std::chrono::milliseconds>(fileEnd - fileStart);

            logMessage("ERROR - failed to process file " + pluginImportPath.string() + ": " + e.what() + "\n", logFile);

            // Clear data in case of error
            validMastersIn.clear();
            validMastersDb.clear();
            mismatchedEntries.clear();
            continue;
        }
    }

    // Time total
    auto programEnd = std::chrono::high_resolution_clock::now();
    auto programDuration = programEnd - programStart;
    auto seconds = std::chrono::duration<double>(programDuration).count();
    if (!options.silentMode) {
        logMessage(std::format("\nTotal processing time: {:.3f} seconds", seconds), logFile);
    }

    // Close the database
    if (!options.silentMode) {
        logMessage("\nThe ending of the words is ALMSIVI", logFile);
        logFile.close();

        // Wait for user input before exiting (Windows)
    #ifndef __linux__
        std::cout << "\nPress Enter to exit...";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    #endif
    }

    return EXIT_SUCCESS;
}
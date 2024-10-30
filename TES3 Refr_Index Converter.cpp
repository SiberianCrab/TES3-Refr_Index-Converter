#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <sqlite3.h>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <sstream>

// Program info
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

// Добавьте глобальную переменную для допустимых значений mast_index
std::unordered_set<int> validMastIndices;

// Unified logging function
void logMessage(const std::string& message) {
    std::ofstream logFile("tes3ric_log.txt", std::ios_base::app);
    logFile << message << std::endl;
    std::cout << message << std::endl;
}

// Function to clear the log file
void clearLogFile(const std::string& logFileName) {
    if (std::filesystem::exists(logFileName)) {
        try {
            std::filesystem::remove(logFileName);
            logMessage("Log cleared successfully...");
        }
        catch (const std::filesystem::filesystem_error& e) {
            logMessage("Error clearing log file: " + std::string(e.what()));
        }
    }
}

// Function to check the order of dependencies in the JSON header
bool checkDependencyOrder(const std::string& inputData) {
    // Find positions of the masters
    size_t mwPos = inputData.find("Morrowind.esm");
    size_t tPos = inputData.find("Tribunal.esm");
    size_t bPos = inputData.find("Bloodmoon.esm");

    // Check if Morrowind is found
    if (mwPos == std::string::npos) {
        logMessage("Error: Morrowind.esm not found.");
        return false;
    }

    // Clearing previous values
    validMastIndices.clear(); // Use one container

    // Check Tribunal and Bloodmoon positions
    bool hasTribunal = (tPos != std::string::npos);
    bool hasBloodmoon = (bPos != std::string::npos);

    // Verify the order of dependencies
    if (hasTribunal && hasBloodmoon) {
        // Valid combination: M+T+B
        if (tPos < bPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.");
            validMastIndices.insert(2); // Add index 2 for Tribunal
            validMastIndices.insert(3); // Adding index 3 for Bloodmoon
            return true;
        }
        else {
            logMessage("Error: Invalid order. Tribunal.esm should come before Bloodmoon.esm!");
            return false;
        }
    }
    else if (hasTribunal) {
        // Valid combination: M+T
        logMessage("Valid order of Parent Masters found: M+T.");
        validMastIndices.insert(2); // Add index 2 for Tribunal
        return true;
    }
    else if (hasBloodmoon) {
        // Valid combination: M+B
        logMessage("Valid order of Parent Masters found: M+B.");
        validMastIndices.insert(2); // Adding index 2 for Bloodmoon
        return true;
    }

    // If neither Tribunal nor Bloodmoon is found, we return false
    logMessage("Error: Neither Tribunal.esm nor Bloodmoon.esm found in the correct order!");
    return false;
}

// Helper to fetch refr_index from the database
int fetchRefIndex(sqlite3* db, const std::string& query, int refrIndex, const std::string& id) {
    int result = -1;
    sqlite3_stmt* stmt;

    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, refrIndex);
        sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
        }
    }
    else {
        logMessage("Database query error: " + std::string(sqlite3_errmsg(db)));
    }

    sqlite3_finalize(stmt);
    return result;
}

// Escape special characters for regex matching
std::string regexEscape(const std::string& str) {
    static const std::regex specialChars(R"([-[\]{}()*+?.,\^$|#\s])");
    return std::regex_replace(str, specialChars, R"(\$&)");
}

// Optimized function for processing JSON replacements
void optimizeJsonReplacement(std::ostringstream& outputStream, const std::string& inputData, const std::unordered_map<int, int>& replacements) {
    size_t pos = 0;
    size_t lastPos = 0;

    while (pos < inputData.size()) {
        // Search for the next "mast_index" entry
        pos = inputData.find("\"mast_index\":", lastPos);
        if (pos == std::string::npos) {
            // No more replacements, copy the rest of the input data
            outputStream << inputData.substr(lastPos);
            break;
        }

        // Write data before the found "mast_index"
        outputStream << inputData.substr(lastPos, pos - lastPos);

        // Find the end position of "mast_index" value
        size_t endPos = inputData.find(',', pos);
        if (endPos == std::string::npos) endPos = inputData.find('}', pos);

        // Extract the current mast_index
        std::string currentMastIndexStr = inputData.substr(pos + 14, endPos - pos - 14);
        int currentMastIndex = std::stoi(currentMastIndexStr);

        // Move to the next object to find "refr_index"
        size_t refrIndexPos = inputData.find("\"refr_index\":", endPos);
        if (refrIndexPos == std::string::npos) {
            outputStream << inputData.substr(lastPos); // If not found, just append the rest
            break;
        }

        // Move to the value of refr_index
        size_t refrEndPos = inputData.find(',', refrIndexPos);
        if (refrEndPos == std::string::npos) refrEndPos = inputData.find('}', refrIndexPos);

        // Extract the current refr_index
        std::string currentIndexStr = inputData.substr(refrIndexPos + 14, refrEndPos - refrIndexPos - 14);
        int currentIndex = std::stoi(currentIndexStr);

        // Check if mast_index is valid
        if (validMastIndices.find(currentMastIndex) != validMastIndices.end()) {
            // mast_index is valid, check for replacement of refr_index
            auto it = replacements.find(currentIndex);
            if (it != replacements.end()) {
                // Replace refr_index with the new value
                outputStream << "\"mast_index\": " << currentMastIndex << ",\n        ";
                outputStream << "\"refr_index\": " << it->second;
            }
            else {
                // No replacement for refr_index, keep original
                outputStream << "\"mast_index\": " << currentMastIndex << ",\n        ";
                outputStream << "\"refr_index\": " << currentIndex;
            }
        }
        else {
            // mast_index is not valid, write back as is
            outputStream << "\"mast_index\": " << currentMastIndex << ",\n        ";
            outputStream << "\"refr_index\": " << currentIndex;
        }

        // Update the lastPos to continue searching in the remainder of inputData
        lastPos = refrEndPos;
    }
}

// Function to save modified JSON to a file
bool saveJsonToFile(const std::string& jsonFilePath, const std::string& outputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile.is_open()) {
        outputFile << outputData;
        outputFile.close();
        logMessage("Modified JSON saved as: " + jsonFilePath);
        return true;
    }
    else {
        logMessage("Error saving JSON file.");
        return false;
    }
}

// Function to convert JSON back to ESM/ESP
bool convertJsonToEsp(const std::string& jsonFilePath, const std::string& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath + "\" \"" + espFilePath + "\"";
    if (std::system(command.c_str()) != 0) {
        logMessage("Error converting JSON back to ESM/ESP.");
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath);
    return true;
}

// Main program
int main() {
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    clearLogFile("tes3ric_log.txt");

    if (!std::filesystem::exists("tes3_ru-en_refr_index.db")) {
        logMessage("Database file 'tes3_ru-en_refr_index.db' not found.\n");
        std::system("pause");
        return 1;
    }

    sqlite3* db;
    if (sqlite3_open("tes3_ru-en_refr_index.db", &db)) {
        logMessage("Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
        std::system("pause");
        return 1;
    }
    logMessage("Database opened successfully...");

    if (!std::filesystem::exists("tes3conv.exe")) {
        logMessage("tes3conv.exe not found. Please download the latest version from\nhttps://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory as this program.\n");
        std::system("pause");
        return 1;
    }
    logMessage("tes3conv.exe found.\n");

    int conversionChoice;
    while (true) {
        std::cout << "Convert a plugin or master file:\n1. Russian 1C to English GOTY\n2. English GOTY to Russian 1C\nChoice: ";
        std::cin >> conversionChoice;
        if (conversionChoice == 1 || conversionChoice == 2) break;
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    std::cin.ignore();

    std::string inputFilePath;
    while (true) {
        std::cout << "Enter the ESP/ESM full path (including extension), or filename (with extension)\nif it's in the same folder as this converter: ";
        std::getline(std::cin, inputFilePath);
        if (std::filesystem::exists(inputFilePath) &&
            (inputFilePath.ends_with(".esp") || inputFilePath.ends_with(".esm"))) {
            logMessage("File found: " + inputFilePath);
            break;
        }
        logMessage("File not found or incorrect extension.");
    }

    std::filesystem::path inputPath(inputFilePath);
    std::filesystem::path outputDir = inputPath.parent_path();
    std::string jsonFilePath = (outputDir / inputPath.stem()).string() + ".json";
    std::string command = "tes3conv.exe \"" + inputFilePath + "\" \"" + jsonFilePath + "\"";
    if (std::system(command.c_str()) != 0) {
        logMessage("Error converting to JSON. Check tes3conv.exe and the input file.");
        std::system("pause");
        return 1;
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath);

    std::ifstream inputFile(jsonFilePath);
    std::string inputData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

    // Dependency Order check function
    if (!checkDependencyOrder(inputData)) {
        logMessage("Required Parent Masters not found or in the wrong order. Aborting process...");
        sqlite3_close(db);
        std::system("pause");
        return 1;
    }

    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    std::string outputData = inputData;

    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();
    std::string query = (conversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    std::unordered_map<int, int> replacements;

    // Process all JSON objects
    while (it != end) {
        std::string jsonObject = it->str();
        std::regex refrIndexRegex(R"(\"refr_index\"\s*:\s*(\d+))");
        std::regex idRegex(R"(\"id\"\s*:\s*\"([^\"]+)\")");

        std::smatch refrIndexMatch, idMatch;
        // Use std::regex_search to find indices and IDs in each JSON object
        if (std::regex_search(jsonObject, refrIndexMatch, refrIndexRegex) &&
            std::regex_search(jsonObject, idMatch, idRegex)) {

            int refrIndex = std::stoi(refrIndexMatch[1].str());
            std::string id = idMatch[1].str();
            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id);

            if (newRefrIndex != -1) {
                replacements[refrIndex] = newRefrIndex;
                logMessage("Will replace refr_index " + std::to_string(refrIndex) + " with " + std::to_string(newRefrIndex) + " for id: " + id);
            }
        }
        ++it;
    }

    // Use the optimized replacement function
    std::ostringstream outputStream;
    optimizeJsonReplacement(outputStream, inputData, replacements);
    outputData = outputStream.str();

    // Saving all JSON objects function
    std::string newJsonFilePath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json")).string();

    if (!saveJsonToFile(newJsonFilePath, outputData)) {
        sqlite3_close(db);
        std::system("pause");
        return 1;
    }

    // Converting all JSON objects back to .esp or .esm function
    std::string outputExtension = inputPath.extension() == ".esp" ? ".esp" : ".esm";
    std::string newEspPath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension)).string();

    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        sqlite3_close(db);
        std::system("pause");
        return 1;
    }

    //
    sqlite3_close(db);
    logMessage("Process complete.\n");
    std::system("pause");
    return 0;
}
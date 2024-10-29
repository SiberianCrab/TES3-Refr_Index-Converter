#include <iostream>
#include <fstream>
#include <string>
#include <regex>
#include <sqlite3.h>
#include <filesystem>
#include <unordered_map>
#include <sstream>

// Program info
const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

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
        // Search for the next "refr_index" entry
        pos = inputData.find("\"refr_index\":", lastPos);
        if (pos == std::string::npos) {
            // No more replacements, copy the rest of the input data
            outputStream << inputData.substr(lastPos);
            break;
        }

        // Write data before the found "refr_index"
        outputStream << inputData.substr(lastPos, pos - lastPos);

        // Find the value of "refr_index"
        size_t endPos = inputData.find(',', pos);
        if (endPos == std::string::npos) endPos = inputData.find('}', pos);

        // Extract the current index
        std::string currentIndexStr = inputData.substr(pos + 14, endPos - pos - 14);
        int currentIndex = std::stoi(currentIndexStr);

        // Check if we have a replacement
        auto it = replacements.find(currentIndex);
        if (it != replacements.end()) {
            // Replace with new index
            outputStream << "\"refr_index\": " << it->second;
        }
        else {
            // No replacement found, keep the original
            outputStream << "\"refr_index\": " << currentIndex;
        }

        lastPos = endPos;
    }
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

    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    std::string outputData = inputData;

    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();
    std::string query = (conversionChoice == 1) ?
        "SELECT refr_index_EN FROM ref_index_database WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM ref_index_database WHERE refr_index_EN = ? AND id = ?;";

    std::unordered_map<int, int> replacements; // Map to hold index replacements

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
                replacements[refrIndex] = newRefrIndex; // Store the replacement
                logMessage("Will replace refr_index " + std::to_string(refrIndex) + " with " + std::to_string(newRefrIndex) + " for id: " + id);
            }
        }
        ++it;
    }

    // Use the optimized replacement function
    std::ostringstream outputStream;
    optimizeJsonReplacement(outputStream, inputData, replacements);
    outputData = outputStream.str(); // Retrieve the final result

    std::string newJsonFilePath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json")).string();
    std::ofstream outputFile(newJsonFilePath);
    if (outputFile.is_open()) {
        outputFile << outputData;
        outputFile.close();
        logMessage("Modified JSON saved as: " + newJsonFilePath);
    }
    else {
        logMessage("Error saving JSON file.");
        sqlite3_close(db);
        std::system("pause");
        return 1;
    }

    std::string outputExtension = inputPath.extension() == ".esp" ? ".esp" : ".esm";
    std::string newEspPath = (outputDir / ("CONV_" + std::string(conversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension)).string();
    std::string backCommand = "tes3conv.exe \"" + newJsonFilePath + "\" \"" + newEspPath + "\"";
    if (std::system(backCommand.c_str()) != 0) {
        logMessage("Error converting JSON back to ESM/ESP.");
        sqlite3_close(db);
        std::system("pause");
        return 1;
    }
    logMessage("Final conversion to ESM/ESP successful: " + newEspPath);

    sqlite3_close(db);
    logMessage("Process complete.\n");
    std::system("pause");
    return 0;
}
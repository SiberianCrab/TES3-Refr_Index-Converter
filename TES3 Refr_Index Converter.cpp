#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sqlite3.h>
#include <optional>

const std::string PROGRAM_NAME = "TES3 Refr_Index Converter";
const std::string PROGRAM_VERSION = "V 1.0.0";
const std::string PROGRAM_AUTHOR = "by SiberianCrab";

std::unordered_set<int> validMastIndices;
std::unordered_set<int> validMastersDB;
std::unordered_map<int, int> replacements;

struct MismatchEntry {
    int refrIndex;
    std::string id;
    std::string dbId;
    int oppositeRefrIndex;
};

std::vector<MismatchEntry> mismatchedEntries;

void logMessage(const std::string& message) {
    std::ofstream logFile("tes3ric_log.txt", std::ios_base::app);
    if (logFile.is_open()) {
        logFile << message << std::endl;
    }
    else {
        std::cerr << "Failed to open log file." << std::endl;
    }
    std::cout << message << std::endl;
}

void logErrorAndExit(sqlite3* db, const std::string& message) {
    logMessage(message);
    if (db) {
        sqlite3_close(db);
    }
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    throw std::runtime_error(message);
}

void clearLogFile(const std::filesystem::path& logFileName) {
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

int getConversionChoice() {
    int ConversionChoice;
    while (true) {
        std::cout << "Convert refr_index in a plugin or master file:\n"
            "1. From Russian 1C to English GOTY\n2. From English GOTY to Russian 1C\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            ConversionChoice = input[0] - '0';
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return ConversionChoice;
}

int getUserChoice() {
    int choice = 0;
    while (true) {
        std::cout << "Mismatched entries found (could occur if a Tribunal or Bloodmoon object was replaced with\n"
            "'Edit -> Search & Replace' in TES3 CS). Would you like to replace their refr_index anyway?\n"
            "1.Yes\n2.No\nChoice: ";
        std::string input;
        std::getline(std::cin, input);
        if (input == "1" || input == "2") {
            choice = std::stoi(input);
            break;
        }
        logMessage("Invalid choice. Enter 1 or 2.");
    }
    return choice;
}

std::filesystem::path getInputFilePath() {
    std::filesystem::path filePath;
    while (true) {
        std::cout << "Enter full path to .ESP or .ESM (including extension), or filename (with extension)\n"
            "if it's in the the same directory with this program.: ";
        std::string input;
        std::getline(std::cin, input);
        filePath = input;
        if (std::filesystem::exists(filePath) &&
            (filePath.extension() == ".esp" || filePath.extension() == ".esm")) {
            logMessage("File found: " + filePath.string());
            break;
        }
        logMessage("File not found or incorrect extension.");
    }
    return filePath;
}

std::pair<bool, std::unordered_set<int>> checkDependencyOrder(const std::string& inputData) {
    size_t mwPos = inputData.find("Morrowind.esm");
    size_t tPos = inputData.find("Tribunal.esm");
    size_t bPos = inputData.find("Bloodmoon.esm");

    if (mwPos == std::string::npos) {
        logMessage("Morrowind.esm not found!");
        return { false, {} };
    }

    validMastIndices.clear();
    validMastersDB.clear();

    if (tPos != std::string::npos && bPos != std::string::npos) {
        if (tPos < bPos) {
            logMessage("Valid order of Parent Masters found: M+T+B.");
            validMastIndices = { 2, 3 };
            validMastersDB.insert(1);
            return { true, validMastersDB };
        }
        else {
            logMessage("Invalid order of Parent Masters! Tribunal.esm should be before Bloodmoon.esm.");
            return { false, {} };
        }
    }

    if (tPos != std::string::npos) {
        logMessage("Valid order of Parent Masters found: M+T.");
        validMastIndices.insert(2);
        validMastersDB.insert(2);
        return { true, validMastersDB };
    }

    if (bPos != std::string::npos) {
        logMessage("Valid order of Parent Masters found: M+B.");
        validMastIndices.insert(2);
        validMastersDB.insert(3);
        return { true, validMastersDB };
    }

    return { false, {} };
}

int fetchCurrentMastIndex(const std::string& jsonObject) {
    std::regex mastIndexRegex(R"(\"mast_index\"\s*:\s*(\d+))");
    std::smatch mastIndexMatch;

    if (std::regex_search(jsonObject, mastIndexMatch, mastIndexRegex)) {
        return std::stoi(mastIndexMatch[1].str());
    }
    return -1;
}

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

std::optional<int> findRefrIndex(const std::string& jsonObject) {
    std::regex refrIndexRegex(R"(\"refr_index\"\s*:\s*(\d+))");
    std::smatch match;
    if (std::regex_search(jsonObject, match, refrIndexRegex)) {
        return std::stoi(match[1].str());
    }
    return std::nullopt;
}

std::optional<std::string> findId(const std::string& jsonObject) {
    std::regex idRegex(R"(\"id\"\s*:\s*\"([^\"]+)\")");
    std::smatch match;
    if (std::regex_search(jsonObject, match, idRegex)) {
        return match[1].str();
    }
    return std::nullopt;
}

enum FetchMode {
    FETCH_DB_ID,
    FETCH_OPPOSITE_REFR_INDEX
};

template <FetchMode mode>
auto fetchValue(sqlite3* db, int refrIndex, int mastIndex, const std::unordered_set<int>& validMastersDB, int conversionChoice)
-> std::conditional_t<mode == FETCH_DB_ID, std::string, int> {
    std::string query;

    switch (conversionChoice) {
    case 1:
        query = (mode == FETCH_DB_ID) ? "SELECT ID FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ?"
            : "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ?";
        break;
    case 2:
        query = (mode == FETCH_DB_ID) ? "SELECT ID FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ?"
            : "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ?";
        break;
    default:
        std::cerr << "Invalid conversion choice." << std::endl;
        if constexpr (mode == FETCH_DB_ID) {
            return std::string();
        }
        else {
            return -1;
        }
    }

    if (validMastersDB.count(1)) {
        if (mastIndex == 2) {
            query += " AND Master = 'Tribunal'";
        }
        else if (mastIndex == 3) {
            query += " AND Master = 'Bloodmoon'";
        }
    }
    else if (validMastersDB.count(2)) {
        query += " AND Master = 'Tribunal'";
    }
    else if (validMastersDB.count(3)) {
        query += " AND Master = 'Bloodmoon'";
    }

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "Failed to prepare statement: " << sqlite3_errmsg(db) << std::endl;
        if constexpr (mode == FETCH_DB_ID) {
            return std::string();
        }
        else {
            return -1;
        }
    }

    sqlite3_bind_int(stmt, 1, refrIndex);

    if constexpr (mode == FETCH_DB_ID) {
        std::string dbId;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (id) {
                dbId = id;
            }
        }
        else {
            std::cerr << "No matching id found for refr_index: " << refrIndex << std::endl;
        }
        sqlite3_finalize(stmt);
        return dbId;
    }
    else {
        int oppositeRefrIndex = -1;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            oppositeRefrIndex = sqlite3_column_int(stmt, 0);
        }
        else {
            std::cerr << "No matching DB refr_index found for JSON refr_index: " << refrIndex << std::endl;
        }
        sqlite3_finalize(stmt);
        return oppositeRefrIndex;
    }
}

std::string regexEscape(const std::string& str) {
    static const std::regex specialChars(R"([-[\]{}()*+?.,\^$|#\s])");
    return std::regex_replace(str, specialChars, R"(\$&)");
}

int processAndHandleMismatches(sqlite3* db, const std::string& query, const std::string& inputData,
    int conversionChoice, const std::unordered_set<int>& validMastersDB,
    std::unordered_map<int, int>& replacements,
    std::vector<MismatchEntry>& mismatchedEntries) {
    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();

    while (it != end) {
        std::string jsonObject = it->str();
        auto refrIndexOpt = findRefrIndex(jsonObject);
        auto idOpt = findId(jsonObject);

        if (refrIndexOpt && idOpt) {
            int refrIndex = *refrIndexOpt;
            std::string id = *idOpt;
            int currentMastIndex = fetchCurrentMastIndex(jsonObject);
            int newRefrIndex = fetchRefIndex(db, query, refrIndex, id);

            if (newRefrIndex != -1) {
                replacements[refrIndex] = newRefrIndex;
                logMessage("Will replace JSON refr_index " + std::to_string(refrIndex) + " with DB refr_index " + std::to_string(newRefrIndex) + " for JSON id: " + id);
            }
            else if (currentMastIndex == 2 || currentMastIndex == 3) {
                int oppositeRefrIndex = fetchValue<FETCH_OPPOSITE_REFR_INDEX>(db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);
                std::string dbId = fetchValue<FETCH_DB_ID>(db, refrIndex, currentMastIndex, validMastersDB, conversionChoice);
                mismatchedEntries.emplace_back(MismatchEntry{ refrIndex, id, dbId, oppositeRefrIndex });
                logMessage("Mismatch found for JSON refr_index " + std::to_string(refrIndex) + " with JSON id: " + id + " with DB refr_index: " + std::to_string(oppositeRefrIndex) + " with DB id: " + dbId);
            }
        }
        ++it;
    }

    int mismatchChoice = getUserChoice();

    if (mismatchChoice == 1) {
        for (const auto& entry : mismatchedEntries) {
            int refrIndex = entry.refrIndex;
            int oppositeRefrIndex = entry.oppositeRefrIndex;

            if (oppositeRefrIndex != -1) {
                replacements[refrIndex] = oppositeRefrIndex;
                logMessage("Replaced JSON refr_index " + std::to_string(refrIndex) + " with DB refr_index: " + std::to_string(oppositeRefrIndex));
            }
        }
    }
    else {
        logMessage("Mismatched entries will remain unchanged.");
    }

    return mismatchChoice;
}

void optimizeJsonReplacement(std::ostringstream& outputStream, std::string_view inputData, const std::unordered_map<int, int>& replacements) {
    size_t pos = 0, lastPos = 0;
    const std::string mastKey = "\"mast_index\":";
    const std::string refrKey = "\"refr_index\":";
    const size_t mastKeyLen = mastKey.length();
    const size_t refrKeyLen = refrKey.length();

    while ((pos = inputData.find(mastKey, lastPos)) != std::string::npos) {
        outputStream << inputData.substr(lastPos, pos - lastPos);

        size_t endPos = inputData.find_first_of(",}", pos);
        int currentMastIndex = std::stoi(std::string(inputData.substr(pos + mastKeyLen, endPos - pos - mastKeyLen)));

        size_t refrIndexPos = inputData.find(refrKey, endPos);
        if (refrIndexPos == std::string::npos) {
            outputStream << inputData.substr(lastPos);
            break;
        }

        size_t refrEndPos = inputData.find_first_of(",}", refrIndexPos);
        int currentIndex = std::stoi(std::string(inputData.substr(refrIndexPos + refrKeyLen, refrEndPos - refrIndexPos - refrKeyLen)));

        outputStream << mastKey << " " << currentMastIndex << ",\n        ";
        outputStream << refrKey << " " << (replacements.count(currentIndex) ? replacements.at(currentIndex) : currentIndex);

        lastPos = refrEndPos;
    }

    if (lastPos < inputData.size()) {
        outputStream << inputData.substr(lastPos);
    }
}

bool saveJsonToFile(const std::filesystem::path& jsonFilePath, const std::string& outputData) {
    std::ofstream outputFile(jsonFilePath);
    if (outputFile) {
        outputFile << outputData;
        logMessage("Modified JSON saved as: " + jsonFilePath.string());
        return true;
    }
    return false;
}

bool convertJsonToEsp(const std::filesystem::path& jsonFilePath, const std::filesystem::path& espFilePath) {
    std::string command = "tes3conv.exe \"" + jsonFilePath.string() + "\" \"" + espFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        return false;
    }
    logMessage("Final conversion to ESM/ESP successful: " + espFilePath.string());
    return true;
}

int main() {
    std::cout << PROGRAM_NAME << "\n" << PROGRAM_VERSION << "\n" << PROGRAM_AUTHOR << "\n\n";

    clearLogFile("tes3ric_log.txt");

    std::filesystem::path dbFilePath = "tes3_ru-en_refr_index.db";
    if (!std::filesystem::exists(dbFilePath)) {
        logErrorAndExit(nullptr, "Database file 'tes3_ru-en_refr_index.db' not found.\n");
    }

    sqlite3* db = nullptr;

    if (sqlite3_open(dbFilePath.string().c_str(), &db)) {
        logErrorAndExit(db, "Failed to open database: " + std::string(sqlite3_errmsg(db)) + "\n");
    }
    logMessage("Database opened successfully...");

    std::filesystem::path converterPath = "tes3conv.exe";
    if (!std::filesystem::exists(converterPath)) {
        logErrorAndExit(db, "tes3conv.exe not found. Please download the latest version from\n"
            "https://github.com/Greatness7/tes3conv/releases and place it in\nthe same directory with this program.\n");
    }
    logMessage("tes3conv.exe found.\n");

    int ConversionChoice = getConversionChoice();

    std::filesystem::path inputFilePath = getInputFilePath();
    std::filesystem::path inputPath(inputFilePath);

    std::filesystem::path outputDir = inputPath.parent_path();
    std::filesystem::path jsonFilePath = outputDir / (inputPath.stem() += ".json");
    std::string command = "tes3conv.exe \"" + inputPath.string() + "\" \"" + jsonFilePath.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        logErrorAndExit(db, "Error converting to JSON. Check tes3conv.exe and the input file.\n");
    }
    logMessage("Conversion to JSON successful: " + jsonFilePath.string());

    std::ifstream inputFile(jsonFilePath);
    if (!inputFile) {
        logErrorAndExit(db, "Failed to open JSON file for reading: " + jsonFilePath.string() + "\n");
    }
    std::string inputData((std::istreambuf_iterator<char>(inputFile)), std::istreambuf_iterator<char>());
    inputFile.close();

    std::regex jsonObjectRegex(R"(\{[^{}]*\"mast_index\"[^\}]*\})");
    std::string outputData = inputData;

    auto it = std::sregex_iterator(inputData.begin(), inputData.end(), jsonObjectRegex);
    auto end = std::sregex_iterator();

    std::string query = (ConversionChoice == 1) ?
        "SELECT refr_index_EN FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_RU = ? AND id = ?;" :
        "SELECT refr_index_RU FROM [tes3_T-B_ru-en_refr_index] WHERE refr_index_EN = ? AND id = ?;";

    auto [isValid, validMastersDB] = checkDependencyOrder(inputData);
    if (!isValid) {
        logErrorAndExit(db, "Required Parent Masters not found or in the wrong order.\n");
    }

    int mismatchChoice = processAndHandleMismatches(db, query, inputData, ConversionChoice, validMastersDB, replacements, mismatchedEntries);

    if (replacements.empty()) {
        logErrorAndExit(db, "No replacements found. Conversion canceled.\n");
    }

    std::ostringstream outputStream;
    optimizeJsonReplacement(outputStream, inputData, replacements);
    outputData = outputStream.str();

    std::filesystem::path newJsonFilePath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + ".json");
    if (!saveJsonToFile(newJsonFilePath, outputData)) {
        logErrorAndExit(db, "Error saving modified JSON file.\n");
    }

    std::filesystem::path outputExtension = inputPath.extension();
    std::filesystem::path newEspPath = outputDir / ("CONV_" + std::string(ConversionChoice == 1 ? "RUtoEN" : "ENtoRU") + "_" + inputPath.stem().string() + outputExtension.string());

    if (!convertJsonToEsp(newJsonFilePath, newEspPath)) {
        logErrorAndExit(db, "Error converting JSON back to ESM/ESP.\n");
    }

    sqlite3_close(db);
    logMessage("Conversion complete.\n");
    std::cout << "Press Enter to continue...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return 0;
}
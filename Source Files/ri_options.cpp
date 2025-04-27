#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "ri_options.h"

// Function to parse command-line arguments
ProgramOptions parseArguments(int argc, char* argv[]) {
    ProgramOptions options;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        std::string argLower = arg;
        std::transform(argLower.begin(), argLower.end(), argLower.begin(), ::tolower);

        if (argLower == "--batch" || argLower == "-b") {
            options.batchMode = true;
        }
        else if (argLower == "--silent" || argLower == "-s") {
            options.silentMode = true;
        }
        else if (argLower == "--ru-to-en" || argLower == "-1") {
            options.conversionType = 1;
        }
        else if (argLower == "--en-to-ru" || argLower == "-2") {
            options.conversionType = 2;
        }
        else if (argLower == "--help" || argLower == "-h") {
            std::cout << "================================\n"
                << "TES3 Refr_Index Converter - Help\n"
                << "================================\n\n"
                << "Usage:\n"
                << "  .\\tes3_ri_converter.exe [OPTIONS] [TARGETS]\n\n"
                << "Options:\n"
                << "  -b, --batch      Enable batch mode (required when processing multiple files)\n"
                << "  -s, --silent     Suppress non-critical messages (faster conversion)\n"
                << "  -1, --ru-to-en   Convert Russian 1C -> English GOTY\n"
                << "  -2, --en-to-ru   Convert English GOTY -> Russian 1C\n"
                << "  -h, --help       Show this help message\n\n"
                << "Target Formats:\n\n"
                << "  Single File (works without batch mode):\n"
                << "    mod-in-the-same-folder.esp\n"
                << "    C:\\Morrowind\\Data Files\\mod.esm\n\n"
                << "  Multiple Files (requires -b batch mode):\n"
                << "    file1.esp;file2.esm;file 3.esp\n"
                << "    D:\\Mods\\mod.esp;C:\\Morrowind\\Data Files\\Master mod.esm;Mod-in-the-same-folder.esp\n\n"
                << "  Entire Directory (batch mode, recursive processing):\n"
                << "    C:\\Morrowind\\Data Files\\\n"
                << "    .\\Data\\  (relative path)\n\n";

            // Wait for user input before exiting (Windows)
        #ifndef __linux__
            std::cout << "\nPress Enter to exit...";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        #endif

            std::exit(EXIT_SUCCESS);
        }
        else {
            options.inputFiles.emplace_back(arg);
        }
    }

    return options;
}
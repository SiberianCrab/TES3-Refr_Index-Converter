# TES3-Refr_Index-Converter

 A simple command-line tool that lets you convert Refr_Index'es in TES3 Morrowind .esp/.esm files from russian 1C to english game version and vice versa.

 Requires the latest version of tes3conv.exe from Greatness7 (https://github.com/Greatness7/tes3conv) to run.

Usage:
  .\"TES3 Refr_Index Converter.exe" [OPTIONS] [TARGETS]

Options:
  -b, --batch      Enable batch mode (auto-accept all changes)
  -s, --silent     Suppress non-critical messages
  -1, --ru-to-en   Convert Russian 1C -> English GOTY
  -2, --en-to-ru   Convert English GOTY -> Russian 1C
  -h, --help       Show this help message

Target Formats:
  - Directory (recursive processing):
    "C:\Morrowind\Data Files\"
    .\Data\  (relative path)

  - Single/Multiple Files:
    file.esp
    "file with spaces.esm"
    file1.esp file2.esm "file 3.esp"

Path Handling Rules:
  - Always quote paths with spaces
  - Use double backslashes (\) or forward slashes (/)
  - Relative paths start from program's directory

Wildcards Support:
  - CMD: Only current folder (*.esp)
  - PowerShell (recommended for recursive):
    & .\"TES3_Converter.exe" -1 (Get-ChildItem -Recurse -Filter "*.esp").FullName

Shell Specifics:
  - CMD:
    .\"TES3_Converter.exe" -1 "C:\Mods\file.esp"

  - PowerShell:
    & .\"TES3_Converter.exe" -1 "D:\Modding\my mod.esp"

Example Commands:
  - Convert entire folder:
    .\"TES3_Converter.exe" -b -1 "C:\Morrowind\Data Files\"

  - Convert multiple specific files:
    .\"TES3_Converter.exe" -2 "C:\Mods\My Mod RU.esp" My_Mod.esm

  - Silent mode with PowerShell:
    & .\"TES3_Converter.exe" -s -1 (Get-ChildItem -Recurse -Filter "*_RU.esp").FullName

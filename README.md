# TES3-Refr_Index-Converter

A simple command-line tool that lets you convert `Refr_Index` values in TES3 Morrowind `.esp`/`.esm` files from Russian 1C to English GOTY version and vice versa.

Requires the latest version of `tes3conv.exe` from Greatness7: [https://github.com/Greatness7/tes3conv](https://github.com/Greatness7/tes3conv)

---

Since the release of the Russian version of Morrowind (published by 1C), there has been an incompatibility issue between English and Russian mods that modify objects in the Tribunal and Bloodmoon expansions.

When using mods designed for the other version of the game, affected objects become duplicated. This occurs due to differences in Refr_Index values (object identifiers) between the English (GOTY) and Russian (1C) versions. For example:

https://pic.fullrest.ru/XYIDAKc9.png

---

## Usage

### Windows
```bash
.\tes3_ri_converter.exe [OPTIONS] "[TARGETS]"
```

### Linux
```bash
./tes3_ri_converter [OPTIONS] "[TARGETS]"
```

---

## Options

| Option        | Description                                             |
|---------------|---------------------------------------------------------|
| `-b`, `--batch`    | Enable batch mode (required when processing multiple files) |
| `-s`, `--silent`   | Suppress non-critical messages (faster conversion)        |
| `-1`, `--ru-to-en` | Convert Russian 1C → English GOTY                        |
| `-2`, `--en-to-ru` | Convert English GOTY → Russian 1C                        |
| `-h`, `--help`     | Show help message                                  |

---

## Target Formats

### Single File (works without batch mode):

#### Windows
```
mod-in-the-same-folder.esp  
C:\Morrowind\Data Files\mod.esm
```

#### Linux
```
mod-in-the-same-folder.esp  
/home/user/morrowind/Data Files/mod.esm
```

---

### Multiple Files (requires -b batch mode):

#### Windows
```
file1.esp;file2.esm;file 3.esp  
:\Mods\mod.esp;C:\Morrowind\Data Files\Master mod.esm;Mod-in-the-same-folder.esp
```

#### Linux
```
file1.esp;file2.esm;file 3.esp  
/mnt/data/mods/file1.esp;/home/user/morrowind/Data Files/Master mod.esm;mod-in-the-same-folder.esp
```

---

### Entire Directory (batch mode, recursive processing):

#### Windows
```
C:\Morrowind\Data Files\  
.\Data\  (relative path)
```

#### Linux
```
/home/user/morrowind/Data Files/  
./Data/  (relative path)
```

---

## Important Notes

### Supported:
- ASCII-only file paths (English letters, numbers, standard symbols)
- Both absolute (`C:\...`) and relative (`.\Data\...`) paths

### Not Supported:
- Paths containing non-ASCII characters (e.g., Cyrillic, Chinese, special symbols)
- Wildcards (`*`, `?`) in CMD

### Solution for Non-ASCII Paths:
If your files are in a folder with non-ASCII characters (e.g., `C:\Игры\Morrowind\`),  
move them to a folder with only English characters (`C:\Games\Morrowind\`).

---

## Shell Compatibility

### PowerShell (Recommended on Windows)
- Fully supports batch processing, recursive search, and wildcards

### CMD (Limited Support)
- Does not support recursive file selection with wildcards

### Bash/Zsh (on Linux)
- Fully supports batch processing and wildcard expansion

---

## Wildcard Support

### PowerShell (Recommended for Windows)

Convert all `.esp` files recursively in current folder:
```powershell
& .\tes3_ri_converter.exe -b (Get-ChildItem -Recurse -Include "*.esp").FullName
```

Convert all `.esm` files in specific folder (without subfolders):
```powershell
& .\tes3_ri_converter.exe -b (Get-ChildItem -Path "C:\Mods\" -Include "*.esm").FullName
```

Convert all `.esm` files in specific folder recursively:
```powershell
& .\tes3_ri_converter.exe -b (Get-ChildItem -Path "C:\Mods\" -Recurse -Include "*.esm" -File).FullName
```

---

### CMD (Limited Wildcard Support, No Recursion)

Convert all `.esp` files in current folder:
```cmd
for %f in ("*.esp") do tes3_ri_converter.exe -b -2 "%~f"
```

Convert all `.esm` files in specific folder (without subfolders):
```cmd
for %f in ("C:\Mods\*.esm") do tes3_ri_converter.exe -b -2 "%~f"
```

---

### Bash/Zsh (Full Wildcard Support on Linux)

Convert all `.esp` files recursively in current folder:
```bash
find . -type f -iname "*.esp" -exec ./tes3_ri_converter -b -2 {} \;
```

Convert all `.esm` files in specific folder (without subfolders):
```bash
find /path/to/mods -maxdepth 1 -type f -iname "*.esm" -exec ./tes3_ri_converter -b -2 {} \;
```

Convert all `.esm` files in specific folder recursively:
```bash
find /path/to/mods -type f -iname "*.esm" -exec ./tes3_ri_converter -b -2 {} \;
```

---

## Example Commands

Convert an entire folder:
```powershell
& .\tes3_ri_converter.exe -b -1 "C:\Morrowind\Data Files\"
```

```bash
./tes3_ri_converter -b -1 "/home/user/morrowind/Data Files/"
```

Convert multiple specific files:
```powershell
& .\tes3_ri_converter.exe -b -2 "D:\Mods\mod.esp;Mod-in-the-same-folder.esp"
```

```bash
./tes3_ri_converter -b -2 "/mnt/data/mods/mod.esp;./Mod-in-the-same-folder.esp"
```

Convert all files starting with `RR_` in a folder:
```powershell
& .\tes3_ri_converter.exe -b (Get-ChildItem -Path "C:\Morrowind\Data Files\" -Recurse -Include "RR_*.esp").FullName
```

```bash
find "/home/user/morrowind/Data Files/" -type f -iname "RR_*.esp" -exec ./tes3_ri_converter -b -1 "{}" \;
```

#include "ri_mismatches.h"

// Represents a data mismatch between JSON source and database records
bool MismatchEntry::operator==(const MismatchEntry& other) const noexcept {
    return refrIndexJson == other.refrIndexJson && idJson == other.idJson;
}

// Hash function specialization for MismatchEntry to enable unordered_set usage
size_t std::hash<MismatchEntry>::operator()(const MismatchEntry& e) const {
    size_t h1 = hash<int>{}(e.refrIndexJson);
    size_t h2 = std::hash<std::string>{}(e.idJson);
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}
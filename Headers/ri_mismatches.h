#pragma once
#include <string>
#include <unordered_set>

// Represents a data mismatch between JSON source and database records
struct MismatchEntry {
    int refrIndexJson;
    std::string idJson;
    std::string idDb;
    int refrIndexDb;

    // Equality comparison operator
    bool operator==(const MismatchEntry& other) const noexcept;
};

// Hash function specialization for MismatchEntry to enable unordered_set usage
namespace std {
    template<> struct hash<MismatchEntry> {
        size_t operator()(const MismatchEntry& e) const;
    };
}
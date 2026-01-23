#include "coefficient_io.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <sys/stat.h>

std::vector<LUTEntry> CoefficientIO::load(const std::string& path) {
    std::vector<LUTEntry> entries;
    std::ifstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open coefficient file: " + path);
    }

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;

        // Skip empty lines
        if (line.empty()) {
            continue;
        }

        std::istringstream iss(line);
        std::string c0_str, c1_str, c2_str;

        if (!(iss >> c0_str >> c1_str >> c2_str)) {
            throw std::runtime_error("Invalid format at line " + std::to_string(line_num) +
                                     " in file: " + path);
        }

        LUTEntry entry;
        try {
            entry.c0 = std::stoul(c0_str, nullptr, 16);
            entry.c1 = std::stoul(c1_str, nullptr, 16);
            entry.c2 = std::stoul(c2_str, nullptr, 16);
        } catch (const std::exception& e) {
            throw std::runtime_error("Failed to parse hex values at line " +
                                     std::to_string(line_num) + " in file: " + path);
        }

        entries.push_back(entry);
    }

    if (entries.empty()) {
        throw std::runtime_error("No coefficients loaded from file: " + path);
    }

    return entries;
}

void CoefficientIO::save(const std::string& path, const std::vector<LUTEntry>& entries) {
    std::ofstream file(path);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file for writing: " + path);
    }

    // Write in the same format as the original: uppercase hex, space-separated
    for (const auto& entry : entries) {
        char line[64];
        snprintf(line, sizeof(line), "%X %X %X\n", entry.c0, entry.c1, entry.c2);
        file << line;
    }

    if (!file.good()) {
        throw std::runtime_error("Error writing to file: " + path);
    }
}

void CoefficientIO::backup(const std::string& path) {
    if (!exists(path)) {
        throw std::runtime_error("Cannot backup non-existent file: " + path);
    }

    std::string backup_path = path + ".backup";

    // Read original file
    std::ifstream src(path, std::ios::binary);
    if (!src) {
        throw std::runtime_error("Failed to open source file for backup: " + path);
    }

    // Write backup file
    std::ofstream dst(backup_path, std::ios::binary);
    if (!dst) {
        throw std::runtime_error("Failed to create backup file: " + backup_path);
    }

    dst << src.rdbuf();

    if (!dst.good() || !src.good()) {
        throw std::runtime_error("Error during backup operation");
    }
}

bool CoefficientIO::exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

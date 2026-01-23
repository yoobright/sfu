#ifndef COEFFICIENT_IO_H
#define COEFFICIENT_IO_H

#include <vector>
#include <string>
#include "sfu_lut.h"

class CoefficientIO {
public:
    // Load coefficients from hex format file
    static std::vector<LUTEntry> load(const std::string& path);

    // Save coefficients in hex format (preserves original format)
    static void save(const std::string& path, const std::vector<LUTEntry>& entries);

    // Create backup of existing file
    static void backup(const std::string& path);

    // Verify file exists and is readable
    static bool exists(const std::string& path);
};

#endif // COEFFICIENT_IO_H

#include "readfile.hpp"

#include <fstream>
#include <string>
#include <vector>

std::vector<char> readFile(std::string file_name) {
    std::ifstream ifs;
    ifs.open(file_name, std::ios::ate | std::ios::binary);
    if (!ifs.is_open()) throw std::runtime_error("Failed to open file!");

    std::vector<char> buffer(static_cast<std::vector<char>::size_type>(
            ifs.tellg()));

    ifs.seekg(0, std::ios::beg);
    ifs.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    ifs.close();

    return buffer;
}

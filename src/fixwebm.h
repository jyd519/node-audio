#pragma once

#include <string>
#include <map>

bool remuxWebmFile(const std::string& inputPath, const std::string& outputPath, const std::map<std::string, std::string>& metadata = {});


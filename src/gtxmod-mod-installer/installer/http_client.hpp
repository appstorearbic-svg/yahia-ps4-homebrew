#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace gtxhttp {
typedef bool (*ProgressCallback)(uint64_t received, uint64_t total, void *context);
bool initialize(std::string *error = nullptr);
void shutdown();
bool getText(const std::string &url, std::string &body, std::string *error = nullptr);
bool download(const std::string &url, const std::string &destination,
              ProgressCallback callback, void *context, std::string *error = nullptr);
}

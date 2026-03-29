#pragma once

#include <string>

namespace GeminiTranscription {

struct Result {
  bool finished = false;
  bool success = false;
  std::string transcript;
  std::string error;
};

bool start(const std::string& wavPath);
Result poll();
bool configured();
bool saveApiKey(const std::string& apiKey);
bool clearApiKey();
std::string apiKeyLast4();

}  // namespace GeminiTranscription

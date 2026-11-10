#pragma once
#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>

constexpr int FILE_READ = 0;

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(FILE* file) : file_(file) {}
  FsFile(FsFile&& other) noexcept : file_(std::exchange(other.file_, nullptr)) {}
  FsFile& operator=(FsFile&& other) noexcept {
    close();
    file_ = std::exchange(other.file_, nullptr);
    return *this;
  }
  ~FsFile() { close(); }
  explicit operator bool() const { return file_ != nullptr; }
  bool isOpen() const { return file_ != nullptr; }
  void close() { if (file_) std::fclose(std::exchange(file_, nullptr)); }
  uint64_t size() const {
    const long saved = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    const long size = std::ftell(file_);
    std::fseek(file_, saved, SEEK_SET);
    return static_cast<uint64_t>(size);
  }
  uint32_t position() const { return static_cast<uint32_t>(std::ftell(file_)); }
  bool seek(uint32_t position) { return std::fseek(file_, position, SEEK_SET) == 0; }
  size_t read(void* buffer, size_t length) { return std::fread(buffer, 1, length, file_); }
 private:
  FILE* file_ = nullptr;
};

struct TestSdManager {
  FsFile open(const char* path, int) {
    // Distinct face paths can share a test fixture's bytes, while the actual
    // ExternalFont cache still sees and manages each distinct path normally.
    std::string filename(path);
    const auto suffix = filename.find("?face=");
    if (suffix != std::string::npos) filename.resize(suffix);
    return FsFile(std::fopen(filename.c_str(), "rb"));
  }
};
inline TestSdManager SdMan;

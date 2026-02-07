#ifndef CMP_FILE_HPP
#define CMP_FILE_HPP

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "file.hpp"

namespace vt {

class exception : public std::exception {
 private:
  std::string message_;

 public:
  exception() = default;
  explicit exception(const std::string& message) : message_(message) {}

  template <typename T>
  exception& operator<<(const T& value) {
    message_ += std::to_string(value);
    return *this;
  }

  exception& operator<<(const std::string& value) {
    message_ += value;
    return *this;
  }

  exception& operator<<(const char* value) {
    message_ += value;
    return *this;
  }

  exception& operator<<(char value) {
    message_ += value;
    return *this;
  }

  const char* what() const noexcept override {
    return message_.c_str();
  }
};

class cmp_file {
 private:
  std::unique_ptr<file> libc_;
  std::unique_ptr<file> vtpc_;

 public:
  cmp_file(std::unique_ptr<file> libc, std::unique_ptr<file> vtpc)
      : libc_(std::move(libc)), vtpc_(std::move(vtpc)) {}

  void seek(std::size_t offset) {
    libc_->seek(offset);
    vtpc_->seek(offset);
  }

  void write(std::string_view data) {
    libc_->write(data);
    vtpc_->write(data);
  }

  std::string read(std::size_t size) {
    std::string libc_data = libc_->read(size);
    std::string vtpc_data = vtpc_->read(size);
    if (libc_data != vtpc_data) {
      throw exception() << "read data mismatch: '" << libc_data << "' != '" << vtpc_data << "'";
    }
    return libc_data;
  }

  void sync() {
    libc_->sync();
    vtpc_->sync();
  }
};

}  // namespace vt

#endif  // CMP_FILE_HPP
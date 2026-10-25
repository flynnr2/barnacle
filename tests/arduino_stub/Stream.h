#pragma once
// Compile upstream stream helpers; host protocol tests never use serial I/O.
#include <stddef.h>
#include <stdint.h>
class Stream {
public:
  void print(char) {}
  void print(const char*) {}
  void println() {}
  size_t readBytes(uint8_t*, size_t) { return 0; }
  size_t write(const uint8_t*, size_t) { return 0; }
};

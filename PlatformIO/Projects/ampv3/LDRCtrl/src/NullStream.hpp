#pragma once
// A Stream that swallows everything: for running console-mode code where its text must go
// nowhere (the protocol mode's CAL and VERIFY).
#include <Arduino.h>

class NullStream : public Stream {
public:
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *buffer, size_t size) override { return size; }
};

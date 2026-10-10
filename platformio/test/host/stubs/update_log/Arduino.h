#pragma once

#include <cstdio>
#include <string>

struct TestSerial
{
  std::string output;

  template<typename... Args>
  void printf(const char* format, Args... args)
  {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), format, args...);
    output += buffer;
  }
};

extern TestSerial Serial;

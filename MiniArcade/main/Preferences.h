// NVS backed stand-in for the Arduino Preferences library
#pragma once
#include <stdint.h>
#include <stddef.h>

class Preferences {
public:
  bool     begin(const char *ns, bool readOnly = false);
  void     end();
  uint16_t getUShort(const char *key, uint16_t def);
  void     putUShort(const char *key, uint16_t val);
  size_t   getBytes(const char *key, void *buf, size_t len);
  size_t   putBytes(const char *key, const void *buf, size_t len);
private:
  uint32_t h = 0;
  bool     ro = true;
};

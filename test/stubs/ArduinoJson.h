#pragma once
// Minimal native-only ArduinoJson stand-in for the CPREBOOT mailbox test.
// It is never included by Teensy PlatformIO's normal firmware build.
struct JsonVariantConst {
  bool isNull() const { return true; }
  JsonVariantConst operator[](const char *) const { return {}; }
  template<class T> T operator|(const T &defaultValue) const {
    return defaultValue;
  }
};
struct JsonObject {
  JsonObject operator[](const char *) { return {}; }
  template<class T> T to() { return {}; }
  template<class T> JsonObject &operator=(const T &) { return *this; }
};

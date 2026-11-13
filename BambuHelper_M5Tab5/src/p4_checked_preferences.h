#pragma once

#if __has_include(<Preferences.h>)
#include <Preferences.h>
#endif
#if defined(BOARD_IS_WS_P4_7B) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#endif
#include <cstdlib>
#include <cstring>
#include <new>

// P4 settings are accepted only after every write has committed and reads back
// as the requested value. In particular, putString("") returning zero is not
// an error. The verification window is explicit so unrelated best-effort NVS
// maintenance cannot change the outcome of a settings commit.
template<typename Store>
class P4CheckedPreferences {
  Store store_;
  bool checking_ = false;
  bool ok_ = true;

  void check(bool ok) { if (checking_ && !ok) ok_ = false; }
  bool stringMatches(const char* key, const char* expected) {
    if (store_.getType(key) != PT_STR) return false;
    return store_.getString(key, "nvs-read-failed") == expected;
  }

public:
  Store& backend() { return store_; }
  void beginWriteVerification() { checking_ = true; ok_ = true; }
  bool endWriteVerification() { checking_ = false; return ok_; }
  void markWriteFailure() { check(false); }

  bool begin(const char* name, bool readOnly = false) {
    const bool ok = store_.begin(name, readOnly);
    check(ok);
    return ok;
  }
  void end() { store_.end(); }
  bool isKey(const char* key) { return store_.isKey(key); }
  PreferenceType getType(const char* key) { return store_.getType(key); }
  size_t getBytesLength(const char* key) { return store_.getBytesLength(key); }
  size_t getBytes(const char* key, void* value, size_t size) {
    return store_.getBytes(key, value, size);
  }
  uint8_t getUChar(const char* key, uint8_t fallback = 0) {
    return store_.getUChar(key, fallback);
  }
  uint16_t getUShort(const char* key, uint16_t fallback = 0) {
    return store_.getUShort(key, fallback);
  }
  uint32_t getUInt(const char* key, uint32_t fallback = 0) {
    return store_.getUInt(key, fallback);
  }
  uint32_t getULong(const char* key, uint32_t fallback = 0) {
    return store_.getULong(key, fallback);
  }
  bool getBool(const char* key, bool fallback = false) {
    return store_.getBool(key, fallback);
  }
  int16_t getShort(const char* key, int16_t fallback = 0) {
    return store_.getShort(key, fallback);
  }
  float getFloat(const char* key, float fallback = 0) {
    return store_.getFloat(key, fallback);
  }
  String getString(const char* key, const String& fallback = String()) {
    return store_.getString(key, fallback);
  }

  size_t putUChar(const char* key, uint8_t value) {
    const size_t result = store_.putUChar(key, value);
    check(result == 1 && store_.getUChar(key, uint8_t(value + 1)) == value);
    return result;
  }
  size_t putUShort(const char* key, uint16_t value) {
    const size_t result = store_.putUShort(key, value);
    check(result == sizeof(value) && store_.getUShort(key, uint16_t(value + 1)) == value);
    return result;
  }
  size_t putUInt(const char* key, uint32_t value) {
    const size_t result = store_.putUInt(key, value);
    check(result == sizeof(value) && store_.getUInt(key, value + 1) == value);
    return result;
  }
  size_t putULong(const char* key, uint32_t value) {
    const size_t result = store_.putULong(key, value);
    check(result == sizeof(value) && store_.getULong(key, value + 1) == value);
    return result;
  }
  size_t putBool(const char* key, bool value) {
    return putUChar(key, uint8_t(value));
  }
  size_t putFloat(const char* key, float value) {
    const size_t result = store_.putFloat(key, value);
    float actual = 0;
    check(result == sizeof(value) && store_.getBytesLength(key) == sizeof(value) &&
          store_.getBytes(key, &actual, sizeof(actual)) == sizeof(value) &&
          memcmp(&actual, &value, sizeof(value)) == 0);
    return result;
  }
  size_t putString(const char* key, const char* value) {
    const size_t result = store_.putString(key, value);
    check(result == strlen(value) && stringMatches(key, value));
    return result;
  }
  size_t putString(const char* key, const String& value) {
    return putString(key, value.c_str());
  }
  size_t putBytes(const char* key, const void* value, size_t length) {
    const size_t result = store_.putBytes(key, value, length);
    // NVS operations temporarily disable the Flash cache. Readback stays in
    // the storage owner, while its potentially large data buffer belongs in
    // PSRAM rather than the internal region needed by network descriptors.
#if defined(BOARD_IS_WS_P4_7B) && __has_include(<esp_heap_caps.h>)
    void* actual = length ? heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (!actual && length) actual = std::malloc(length);
#else
    void* actual = length ? std::malloc(length) : nullptr;
#endif
    check(result == length && actual && store_.getBytesLength(key) == length &&
          store_.getBytes(key, actual, length) == length &&
          memcmp(actual, value, length) == 0);
#if defined(BOARD_IS_WS_P4_7B) && __has_include(<esp_heap_caps.h>)
    heap_caps_free(actual);
#else
    std::free(actual);
#endif
    return result;
  }

  bool remove(const char* key) {
    const bool existed = store_.isKey(key);
    const bool result = store_.remove(key);
    check((result || !existed) && !store_.isKey(key));
    return result;
  }
  bool clear() {
    const bool result = store_.clear();
    check(result);
    return result;
  }
};

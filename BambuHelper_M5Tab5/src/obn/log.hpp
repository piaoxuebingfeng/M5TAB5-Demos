// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Wire logs may contain camera credentials. Production builds keep them off.
// The standalone development probe opts in to numeric progress events only.
#define OBN_TRACE(...) do {} while (0)
#if defined(BAMBU_CAMERA_DEBUG) && !defined(ESP_PLATFORM)
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
namespace bambu_camera_debug {
inline std::array<std::string,3> secrets;
inline std::mutex mutex;
inline void emit(const char* format,...){
  if(strstr(format,"session_token"))return;
  char buffer[4096];va_list args;va_start(args,format);vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
  std::string line=buffer;
  for(const auto& secret:secrets){
    if(secret.empty())continue;
    size_t at=0;while((at=line.find(secret,at))!=std::string::npos){line.replace(at,secret.size(),"[redacted]");at+=10;}
  }
  std::lock_guard<std::mutex> lock(mutex);fprintf(stderr,"[camera] %s\n",line.c_str());
}
}
#define OBN_DEBUG(...) bambu_camera_debug::emit(__VA_ARGS__)
#define OBN_INFO(...) bambu_camera_debug::emit(__VA_ARGS__)
#define OBN_WARN(...) bambu_camera_debug::emit(__VA_ARGS__)
#define OBN_ERROR(...) bambu_camera_debug::emit(__VA_ARGS__)
#else
#define OBN_DEBUG(...) do {} while (0)
#define OBN_INFO(...) do {} while (0)
#define OBN_WARN(...) do {} while (0)
#define OBN_ERROR(...) do {} while (0)
#endif

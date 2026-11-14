#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>

inline bool copyCloudImageUrl(const char* candidate,char* out,size_t capacity) {
  if (!candidate || strncmp(candidate,"https://",8)!=0 || strlen(candidate)>=capacity) return false;
  strlcpy(out,candidate,capacity); return true;
}
// Task-list fallbacks must match the active task and plate, never simply pick
// the account's latest image (which can belong to another printer or job).
inline bool selectCloudThumbnail(JsonObjectConst root,const char* task,int plate,char* out,size_t capacity) {
  out[0]=0;
  const auto plates=root["context"]["plates"].as<JsonArrayConst>();
  for (JsonObjectConst p : plates) {
    if (p["index"].as<int>()!=plate && !(plate==0 && plates.size()==1)) continue;
    if (copyCloudImageUrl(p["thumbnail"]["url"] | "",out,capacity)) return true;
  }
  auto matchingCover = [&](JsonObjectConst entry) {
    char id[32] = {};
    if (entry["id"].is<const char*>()) strlcpy(id,entry["id"],sizeof(id));
    else if (entry["id"].is<long long>()) snprintf(id,sizeof(id),"%lld",entry["id"].as<long long>());
    if (!id[0] || strcmp(id,task)!=0) return false;
    if (plate>0 && entry["plateIndex"].is<int>() && entry["plateIndex"].as<int>()!=plate) return false;
    return copyCloudImageUrl(entry["cover"] | "",out,capacity);
  };
  if (matchingCover(root)) return true;
  for (JsonObjectConst entry : root["hits"].as<JsonArrayConst>()) if (matchingCover(entry)) return true;
  return false;
}

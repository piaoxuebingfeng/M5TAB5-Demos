#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

// The MQTT subtask and task-list title can be a slicing preset. Select a
// model/plate name only for the exact current job, printer and plate.
inline bool selectCloudJobName(JsonObjectConst root,const char* task,const char* serial,int plate,
                               char* out,size_t capacity){
  if(!out||!capacity)return false;
  out[0]=0;
  if(!task||!task[0]||!strcmp(task,"0")||!serial||!serial[0])return false;
  for(JsonObjectConst entry:root["hits"].as<JsonArrayConst>()){
    char id[32]={};
    if(entry["id"].is<const char*>())snprintf(id,sizeof(id),"%s",entry["id"].as<const char*>());
    else if(entry["id"].is<long long>())snprintf(id,sizeof(id),"%lld",entry["id"].as<long long>());
    if(strcmp(id,task)||strcmp(entry["deviceId"]|"",serial))continue;
    if(plate>0&&(!entry["plateIndex"].is<int>()||entry["plateIndex"].as<int>()!=plate))continue;
    for(const char* field:{"plateName","designTitleTranslated","designTitle"}){
      const char* name=entry[field]|"";
      while(*name==' '||*name=='\t')++name;
      if(!name[0])continue;
      size_t n=std::min(strlen(name),capacity-1);
      // A clipped UTF-8 code point must not reach the font decoder.
      if(n<strlen(name))while(n&&(static_cast<unsigned char>(name[n])&0xc0)==0x80)--n;
      while(n&&(name[n-1]==' '||name[n-1]=='\t'))--n;
      if(!n)continue;
      memcpy(out,name,n);out[n]=0;return true;
    }
  }
  return false;
}

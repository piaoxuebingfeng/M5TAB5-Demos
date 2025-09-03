#include <Arduino.h>
#include "LogUtil.h"

void log(String message){
  Serial.println(message);
}
 
void logError(String message){
  if (LOG_LEVEL >= LOG_LEVEL_ERROR){
    Serial.println("ERROR: " + String(message));
  }
}
 
void logWarning(String message){
  if (LOG_LEVEL >= LOG_LEVEL_WARNING){
    Serial.println("WARNING: " + String(message));
  }
}
 
void logInfo(String message){
  if (LOG_LEVEL >= LOG_LEVEL_INFO){
    Serial.print(String(message));
  }
}

void logInfoln(String message){
  if (LOG_LEVEL >= LOG_LEVEL_INFO){
    Serial.println(String(message));
  }
}
 
void logDebug(String message){
  if (LOG_LEVEL >= LOG_LEVEL_DEBUG){
    Serial.print(String(message));
  }
}
void logDebugln(String message){
  if (LOG_LEVEL >= LOG_LEVEL_DEBUG){
    Serial.println(String(message));
  }
}
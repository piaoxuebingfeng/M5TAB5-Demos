#ifndef __LOGUTIL_H
#define __LOGUTIL_H

#define LOG_LEVEL LOG_LEVEL_INFO
#define LOG_LEVEL_SILENT   0
#define LOG_LEVEL_ERROR    1
#define LOG_LEVEL_WARNING  2
#define LOG_LEVEL_INFO     3
#define LOG_LEVEL_DEBUG    4
void log(String message);
void logError(String message);
void logWarning(String message);
void logInfo(String message);
void logInfoln(String message);
void logDebug(String message);
void logDebugln(String message);

#endif

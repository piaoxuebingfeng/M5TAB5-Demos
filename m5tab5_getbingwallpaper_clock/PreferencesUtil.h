#ifndef __PreferencesUtil_H
#define __PreferencesUtil_H

#define BACK_BLACK    0
#define BACK_WHITE    1

#define BRIGHT 150
#define BRIGHT_MIN 10
#define BRIGHT_MAX 240

#define ALARM_VOICE_VALUE_DEFAULT 15
#define ALARM_VOICE_VALUE_MIN     3
#define ALARM_VOICE_VALUE_MAX     21

void getInfo();
void setInfo();
void printInfo();
void clearInfo();
void setVoice();
void setBright();
void setAlarmVoiceValue();
void setTheme();
void setTempOffset();
void setInfo4Test();

#endif

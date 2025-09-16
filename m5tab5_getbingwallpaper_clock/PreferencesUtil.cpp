#include <Preferences.h>
#include "PreferencesUtil.h"
#include "net.h"
// #include "common.h"
// #include "tftUtil.h"
// #include "Task.h"

static Preferences prefs; // 声明Preferences对象


static int backColor=BACK_BLACK;
static uint8_t lcd_bl_analog;

static uint8_t alarmvoicevalue;


// 读取一系列初始化参数
void getInfo(){
  prefs.begin("project");

    // 检查是否已经存储过数据，通过检查ssid是否存在来判断
  if (!prefs.isKey("ssid")) {
    // 如果没有存储过数据，则写入默认值
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.putString("city", city);
    prefs.putString("adm", adm);
    prefs.putString("location", location);
    prefs.putInt("bright", BRIGHT);
    prefs.putInt("alarmvoice", ALARM_VOICE_VALUE_DEFAULT);
    prefs.putInt("backColor", BACK_BLACK);
  }
  
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  city = prefs.getString("city", "");
  adm = prefs.getString("adm", "");
  location = prefs.getString("location", "");
  lcd_bl_analog = prefs.getInt("bright", BRIGHT);
  alarmvoicevalue = prefs.getInt("alarmvoice", ALARM_VOICE_VALUE_DEFAULT);
  backColor = prefs.getInt("backColor",BACK_BLACK);
  // voice = prefs.getBool("voice", true);
  // tempOffset = prefs.getFloat("tempOffset", 0.0);
  prefs.end();
}

// 写入初始化参数
void setInfo(){
  prefs.begin("project");
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putString("city", city);
  prefs.putString("adm", adm);
  prefs.putString("location", location);
  prefs.end();
}

void printInfo()
{
  Serial.printf("ssid:%s\n",ssid);
  Serial.printf("pass:%s\n",pass);
  Serial.printf("city:%s\n",city);
  Serial.printf("adm:%s\n",adm);
  Serial.printf("location:%s\n",location);
}

// // 写入声音参数
// void setVoice(){
//   prefs.begin("project");
//   prefs.putBool("voice", voice);
//   prefs.end();
// }

// 写入亮度参数
void setBright(){
  prefs.begin("project");
  prefs.putInt("bright", lcd_bl_analog);
  prefs.end();
}


void setAlarmVoiceValue(){
  prefs.begin("project");
  prefs.putInt("alarmvoice", alarmvoicevalue);
  prefs.end();
}

// // 写入温度偏移值
// void setTempOffset(){
//   prefs.begin("project");
//   prefs.putFloat("tempOffset", tempOffset);
//   prefs.end();
// }

// 写入主题参数
void setTheme(){
  prefs.begin("project");
  prefs.putInt("backColor",backColor);
  prefs.end();
}

// 清除所有初始化参数
void clearInfo(){
  prefs.begin("project");
  prefs.clear();
  prefs.end();
}

// 测试用，在读取NVS之前，先写入自己的Wifi信息，免得每次浪费时间再配网
void setInfo4Test(){
  prefs.begin("project");
  prefs.putString("ssid", "xiaomi14");
  prefs.putString("pass", "1234567890");
  prefs.putString("city", "郑州");
  prefs.putString("adm", "");
  prefs.putString("location", "");
  prefs.putInt("bright", 150);
  prefs.putInt("alarmvoice", ALARM_VOICE_VALUE_DEFAULT);
  prefs.putInt("backColor",BACK_BLACK);
  prefs.end();
}
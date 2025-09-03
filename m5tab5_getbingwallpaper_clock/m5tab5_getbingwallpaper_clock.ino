/*
M5TAB5 必应每日画报

制作 GFX 字体
https://rop.nl/truetype2gfx/
*/

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include "FreeSansBold80pt7b.h"
#include "rx8130.h"
#include "ntpsynctime.h"
#include "net.h"
#include "PreferencesUtil.h"


#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

static int wifiloadNum = 0;
String wifissid     = "yourssid";
String wifipassword = "yourpassword";
static bool wifistatus = false;
RX8130_Class RX8130;


// M5 Touch Button
m5::touch_detail_t touchDetail;
static int32_t button_w=300;
static int32_t button_h=100;

LGFX_Button WifiConfigButton;
static bool wifi_config_start_flag =false;


static void WifiConfigButton_init()
{
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    WifiConfigButton.initButton(&M5.Lcd, 1280 / 2, 500, button_w, button_h, TFT_BLUE, TFT_YELLOW, TFT_BLACK, "WifiConfig",1, 1);
    WifiConfigButton.drawButton();
}

TaskHandle_t TouchCheckTask;

static void TouchCheck_task(void *pvParameters) 
{
  int nums=0;
  while(1)
  {
    vTaskDelay(20);
    M5.update();
    touchDetail = M5.Touch.getDetail();  
    if (touchDetail.isPressed()) 
    {
      if(WifiConfigButton.contains(touchDetail.x, touchDetail.y))
      {
          // M5.Display.drawString("Button  Pressed", w / 2, 0, &fonts::FreeMonoBold24pt7b);
          if(wifi_config_start_flag==false)
          {
            Serial.println("WifiConfigButton pressed");
            M5.Display.drawCenterString("Wifi config starting ...", 1280 / 2, 720/2, &fonts::FreeMonoBold24pt7b);
            disconnectWiFi();
            vTaskDelay(20);
            wifiConfigBySoftAP();
            wifi_config_start_flag=true;
            Serial.println("Wifi SoftAP Set Finished,start listening...");
            break;
          }

      }
    }
    // else {
    //   M5.Display.drawString("Button Released", w / 2, 0, &fonts::FreeMonoBold24pt7b);
    // }
  }
    vTaskDelete(TouchCheckTask);
}


void createTouchCheckTask()
{
  xTaskCreate(TouchCheck_task, "TouchCheck_task", 4*8192, NULL, 10, &TouchCheckTask);
}

// LGFX_Sprite sprite(&M5.Display);

//wifi event handler
static void WiFiEvent(WiFiEvent_t event){
    switch(event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          //When connected set 
          Serial.print("WiFi connected! IP address: ");
          Serial.println(WiFi.localIP());  
          break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
          Serial.println("WiFi lost connection");
          break;
      default: break;
    }
}

void rx8130_init()
{
    if (!RX8130.begin()) {
        Serial.println("rx8130 init failed!");
    } else {
        RX8130.disableIrq();
        RX8130.clearIrqFlags();
    }
}

void setRtcTime(struct tm* time)
{
    RX8130.setTime(time);
}

void getRtcTime(struct tm* time)
{
    RX8130.getTime(time);
}


void getBingWallpaper() {
  HTTPClient http;
  String jsonDataUrl = "http://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=1&mkt=zh-CN";
  
  http.begin(jsonDataUrl);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // 解析JSON
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);
    
    String imageUrl = doc["images"][0]["url"].as<String>();
    // 替换分辨率
    imageUrl.replace("1920x1080", "1280x720");
    String fullUrl = "http://bing.com" + imageUrl;
    
    Serial.println(fullUrl);
    // 下载图片
    downloadImage(fullUrl);
  }
  
  http.end();
}

void downloadImage(String imageUrl) {
  HTTPClient http;
  http.begin(imageUrl);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == HTTP_CODE_OK) {
    File file = SPIFFS.open("/bing_wallpaper.jpg", FILE_WRITE);
    if (file) {
      http.writeToStream(&file);
      file.close();
      Serial.println("Image downloaded successfully");
    }
  }
  
  http.end();
}

void listFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  
  while(file) {
    Serial.print("File: ");
    Serial.print(file.name());
    Serial.print(" Size: ");
    Serial.println(file.size());
    file = root.openNextFile();
  }
}



void setup()
{
    M5.begin();
    Serial.begin(115200);

    // setInfo4Test();
    getInfo();
    printInfo();

    wifissid = ssid ;
    wifipassword = pass ;
    Serial.printf("wifi ssid : ");
    Serial.println(wifissid);
    Serial.printf("wifi password : ");
    Serial.println(wifipassword);

    rx8130_init();
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }

    M5.Display.setFont(&fonts::FreeMonoBoldOblique24pt7b);
    M5.Display.setRotation(3);
    M5.Display.setBrightness(255);

    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);

    // If you select the M5Tab5 board in Arduino IDE, you could use the default pins defined.
    // WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD, BOARD_SDIO_ESP_HOSTED_D0,
    //              BOARD_SDIO_ESP_HOSTED_D1, BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
    //              BOARD_SDIO_ESP_HOSTED_RESET);


    
    // STA MODE
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiEvent);
    M5.Display.println("WiFi mode set to STA");
    WiFi.begin(wifissid.c_str(), wifipassword.c_str());
    M5.Display.print("Connecting to ");
    M5.Display.println(wifissid);
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        for(byte n=0;n<10;n++)//每500毫秒检测一次状态 
        {
          delay(50);
          wifiloadNum += 1;
        }
        if(wifiloadNum>=190)
        {
          break;
        }
        M5.Display.print(".");
    }

  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi 连接失败");
    WiFi.removeEvent(WiFiEvent);
    WiFi.disconnect();
    M5.Display.println("");
    M5.Display.print("Connect to ");
    M5.Display.print(wifissid);
    M5.Display.println(" failed");
    wifistatus = false;
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
    M5.Display.println("");
    M5.Display.print("Connected to ");
    M5.Display.println(wifissid);
    M5.Display.print("IP address: ");
    M5.Display.println(WiFi.localIP());
    wifistatus = true;
  }



    createWiFiSyncTimeTask();
    if(wifistatus)
    {
      getBingWallpaper();
      listFiles();
      // void drawJpgFile(T &fs, const char *path, int32_t x = 0, int32_t y = 0, int32_t maxWidth = 0, int32_t maxHeight = 0, int32_t offX = 0, int32_t offY = 0, float scale_x = 1.0f, float scale_y = 0.0f, datum_t datum = datum_t::top_left)
      
      M5.Display.drawJpgFile(SPIFFS,"/bing_wallpaper.jpg", 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
    }
    else
    {
      M5.Display.fillScreen(BLACK);
      WifiConfigButton_init();
      // createTouchCheckTask();
    }
    


    M5.Display.setFont(&FreeSansBold80pt7b);

  // sprite.setColorDepth(16);
  // sprite.setFont(&FreeSansBold80pt7b);
  // sprite.setTextDatum(middle_center);
}


void show_clock_time(struct tm *time,bool force_refresh)
{
  char timeStrBuffer[32];
  String timeStr="";
  if(force_refresh)
  {
      if(wifistatus)
      {
        M5.Display.drawJpgFile(SPIFFS,"/bing_wallpaper.jpg", 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
      }
      else
      {
        M5.Display.fillScreen(BLACK);
        WifiConfigButton_init();
      }
  }
  getRtcTime(time);
  sprintf(timeStrBuffer, "%02d:%02d",time->tm_hour, time->tm_min);
  timeStr = String(timeStrBuffer);
  M5.Display.setFont(&FreeSansBold80pt7b);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawCenterString(timeStr, 1280/2, 160/2);
  sprintf(timeStrBuffer, "%04d    %02d/%02d",time->tm_year + 1900, time->tm_mon + 1, time->tm_mday);
  timeStr = String(timeStrBuffer);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawCenterString(timeStr, 1280/2, 230);
}



void loop()
{
    static struct tm time;
    
    char oldminutes=0;
    getRtcTime(&time);
    oldminutes = time.tm_min;
    show_clock_time(&time,false);

    while(1)
    {
      if(wifi_config_start_flag)
      {
         doClient();
         delay(20);
      }
      else
      {
        M5.update();
        touchDetail = M5.Touch.getDetail();  
        if (touchDetail.isPressed()) 
        {
          if(WifiConfigButton.contains(touchDetail.x, touchDetail.y))
          {
              // M5.Display.drawString("Button  Pressed", w / 2, 0, &fonts::FreeMonoBold24pt7b);
              if(wifi_config_start_flag==false)
              {
                Serial.println("WifiConfigButton pressed");
                M5.Display.drawCenterString("Wifi config starting ...", 1280 / 2, 720/2, &fonts::FreeMonoBold24pt7b);
                disconnectWiFi();
                vTaskDelay(20);
                wifiConfigBySoftAP();
                wifi_config_start_flag=true;
                Serial.println("Wifi SoftAP Set Finished,start listening...");
              }

          }
        }

        getRtcTime(&time);
        if(oldminutes!=time.tm_min)
        {
          Serial.printf("Time: %d/%d/%d %d:%d:%d\n", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min,
              time.tm_sec);
          show_clock_time(&time,true);
        }
        delay(200);
        oldminutes= time.tm_min;
      }
    }

} 
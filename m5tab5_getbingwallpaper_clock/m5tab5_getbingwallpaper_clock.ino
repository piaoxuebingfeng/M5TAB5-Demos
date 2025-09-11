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
#include "pi4ioe5v6408.h"
#include "ina226.h"


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
static bool wifistatus_startup = false;
RX8130_Class RX8130;

PI4IOE5V6408_Class _io_expander_b(0x44);

INA226_Class INA226(0x41);

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

static void WifiConfigButton_check()
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
          if(WiFi.status() == WL_CONNECTED)
          {
            disconnectWiFi();
          }
          vTaskDelay(20);
          wifiConfigBySoftAP();
          wifi_config_start_flag=true;
          Serial.println("Wifi SoftAP Set Finished,start listening...");
        }

    }
  }
}

// TaskHandle_t TouchCheckTask;

// static void TouchCheck_task(void *pvParameters) 
// {
//   int nums=0;
//   while(1)
//   {
//     vTaskDelay(20);
//     WifiConfigButton_check();
//   }
//     vTaskDelete(TouchCheckTask);
// }


// void createTouchCheckTask()
// {
//   xTaskCreate(TouchCheck_task, "TouchCheck_task", 4*8192, NULL, 10, &TouchCheckTask);
// }

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



void _io_expander_b_init()
{
  if(!_io_expander_b.begin())
  {
    Serial.println("pi4ioe5v6408 init failed!");
    return;
  }
  else
  {
    Serial.println("pi4ioe5v6408 init success");
    _io_expander_b.resetIrq();

    // P0 : WLAN_PWR_EN : HIGH -> enable
    _io_expander_b.setDirection(0,true);
    _io_expander_b.setPullMode(0, false);
    _io_expander_b.setHighImpedance(0, false);
    _io_expander_b.digitalWrite(0, true);
  
    // // P1 : unused
    // // P2 : unused

    // P3 : USB5V_EN : HIGH -> enable
    _io_expander_b.setDirection(3,true);
    _io_expander_b.setPullMode(3, false);
    _io_expander_b.setHighImpedance(3, false);
    _io_expander_b.digitalWrite(3, true);

    // P4 : PWR_OFF_PULSE : LOW
    _io_expander_b.setDirection(4,true);
    _io_expander_b.setPullMode(4, false);
    _io_expander_b.setHighImpedance(4, false);
    _io_expander_b.digitalWrite(4, false);

    // P5 : nCHG_QC_EN : LOW -> quick charge
    _io_expander_b.setDirection(5,true);
    _io_expander_b.setPullMode(5, false);
    _io_expander_b.setHighImpedance(5, false);
    _io_expander_b.digitalWrite(5, true);
    // P6 : CHG_STAT
    _io_expander_b.setDirection(6,false);
    _io_expander_b.setPullMode(6, true);
    _io_expander_b.setHighImpedance(6, false);

    // P7 : CHG_EN : LOW -> disable
    // chg enable
    _io_expander_b.setDirection(7,true);
    _io_expander_b.setPullMode(7, false);
    _io_expander_b.setHighImpedance(7, false);
    _io_expander_b.digitalWrite(7, true);
    delay(1000);
    Serial.println("pi4ioe5v6408 set CHG_EN true");
  }
}


void ina226_init()
{
    if (!INA226.begin()) {
        Serial.println("ina226 init failed");
    } else {
        // 28.4 Hz
        INA226.configure(INA226_AVERAGES_16, INA226_BUS_CONV_TIME_1100US, INA226_SHUNT_CONV_TIME_1100US,
                         INA226_MODE_SHUNT_BUS_CONT);
        INA226.calibrate(0.05, 8.192);
    }
}

float getPowerVoltage()
{
    return INA226.readBusVoltage();
}

float getShuntCurrent()
{
    return INA226.readShuntCurrent();
}


void M5Display_CenterString(String showstr)
{
  M5.Display.fillScreen(BLACK);
  // M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setColor(WHITE);
  M5.Display.drawCenterString(showstr, 1280 / 2, 720/2, &fonts::FreeSansBold12pt7b);
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

void getBingWallpapers() {
  HTTPClient http;
  // 修改n=7以获取最近7天的壁纸
  String jsonDataUrl = "http://www.bing.com/HPImageArchive.aspx?format=js&idx=0&n=7&mkt=zh-CN";
  
  http.begin(jsonDataUrl);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // 解析JSON
    DynamicJsonDocument doc(8192); // 增加缓冲区大小以适应更多数据
    deserializeJson(doc, payload);
    
    // 获取图片数组
    JsonArray images = doc["images"];
    
    // 遍历最近7天的壁纸
    for (int i = 0; i < images.size() && i < 7; i++) {
      String imageUrl = images[i]["url"].as<String>();
      
      // 替换分辨率
      imageUrl.replace("1920x1080", "1280x720");
      M5Display_CenterString(imageUrl);
      String fullUrl = "http://bing.com" + imageUrl;
      
      Serial.println("Downloading image " + String(i) + ": " + fullUrl);
      
      // 为每张图片创建不同的文件名
      String filename = "/bing_wallpaper_" + String(i) + ".jpg";
      downloadImage(fullUrl, filename);
      delay(50);
    }
  }
  
  http.end();
}

// 修改downloadImage函数以支持自定义文件名
void downloadImage(String imageUrl, String filename) {
  HTTPClient http;
  http.begin(imageUrl);
  
  int httpResponseCode = http.GET();
  if (httpResponseCode == HTTP_CODE_OK) {
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (file) {
      http.writeToStream(&file);
      file.close();
      Serial.println("Image downloaded successfully: " + filename);
    }
  }
  
  http.end();
}

// 保持原有的downloadImage函数以保证兼容性
void downloadImage(String imageUrl) {
  downloadImage(imageUrl, "/bing_wallpaper.jpg");
}

// void downloadImage(String imageUrl) {
//   HTTPClient http;
//   http.begin(imageUrl);
  
//   int httpResponseCode = http.GET();
//   if (httpResponseCode == HTTP_CODE_OK) {
//     File file = SPIFFS.open("/bing_wallpaper.jpg", FILE_WRITE);
//     if (file) {
//       http.writeToStream(&file);
//       file.close();
//       Serial.println("Image downloaded successfully");
//     }
//   }
  
//   http.end();
// }

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

// 定义最大图片数量
#define MAX_JPG_IMAGES 20
// 存储图片路径的数组
String jpgImagePaths[MAX_JPG_IMAGES];
// 图片数量计数器
int jpgImageCount = 0;
int currentJpgImageIndex = 0;

void listJpgImages() {
  // 重置计数器
  jpgImageCount = 0;
  
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  
  while(file && jpgImageCount < MAX_JPG_IMAGES) {
    String fileName = file.name();
    // 检查文件扩展名是否为 .jpg
    if (fileName.endsWith(".jpg") || fileName.endsWith(".JPG")) {
      jpgImagePaths[jpgImageCount] = "/" + fileName;
      jpgImageCount++;
      Serial.println("Found image: " + fileName);
    }
    file = root.openNextFile();
  }
  
  root.close();
  Serial.println("Total JPG images found: " + String(jpgImageCount));

  for (int i = 0; i < jpgImageCount; i++) 
  {
    Serial.println("Image " + String(i) + ": " + jpgImagePaths[i]);
    // 可以在这里使用图片路径，例如显示图片:
    // M5.Display.drawJpgFile(SPIFFS, jpgImagePaths[i].c_str(), 0, 0);
  }
}

// /atoms3rcamera.jpg
// 清除 SPIFFS


// // 方法1: 删除所有JPG文件
// void deleteAllJpgFiles() {
//   File root = SPIFFS.open("/");
//   File file = root.openNextFile();
  
//   while(file) {
//     String fileName = file.name();
//     if (fileName.endsWith(".jpg") || fileName.endsWith(".JPG")) {
//       file.close();
//       SPIFFS.remove(fileName);
//       Serial.println("Deleted: " + fileName);
//     } else {
//       file.close();
//     }
//     file = root.openNextFile();
//   }
//   root.close();
// }


// // 方法2: 格式化整个SPIFFS文件系统（会删除所有文件）
// void formatSPIFFS() {
//   Serial.println("Formatting SPIFFS...");
//   SPIFFS.format();
//   Serial.println("SPIFFS formatted");
// }

// 方法3: 删除指定的壁纸文件
void deleteBingWallpapers() {
  // 删除默认壁纸
  if (SPIFFS.exists("/atoms3rcamera.jpg")) {
    SPIFFS.remove("/atoms3rcamera.jpg");
    Serial.println("Deleted: /atoms3rcamera.jpg");
  }
  
  // // 删除编号壁纸
  // for (int i = 0; i < 10; i++) {
  //   String filename = "/bing_wallpaper_" + String(i) + ".jpg";
  //   if (SPIFFS.exists(filename)) {
  //     SPIFFS.remove(filename);
  //     Serial.println("Deleted: " + filename);
  //   }
  // }
}

// // 方法4: 删除所有文件（逐个删除）
// void deleteAllFiles() {
//   File root = SPIFFS.open("/");
//   File file = root.openNextFile();
  
//   while(file) {
//     String fileName = file.name();
//     file.close(); // 必须先关闭文件
//     SPIFFS.remove(fileName);
//     Serial.println("Deleted: " + fileName);
//     file = root.openNextFile();
//   }
//   root.close();
// }

void show_clock_time(struct tm *time,bool force_refresh)
{
  char timeStrBuffer[32];
  String timeStr="";
  if(force_refresh)
  {
      if(wifistatus_startup)
      {
        // M5.Display.drawJpgFile(SPIFFS,"/bing_wallpaper.jpg", 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
        Serial.println(jpgImagePaths[currentJpgImageIndex]);
        M5.Display.drawJpgFile(SPIFFS,jpgImagePaths[currentJpgImageIndex++].c_str(), 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
        if(currentJpgImageIndex>jpgImageCount-1)
        {
          currentJpgImageIndex = 0;
        }
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

  sprintf(timeStrBuffer, "V: %.2f  A:%.2f",getPowerVoltage(), getShuntCurrent());
  timeStr = String(timeStrBuffer);
  // M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(RED);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawCenterString(timeStr, 1280/2, 300,&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(WHITE);

}


TaskHandle_t WallPaperClockShowTaskHandle;

static void WallPaperClockShow_task(void *pvParameters) 
{
  static struct tm time;
  
  char oldminutes=0;
  getRtcTime(&time);
  oldminutes = time.tm_min;
  show_clock_time(&time,false);
  while(1)
  {
    getRtcTime(&time);
    if(oldminutes!=time.tm_min)
    {
      Serial.printf("Time: %d/%d/%d %d:%d:%d\n", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min,
          time.tm_sec);
      show_clock_time(&time,true);
      // Serial.printf("getBatteryVoltage : %d\n",M5.Power.getBatteryVoltage());
      // Serial.printf("getBatteryCurrent : %d\n",M5.Power.getBatteryCurrent());

      Serial.printf("getPowerVoltage : %.2f\n",getPowerVoltage());
      Serial.printf("getShuntCurrent : %.2f\n",getShuntCurrent());
      
    }
    delay(1000);
    oldminutes= time.tm_min;
  }
}


void WallPaperClockShowTask()
{
  xTaskCreate(WallPaperClockShow_task, "WallPaperClockShow_task", 16*1024, NULL, 10, &WallPaperClockShowTaskHandle);
}



void WifiStatusManager_task(void *pvParameters) 
{
  while(1)
  {
    if(wifistatus_startup)
    {
      if((WiFi.status() == WL_CONNECTED) && (is_ntpsync_finished()))
      {
        disconnectWiFi(); 
        Serial.println("ntp sync success, wallpaper get sunccess,disconnect wifi");
      }
    }
    delay(1000);
  }
}

void WifiStatusManagerTask()
{
  xTaskCreate(WifiStatusManager_task, "WifiStatusManager_task", 2*8192, NULL, 10, NULL);
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

    _io_expander_b_init();
    ina226_init();
    rx8130_init();
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }


    if(M5.Power.isCharging())
    {
      Serial.println("tab5 power is charging now ...");
    }
    else
    {
      Serial.println("tab5 power not charge");
    }
    // Serial.printf("getBatteryVoltage : %d\n",M5.Power.getBatteryVoltage());
    // Serial.printf("getBatteryCurrent : %d\n",M5.Power.getBatteryCurrent());
    Serial.printf("getPowerVoltage : %.2f\n",getPowerVoltage());
    Serial.printf("getShuntCurrent : %.2f\n",getShuntCurrent());


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
    wifistatus_startup = false;
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
    M5.Display.println("");
    M5.Display.print("Connected to ");
    M5.Display.println(wifissid);
    M5.Display.print("IP address: ");
    M5.Display.println(WiFi.localIP());
    wifistatus_startup = true;
  }

  delay(1500);
  if(wifistatus_startup)
  {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(0,0);
    M5.Display.print("IP address: ");
    M5.Display.println(WiFi.localIP());
    M5.Display.println("show bing wallpaper...");
  }
  delay(1500);


  if(wifistatus_startup)
  {
    M5.Display.setBrightness(5);
    // getBingWallpaper();
    // listFiles();

    currentJpgImageIndex=0;
    getBingWallpapers();
    // deleteBingWallpapers();
    listJpgImages();

    // void drawJpgFile(T &fs, const char *path, int32_t x = 0, int32_t y = 0, int32_t maxWidth = 0, int32_t maxHeight = 0, int32_t offX = 0, int32_t offY = 0, float scale_x = 1.0f, float scale_y = 0.0f, datum_t datum = datum_t::top_left)
    M5.Display.fillScreen(BLACK);
    Serial.println(jpgImagePaths[currentJpgImageIndex]);
    M5.Display.drawJpgFile(SPIFFS,jpgImagePaths[currentJpgImageIndex++].c_str(), 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
    if(currentJpgImageIndex>jpgImageCount-1)
    {
      currentJpgImageIndex = 0;
    }
    M5.Display.setBrightness(255);

  }
  else
  {
    M5.Display.fillScreen(BLACK);
    WifiConfigButton_init();
    // createTouchCheckTask();
  }
  createWiFiSyncTimeTask();



    M5.Display.setFont(&FreeSansBold80pt7b);
    WallPaperClockShowTask();
    WifiStatusManagerTask();
  // sprite.setColorDepth(16);
  // sprite.setFont(&FreeSansBold80pt7b);
  // sprite.setTextDatum(middle_center);
}

void loop()
{
    while(1)
    {         
      delay(50);
      if(wifi_config_start_flag)
      {
         doClient();
      }
      else
      {
        if(!wifistatus_startup)
        {
          WifiConfigButton_check();
        }
      }
    }

} 
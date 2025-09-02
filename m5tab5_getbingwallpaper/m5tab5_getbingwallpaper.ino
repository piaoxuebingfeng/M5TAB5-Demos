#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <M5GFX.h>
#include <M5Unified.h>


#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

const char *ssid     = "yourssid";
const char *password = "yourpasswd";

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

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS mount failed");
        return;
    }

    M5.Display.setFont(&fonts::FreeMonoBoldOblique24pt7b);
    M5.Display.setRotation(3);
    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);

    // If you select the M5Tab5 board in Arduino IDE, you could use the default pins defined.
    // WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD, BOARD_SDIO_ESP_HOSTED_D0,
    //              BOARD_SDIO_ESP_HOSTED_D1, BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
    //              BOARD_SDIO_ESP_HOSTED_RESET);

    // STA MODE
    WiFi.mode(WIFI_STA);
    M5.Display.println("WiFi mode set to STA");
    WiFi.begin(ssid, password);
    M5.Display.print("Connecting to ");
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        M5.Display.print(".");
    }
    M5.Display.println("");
    M5.Display.print("Connected to ");
    M5.Display.println(ssid);
    M5.Display.print("IP address: ");
    M5.Display.println(WiFi.localIP());

    getBingWallpaper();
    
    listFiles();

    M5.Display.setBrightness(255);
    // void drawJpgFile(T &fs, const char *path, int32_t x = 0, int32_t y = 0, int32_t maxWidth = 0, int32_t maxHeight = 0, int32_t offX = 0, int32_t offY = 0, float scale_x = 1.0f, float scale_y = 0.0f, datum_t datum = datum_t::top_left)
    
    M5.Display.drawJpgFile(SPIFFS,"/bing_wallpaper.jpg", 0, 0,1280,720,0,0,1.0,1.0,datum_t::top_left);
}

void loop()
{
} 
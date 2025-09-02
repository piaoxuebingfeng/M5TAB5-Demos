#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <TimeLib.h>
#include "rx8130.h"


#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

const char *ssid     = "yourssid";
const char *password = "yourpassword";


RX8130_Class RX8130;


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


// wifi 时间同步任务
TaskHandle_t wifiSyncTimeTask;

// 网络部分
#define ONLINE_MODE   0
#define OFFLINE_MODE  1

int wifimode = OFFLINE_MODE; // 运行模式
//NTP服务器
static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8;     //东八区
WiFiUDP Udp;
unsigned int localPort = 8000;
const int NTP_PACKET_SIZE = 48; // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime();

// 向NTP服务器发送请求
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  //Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  //Serial.print(ntpServerName);
  //Serial.print(": ");
  //Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // 无法获取时间时返回0
}

//wifi event handler
void WiFiEvent(WiFiEvent_t event){
    switch(event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
          //When connected set 
          Serial.print("WiFi connected! IP address: ");
          Serial.println(WiFi.localIP());  
          wifimode = ONLINE_MODE;
          break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
          Serial.println("WiFi lost connection");
          wifimode = OFFLINE_MODE;
          break;
      default: break;
    }
}

bool wifi_sync_time()
{
    int loadNum = 6;

    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiEvent);
    Serial.printf("\n\n********************连接 WIFI: %s ********************\n",ssid);
    WiFi.begin(ssid, password);
    WiFi.setAutoReconnect(true);

  Serial.print("正在连接WIFI ");
 

  while (WiFi.status() != WL_CONNECTED) 
  {
    for(byte n=0;n<10;n++)//每500毫秒检测一次状态 
    {
      //loading(50);
      delay(50);
      loadNum += 1;
    }
    if(loadNum>=190)
    {
      break;
    }
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi 连接失败");
    WiFi.removeEvent(WiFiEvent);
    WiFi.disconnect();
    wifimode = OFFLINE_MODE;
    delay(1000);
    return false;
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
    // while(loadNum < 194)
    // { //让动画走完
    //   loading(1);
    //   if(loadNum>190)
    //   {
    //     break;
    //   }
    // }
    Serial.print("本地IP： ");
    Serial.println(WiFi.localIP());
    //Serial.println("启动UDP");
    Udp.begin(localPort);
    //Serial.print("端口号: ");
    //Serial.println(Udp.localPort());
    //Serial.println("等待同步...");
    setSyncProvider(getNtpTime);
    // setSyncInterval(300);

    delay(5000);
    WiFi.removeEvent(WiFiEvent);
    WiFi.disconnect();
    wifimode = OFFLINE_MODE;
    delay(1000);
    return true;
  }
}


void wifi_sync_time_task(void *pvParameters) 
{
  bool ret;
  while(1)
  {
    // 通过 WIFI 同步时间
    ret = wifi_sync_time();
    if(ret)// 通过 WIFI 同步时间成功
    {
      Serial.println("wifi sync time success");
      Serial.println("start sync time to RX8130");
      /* Set RTC time */
      struct tm time;
      time.tm_year = year();
      time.tm_mon  = month()-1;
      time.tm_mday = day();
      time.tm_hour = hour();
      time.tm_min  = minute();
      time.tm_sec  = second();
      setRtcTime(&time);

    }
    if(!ret)
    {
      Serial.println("wifi sync time fail");
      Serial.println("start sync time from RTC RX8130\r\n");

      static struct tm time;
      getRtcTime(&time);
      Serial.printf("Time: %d/%d/%d %d:%d:%d\n", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min,
           time.tm_sec);

      setTime(time.tm_hour, time.tm_min, time.tm_sec, time.tm_mday, time.tm_mon + 1, time.tm_year + 1900);
    }
    break;
  }

  vTaskDelete(wifiSyncTimeTask);
}


void createWiFiSyncTimeTask()
{
  xTaskCreate(wifi_sync_time_task, "wifi_sync_time_task", 2*8192, NULL, 1, &wifiSyncTimeTask);
}

void setup()
{
    M5.begin();
    Serial.begin(115200);

    rx8130_init();

    M5.Display.setFont(&fonts::FreeMonoBoldOblique24pt7b);

    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);

    // If you select the M5Tab5 board in Arduino IDE, you could use the default pins defined.
    // WiFi.setPins(BOARD_SDIO_ESP_HOSTED_CLK, BOARD_SDIO_ESP_HOSTED_CMD, BOARD_SDIO_ESP_HOSTED_D0,
    //              BOARD_SDIO_ESP_HOSTED_D1, BOARD_SDIO_ESP_HOSTED_D2, BOARD_SDIO_ESP_HOSTED_D3,
    //              BOARD_SDIO_ESP_HOSTED_RESET);

    // // STA MODE
    // WiFi.mode(WIFI_STA);
    // M5.Display.println("WiFi mode set to STA");
    // WiFi.begin(ssid, password);
    // M5.Display.print("Connecting to ");
    // // Wait for connection
    // while (WiFi.status() != WL_CONNECTED) {
    //     delay(500);
    //     M5.Display.print(".");
    // }
    // M5.Display.println("");
    // M5.Display.print("Connected to ");
    // M5.Display.println(ssid);
    // M5.Display.print("IP address: ");
    // M5.Display.println(WiFi.localIP());

    createWiFiSyncTimeTask();

    // AP MODE
    // WiFi.mode(WIFI_MODE_AP);
    // Serial.println("WiFi mode set to AP");
    // WiFi.softAP(ssid, password);
    // Serial.println("AP started");
    // Serial.print("IP address: ");
    // Serial.println(WiFi.softAPIP());

}

void loop()
{
    static struct tm time;
    char timeStrBuffer[32];
    
    getRtcTime(&time);
    Serial.printf("Time: %d/%d/%d %d:%d:%d\n", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min,
        time.tm_sec);

    sprintf(timeStrBuffer, "%04d/%02d/%02d %02d:%02d:%02d", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    String timeStr = String(timeStrBuffer);
    M5.Display.drawCenterString(timeStr, 360, 100);
    delay(1000);
} 
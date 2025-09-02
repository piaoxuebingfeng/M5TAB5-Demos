#include "ntpsynctime.h"

// wifi 时间同步任务
TaskHandle_t wifiSyncTimeTask;

extern void getRtcTime(struct tm* time);
extern void setRtcTime(struct tm* time);

// 网络部分
#define ONLINE_MODE   0
#define OFFLINE_MODE  1

static bool wifimode = OFFLINE_MODE; // 运行模式
//NTP服务器
static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8;     //东八区
WiFiUDP Udp;
unsigned int localPort = 8000;
const int NTP_PACKET_SIZE = 48; // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

static time_t getNtpTime();

// 向NTP服务器发送请求
static void sendNTPpacket(IPAddress &address)
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
static time_t getNtpTime()
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

// //wifi event handler
// static void WiFiEvent(WiFiEvent_t event){
//     switch(event) {
//       case ARDUINO_EVENT_WIFI_STA_GOT_IP:
//           //When connected set 
//           Serial.print("WiFi connected! IP address: ");
//           Serial.println(WiFi.localIP());  
//           wifimode = ONLINE_MODE;
//           break;
//       case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
//           Serial.println("WiFi lost connection");
//           wifimode = OFFLINE_MODE;
//           break;
//       default: break;
//     }
// }

static bool wifi_sync_time()
{
  //   int loadNum = 6;

  //   WiFi.disconnect();
  //   WiFi.mode(WIFI_STA);
  //   WiFi.onEvent(WiFiEvent);
  //   Serial.printf("\n\n********************连接 WIFI: %s ********************\n",ssid);
  //   WiFi.begin(ssid, password);
  //   WiFi.setAutoReconnect(true);

  // Serial.print("正在连接WIFI ");
 

  // while (WiFi.status() != WL_CONNECTED) 
  // {
  //   for(byte n=0;n<10;n++)//每500毫秒检测一次状态 
  //   {
  //     //loading(50);
  //     delay(50);
  //     loadNum += 1;
  //   }
  //   if(loadNum>=190)
  //   {
  //     break;
  //   }
  // }
  if(WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi 连接失败");
    // WiFi.removeEvent(WiFiEvent);
    WiFi.disconnect();
    wifimode = OFFLINE_MODE;
    delay(1000);
    return false;
  }
  else if(WiFi.status() == WL_CONNECTED)
  {
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
    // WiFi.removeEvent(WiFiEvent);
    // WiFi.disconnect();
    // wifimode = OFFLINE_MODE;
    // delay(1000);
    return true;
  }
}


static void wifi_sync_time_task(void *pvParameters) 
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
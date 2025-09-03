#include "net.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
// #include "ArduinoZlib.h"
#include "PreferencesUtil.h"


// extern void draw2LineText(String text1, String text2);

// Wifi相关
String ssid;  //WIFI名称
String pass;  //WIFI密码
String city;  // 城市
String adm; // 上级城市区划
String location; // 城市ID
String WifiNames; // 根据搜索到的wifi生成的option字符串

// SoftAP相关
const char *APssid = "M5TAB5CLOCK";
IPAddress staticIP(192, 168, 5, 1);
IPAddress gateway(192, 168, 5, 254);
IPAddress subnet(255, 255, 255, 0);
WebServer server(80);


// 查询天气超时时间(ms)
int queryTimeout = 5000;
// 天气接口相关
static HTTPClient httpClient;
String data = "";
uint8_t *outbuf;
Weather weather; // 记录查询到的天气数据
bool queryWeatherSuccess = false;
bool queryAirSuccess = false;
// 对时相关
// WiFiUDP ntpUDP;
// NTPClient timeClient(ntpUDP, NTP, 8 * 3600, TIME_CHECK_INTERVAL * 1000);

// 开启AP模式
void startAP(){
  logInfoln("开启AP模式...");

  // WiFi.enableAP(true); // 使能AP模式
  WiFi.mode(WIFI_MODE_AP);
  //传入参数静态IP地址,网关,掩码
  WiFi.softAPConfig(staticIP, gateway, subnet);
  if (!WiFi.softAP(APssid)) {
    logInfoln("AP模式启动失败");
  }  
  logInfoln("AP模式启动成功");
  // logInfoln("IP address: ");
  // logInfoln(WiFi.softAPIP());

}


// 扫描WiFi,并将扫描到的Wifi组成option选项字符串
void scanWiFi(){
  logInfoln("开始扫描WiFi");
  int n = WiFi.scanNetworks();
  if (n){
    logInfo("扫描到");
    logInfo(String(n));
    logInfoln("个WIFI");
    WifiNames = "";
    for (size_t i = 0; i < n; i++){
      int32_t rssi = WiFi.RSSI(i);
      String signalStrength;
      if(rssi >= -35){
        signalStrength = " (信号极强)";
      }else if(rssi >= -50){
        signalStrength = " (信号强)";
      }else if(rssi >= -70){
        signalStrength = " (信号中)";
      }else{
        signalStrength = " (信号弱)";
      }
      WifiNames += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + signalStrength + "</option>";
    }
  }else{
    logInfoln("没扫描到WIFI");
  }
}


// 处理404情况的函数'handleNotFound'
void handleNotFound(){
  handleRoot();
}
// 处理网站根目录的访问请求
void handleRoot(){
  server.send(200,"text/html", ROOT_HTML_PAGE1 + WifiNames + ROOT_HTML_PAGE2);
}
// 提交数据后的提示页面
void handleConfigWifi(){
  //判断是否有WiFi名称
  if (server.hasArg("ssid")){
    logInfo("获得WiFi名称:");
    ssid = server.arg("ssid");
    logInfoln(ssid);
  }else{
    logInfoln("错误, 没有发现WiFi名称");
    server.send(200, "text/html", "<meta charset='UTF-8'>错误, 没有发现WiFi名称");
    return;
  }
  //判断是否有WiFi密码
  if (server.hasArg("pass")){
    logInfo("获得WiFi密码:");
    pass = server.arg("pass");
    logInfoln(pass);
  }else{
    logInfoln("错误, 没有发现WiFi密码");
    server.send(200, "text/html", "<meta charset='UTF-8'>错误, 没有发现WiFi密码");
    return;
  }
  //判断是否有city名称
  if (server.hasArg("city")){
    if(!server.arg("city").equals(city) ){
      location = "";
    }
    logInfo("获得城市:");
    city = server.arg("city");
    logInfoln(city);
  }else{
    logInfoln("错误, 没有发现城市名称");
    server.send(200, "text/html", "<meta charset='UTF-8'>错误, 没有发现城市名称");
    return;
  }
  logInfo("获得上级区划:");
  adm = server.arg("adm");
  logInfoln(adm);
  // 将信息存入nvs中
  setInfo();
  // 获得了所需要的一切信息，给客户端回复
  server.send(200, "text/html", "<meta charset='UTF-8'><style type='text/css'>body {font-size: 2rem;}</style><br/><br/>WiFi: " + ssid + "<br/>密码: " + pass + "<br/>城市: " + city + "<br/>上级区划: " + adm + "<br/>已取得相关信息,即将尝试连接,请手动关闭此页面。");
  
  // // 绘制重启提示文字
  // draw2LineText("已取得配置信息", "即将重启");
  delay(5000);
  ESP.restart();
}
// 启动服务器
void startServer(){
  // 当浏览器请求服务器根目录(网站首页)时调用自定义函数handleRoot处理，设置主页回调函数，必须添加第二个参数HTTP_GET，否则无法强制门户
  delay(20);
  server.on("/", HTTP_GET, handleRoot);
  // 当浏览器请求服务器/configwifi(表单字段)目录时调用自定义函数handleConfigWifi处理
  server.on("/configwifi", HTTP_POST, handleConfigWifi);
  // 当浏览器请求的网络资源无法在服务器找到时调用自定义函数handleNotFound处理   
  server.onNotFound(handleNotFound);
  server.begin();
  logInfoln("服务器启动成功！");
}
// 处理服务器请求
void doClient(){
  server.handleClient();
}
// 连接WiFi
void connectWiFi(int timeOut_s){
  int connectTime = 0; //用于连接计时，如果长时间连接不成功，复位设备
  logInfoln("正在连接网络 ");
  logInfo("ssid: ");logInfoln(ssid);
  logInfo("pass: ");logInfoln(pass);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED){
    logInfo(".");
    delay(500);
    connectTime++;
    if (connectTime > 2 * timeOut_s){
      logInfoln("网络连接失败");
      connected = false;
      break;
    }
  }
  if(connected){
    logInfoln("网络连接成功");
  }
}

// 检查WiFi状态
bool wifiConnected(){
  if(WiFi.status() == WL_CONNECTED){
    return true;
  }else{
    return false;
  }
}
// url中文编码
String urlEncode(const String& text){
  String encodedText = "";
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encodedText += c;
    } else if (c == ' ') {
      encodedText += '+';
    } else {
      encodedText += '%';
      char hex[4];
      sprintf(hex, "%02X", (uint8_t)c);
      encodedText += hex;
    }
  }
  return encodedText;
}


// 断开Wifi
void disconnectWiFi(){
  WiFi.disconnect();
}



// 开启SoftAP进行配网
void wifiConfigBySoftAP(){
  // 开启AP模式
  startAP();
  // 扫描WiFi,并将扫描到的WiFi组成option选项字符串
  scanWiFi();
  // 启动服务器
  startServer();
}


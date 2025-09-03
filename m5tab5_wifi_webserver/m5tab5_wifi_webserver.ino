#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>

#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

const char *ssid     = "M5TAB5";
const char *password = "1234567890";


WebServer server(80);


const int led = 49;

void handleRoot() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "hello from esp32!");
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

void setup()
{
    pinMode(led, OUTPUT);
    digitalWrite(led, 0);
    M5.begin();
    Serial.begin(115200);

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

    // AP MODE
    WiFi.mode(WIFI_MODE_AP);
    Serial.println("WiFi mode set to AP");
    WiFi.softAP(ssid, password);
    Serial.println("AP started");
    Serial.print("IP address: ");
    Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

}

void loop()
{
  server.handleClient();
  delay(2);  //allow the cpu to switch to other tasks
} 
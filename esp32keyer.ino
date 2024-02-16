/*
   Simple Touch CW keyer based on a ESP32 microcontroller

   Basic idea: Hey, i have this awesome ESP32, which is able to read touch sensors and it has WiFi onboard. Lets create a CW keyer!

   Following steps are done
   - CW paddle designed -> openSCAD and STL files
   - Create keyer and webserver logic including configuration
   - Create text to morse code logic
   -- Configuration parameters: wpm, predefined texts, own and heared callsign
   -- persist configuration
   -- touchWithInteruput -> Not good working.. But polling i still good enough

*/
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebSrv.h>
#include <Preferences.h>
#include <StreamString.h>
#include "MorseNode.h"
#include "wifi_credentials.h"

#define LED_BUILTIN 2
#define TRX_GPIO 26
#define BUZZER_GPIO 27
#define DIT_GPIO 32
#define DAH_GPIO 33
#define DEBUG

// keyer machine states
enum KeyerState {
  STATE_IDLE,
  STATE_TONE_CREATION,
  STATE_WAIT_TONE_END,
  STATE_WAIT_BETWEEN_SYMBOLS,
  STATE_DELETE_PROCESSED_PADDLE_EVENT,
  STATE_SYMBOL_TO_SEND
} state;

enum PaddleState {
  PADDLE_STATE_NONE,
  PADDLE_STATE_DIT,
  PADDLE_STATE_DAH,
  PADDLE_STATE_DIT_DAH,
  PADDLE_STATE_DAH_DIT
};

struct Config {
  char wpm = 13;
  int thresholdDistance = 36;     // sensitivy of touch sensor. Lower values for higher sensitivity.
  String hostname = "keyer";      // Wifi name of keyer
  String ssid     = SSID;
  String ssidPwd  = SSID_PWD;

  String cwTextCq = "cq de mycall mycall k";
  String cwText1  = "gd name name name = qth myqth myqth = rst 599 5nn =";
  String cwText2  = "all ok = ant dipole es 5 w = wx cloudy es temp 5C =";
  String cwText3  = "thx fer qso 73 and pse QSL =";
  String cwText4  = "call de mycall";
} config;

Preferences preferences;               // Persists configuration
AsyncWebServer webServer(80);
StreamString symbolsToSend;            // Contains te text to create morse code fot
MorseNode* morseTree = newMorseTree(); // text to morse code engine

// times in ms
unsigned long durationDit;
unsigned long durationDah;
unsigned long durationBetweenSymbols;
unsigned long durationBetweenLetters;
unsigned long durationBetweenWords;
unsigned long nextCwEvent;
unsigned long ditEvent;
unsigned long dahEvent;

// values for untouched paddles
int ditTouchReadIdle;
int dahTouchReadIdle;

#ifdef DEBUG
long loopCycles = 0;
#endif

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  Serial.println("setup()");

  // Setup keyer
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(TRX_GPIO, OUTPUT);
  loadConfig();
  ditEvent = 0;
  dahEvent = 0;
  nextCwEvent = 0;
  updateDurations();
  touchSetCycles(0x1000, 0x2000);
  ditTouchReadIdle = touchRead(DIT_GPIO);
  dahTouchReadIdle = touchRead(DAH_GPIO);
  setState(STATE_IDLE);

  // Setup wifi
  WiFi.setHostname(config.hostname.c_str());
  WiFi.begin(config.ssid.c_str(), config.ssidPwd.c_str());
  while (WiFi.status() != WL_CONNECTED && millis() < 30000) {
    delay(1000);
    Serial.print(".");
  }
  Serial.print("IP address: "); Serial.println(WiFi.localIP());
  Serial.print("Netmask: "); Serial.println(WiFi.subnetMask());
  Serial.print("Gateway: "); Serial.println(WiFi.gatewayIP());
  if (WiFi.status() != WL_CONNECTED) {
    IPAddress ip(192, 168, 177, 1);
    IPAddress gateway(192, 168, 177, 1);
    IPAddress subnet(255, 255, 255, 0);
    Serial.print("Setting soft-AP configuration "); Serial.println(WiFi.softAPConfig(ip, gateway, subnet) ? "Ready" : "Failed!");
    Serial.print("Setting soft-AP "); Serial.println(WiFi.softAP(config.hostname) ? "Ready" : "Failed!");
    Serial.print("Soft-AP IP address = "); Serial.println(WiFi.softAPIP());
  }

  // setup webserver
  Serial.println("Setup webserver");
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    processWebRequest(request);
  });
  webServer.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
    processWebRequest(request);
  });
  webServer.begin();
  Serial.println("webserver.begin");
}

void processWebRequest(AsyncWebServerRequest * request) {
  AsyncResponseStream *response = request->beginResponseStream("text/html");
  response->addHeader("Server", "Keyer Web Server");
  response->printf("<!DOCTYPE html><html><head><title>Wifi keyer at %s</title></head><body>", request->url().c_str());
  response->print("<h2>Hello ");
  response->print(request->client()->remoteIP());
  response->printf(" from %s", config.hostname.c_str());
  response->print("</h2>");

  if (request->hasParam("wpm", true)) {
    config.wpm = request->getParam("wpm", true)->value().toInt();
    updateDurations();
  }
  if (request->hasParam("thresholdDistance", true)) {
    config.thresholdDistance = request->getParam("thresholdDistance", true)->value().toInt();
  }
  if (request->hasParam("ssid", true)) {
    config.ssid = request->getParam("ssid", true)->value();
  }
  if (request->hasParam("ssidPwd", true)) {
    config.ssidPwd = request->getParam("ssidPwd", true)->value();
  }
  if (request->hasParam("cwTextCq", true)) {
    config.cwTextCq = request->getParam("cwTextCq", true)->value();
  }
  if (request->hasParam("cwText1", true)) {
    config.cwText1 = request->getParam("cwText1", true)->value();
  }
  if (request->hasParam("cwText2", true)) {
    config.cwText2 = request->getParam("cwText2", true)->value();
  }
  if (request->hasParam("cwText3", true)) {
    config.cwText3 = request->getParam("cwText3", true)->value();
  }
  if (request->hasParam("cwText4", true)) {
    config.cwText4 = request->getParam("cwText4", true)->value();
  }

  if (request->hasParam("action", true)) {
    String action = request->getParam("action", true)->value();
    if (action == "reset") {
      config = Config();
    }
    if (action == "update") {
      if (request->hasParam("store", true) && String("on") == request->getParam("store", true)->value()) {
        storeConfig();
      }
    }
    if (action == "sendcq") {
      textToSymbols(config.cwTextCq);
    }
    if (action == "send1") {
      textToSymbols(config.cwText1);
    }
    if (action == "send2") {
      textToSymbols(config.cwText2);
    }
    if (action == "send3") {
      textToSymbols(config.cwText3);
    }
     if (action == "send4") {
      textToSymbols(config.cwText4);
    }
  }
  response->print("<form action=\"/\" method=\"POST\">");
  response->printf("<p><label for=\"ssid\">WLAN SSID</label><br><input type=\"text\" id=\"ssid\" name=\"ssid\" value=\"%s\"></p>", config.ssid.c_str());
  response->printf("<p><label for=\"ssid_pwd\">WLAN password</label><br><input type=\"password\" id=\"ssid_pwd\" name=\"ssid_pwd\" value=\"%s\"></p>", config.ssidPwd.c_str());
  response->printf("<p><label for=\"thresholdDistance\">Touch sensitivy</label><br><input type=\"number\" id=\"thresholdDistance\" name=\"thresholdDistance\" value=\"%d\"></p>", config.thresholdDistance);
  response->printf("<p><label for=\"wpm\">CW wpm</label><br><input type=\"number\" id=\"wpm\" name=\"wpm\" value=\"%d\"></p>", config.wpm);
  response->printf("<p><label for=\"store\">Store permanently </label><br><input type=\"checkbox\" id=\"store\" name=\"store\"></p>");
  response->print("<button name=\"action\" value=\"reset\">Reset</button>\n");
  response->print("<button name=\"action\" value=\"update\">Update</button>\n");
  response->print("<p>\n");
  response->print("<label for=\"cwTextCq\">CW to SEND</label><br>");
  response->printf("<input type=\"text\" id=\"cwTextCq\" name=\"cwTextCq\" size=\"60\" value=\"%s\"> ", config.cwTextCq.c_str());
  response->print("<button name=\"action\" value=\"sendcq\">Send</button><br>");
  response->printf("<input type=\"text\" id=\"cwText1\" name=\"cwText1\" size=\"60\" value=\"%s\"> ", config.cwText1.c_str());
  response->print("<button name=\"action\" value=\"send1\">Send</button><br>");
  response->printf("<input type=\"text\" id=\"cwText2\" name=\"cwText2\" size=\"60\" value=\"%s\"> ", config.cwText2.c_str());
  response->print("<button name=\"action\" value=\"send2\">Send</button><br>");
  response->printf("<input type=\"text\" id=\"cwText3\" name=\"cwText3\" size=\"60\" value=\"%s\"> ", config.cwText3.c_str());
  response->print("<button name=\"action\" value=\"send3\">Send</button><br>");
  response->printf("<input type=\"text\" id=\"cwText4\" name=\"cwText4\" size=\"60\" value=\"%s\"> ", config.cwText4.c_str());
  response->print("<button name=\"action\" value=\"send4\">Send</button><br>");
  response->print("</p><br>\n");
  response->print("</form></body></html>");
  request->send(response);
}

// Callback function from MorseNode
void writeSymbol(Symbol symbol) {
  char tmp = ' ';
  switch (symbol) {
    case NONE:
      tmp = ' '; break;
    case DIT:
      tmp = '.'; break;
    case DAH:
      tmp = '-'; break;
  }
  symbolsToSend.write(tmp);
}

void textToSymbols(String text) {
  for (int i = 0; i < text.length(); i++) {
    char tmpChar = text.charAt(i);
    MorseNode* node = findNode(morseTree, tmpChar);
    #ifdef DEBUG
    Serial.printf("textToSymbols %c \n", tmpChar);
    #endif
    if (node != NULL) {
        node->createSymbols();
    } else {
      Serial.printf("Unable to find morse node for %c \n", tmpChar);
    }
  }
}

void storeConfig() {
  preferences.begin("k", false);
  preferences.putChar("wpm", config.wpm);
  preferences.putInt("thD", config.thresholdDistance);
  preferences.putString("ssid", config.ssid);
  preferences.putString("ssidP", config.ssidPwd);
  preferences.end();
}
void loadConfig() {
  preferences.begin("k", true);
  config.wpm  = preferences.getChar("wpm", config.wpm);
  config.thresholdDistance = preferences.getInt("thD", config.thresholdDistance);
  config.ssid = preferences.getString("ssid", config.ssid);
  config.ssidPwd = preferences.getString("ssidP", config.ssidPwd);
  preferences.end();
}

void updateDurations() {
  durationDit = 100 * 12 / config.wpm;
  durationDah = 3 * durationDit;
  durationBetweenSymbols = durationDit;
  durationBetweenLetters = durationDit * 3;
  durationBetweenWords = durationDit * 7;
}

void toneOn() {
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(TRX_GPIO, HIGH);
  tone(BUZZER_GPIO, 500);
}
void toneOff() {
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(TRX_GPIO, LOW);
  noTone(BUZZER_GPIO);
}
void setState(int newState) {
  state = (KeyerState) newState;
}

int readPaddleState() {
  if (ditEvent > 0 && dahEvent > 0) {
    return (ditEvent < dahEvent) ? PADDLE_STATE_DIT_DAH : PADDLE_STATE_DAH_DIT;
  }
  if (ditEvent > 0) return PADDLE_STATE_DIT;
  if (dahEvent > 0) return PADDLE_STATE_DAH;
  return PADDLE_STATE_NONE;
}

void pollTouchSensors() {
  pollDahSensor();
  pollDitSensor();
}
void pollDahSensor() {
  if (dahEvent > 0) {
    return;
  }
  int value = touchRead(DAH_GPIO);
  if (value > 0 && value < (ditTouchReadIdle - config.thresholdDistance)) {
    #ifdef DEBUG
    Serial.printf("Touch read = %d\n", value);
    #endif
    dahEvent = millis();
    stopSendingSymbols();
  }
}
void pollDitSensor() {
  if (ditEvent > 0) {
    return;
  }
  int value = touchRead(DIT_GPIO);
  if (value > 0 && value < (dahTouchReadIdle - config.thresholdDistance)) {
    #ifdef DEBUG
    Serial.printf("Touch read = %d\n", value);
    #endif
    ditEvent = millis();
    stopSendingSymbols();
  }
}

void stopSendingSymbols() {
  while (symbolsToSend.available() > 0) symbolsToSend.read();
}

void loop() {
#ifdef DEBUG
  loopCycles++; // only keyer logic ~105
#endif
  // the state machine: (states in HIGH LETTERS, transitions in camel case)

  // IDLE                                                                    <-,
  //  |                                                                        |
  // paddleTouched                                                             |
  //  |                                                                        |
  //  '-> TONE_CREATION                                 <-,                    |
  //          |                                           |                    |
  //         toneOn                                       |                    |
  //          |                                     paddleTouched      noPaddleTouched
  //          '-> WAIT_TONE_END                           |                    |
  //                |                                     |                    |
  //               toneOff                                |                    |
  //                |                                     |                    |
  //                '-> WAIT_BETWEEN_SYMBOLS -> DELETE_PROCESSED_PADDLE_EVENT -'

  if (state == STATE_IDLE) {
    pollTouchSensors();
    if (readPaddleState() != PADDLE_STATE_NONE) {
      setState(STATE_TONE_CREATION);
    } else if (symbolsToSend.available() > 0) {
      setState(STATE_SYMBOL_TO_SEND);
    }
  }

  if (state == STATE_SYMBOL_TO_SEND) {
    int symbol = symbolsToSend.read();
    if (symbol != -1) {
      int currentMillis = millis();
      if (symbol == '.') ditEvent = currentMillis;
      if (symbol == '-') dahEvent = currentMillis;
      if (readPaddleState() != PADDLE_STATE_NONE)
        setState(STATE_TONE_CREATION);
      else if (symbol == ' ') {
        if (symbolsToSend.peek() == ' ') {
          symbolsToSend.read();
          nextCwEvent = currentMillis + durationBetweenWords;
        } else {
          nextCwEvent = currentMillis + durationBetweenLetters;
        }
        setState(STATE_WAIT_BETWEEN_SYMBOLS);
      }
    } else {
      setState(STATE_IDLE);
    }
  }

  if (state == STATE_TONE_CREATION) {
    if (readPaddleState() == PADDLE_STATE_DIT || readPaddleState() == PADDLE_STATE_DIT_DAH) {
      nextCwEvent = millis() + durationDit;
    } else {
      nextCwEvent = millis() + durationDah;
    }
    toneOn();
    setState(STATE_WAIT_TONE_END);
  }

  if (state == STATE_WAIT_TONE_END) {
    if (millis() < nextCwEvent) {
      pollTouchSensors();
    } else {
      toneOff();
      nextCwEvent += durationBetweenSymbols;
      setState(STATE_WAIT_BETWEEN_SYMBOLS);
    }
  }

  if (state == STATE_WAIT_BETWEEN_SYMBOLS) {
    if (millis() < nextCwEvent) {
      pollTouchSensors();
    } else {
      setState(STATE_DELETE_PROCESSED_PADDLE_EVENT);
    }
  }

  if (state == STATE_DELETE_PROCESSED_PADDLE_EVENT) {
    if (readPaddleState() == PADDLE_STATE_DIT || readPaddleState() == PADDLE_STATE_DIT_DAH) {
      ditEvent = 0;
    } else {
      dahEvent = 0;
    }
    setState(readPaddleState() == PADDLE_STATE_NONE ? STATE_IDLE : STATE_TONE_CREATION);
#ifdef DEBUG
    Serial.print("Loop cycles per millisecond "); Serial.println(loopCycles / millis());
#endif
  }
}

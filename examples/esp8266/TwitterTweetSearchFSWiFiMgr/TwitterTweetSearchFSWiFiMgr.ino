#include <FS.h>                          // FS.h has to be first
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <TimeLib.h>
#include <ArduinoJson.h>                  // https://github.com/bblanchon/ArduinoJson
//#include "secret.h"                       // uncomment if using secret.h file with credentials
#include <TwitterWebAPI.h>

//#define MAX7219DISPLAY                    // uncomment if using MAX7219-4-digit-display-for-ESP8266
#ifdef MAX7219DISPLAY
#include <SPI.h>
#include <bitBangedSPI.h>
#include <MAX7219_Dot_Matrix.h>           // https://github.com/SensorsIot/MAX7219-4-digit-display-for-ESP8266
// VCC -> 5V, GND -> GND, DIN -> D7, CS -> D8 (configurable below), CLK -> D5
const byte chips = 4;                     // Number of Display Chips
MAX7219_Dot_Matrix display (chips, D8);   // Chips / LOAD
unsigned long MOVE_INTERVAL = 20;         // (msec) increase to slow, decrease to fast
#endif

bool resetsettings = false;               // true to reset WiFiManager & delete FS files
const char *HOSTNAME= "TwitterDisplay";   // Hostname of your device
std::string search_str = "#dog";          // Default search word for twitter
const char *ntp_server = "pool.ntp.org";  // time1.google.com, time.nist.gov, pool.ntp.org
int timezone = -5;                        // US Eastern timezone -05:00 HRS
unsigned long twi_update_interval = 20;   // (seconds) minimum 5s (180 API calls/15 min). Any value less than 5 is ignored!

// Values below are just a placeholder
#ifndef TWITTERINFO  // Obtain these by creating an app @ https://apps.twitter.com/
  static char const consumer_key[]    = "gkyjeH3EF32NJfiuheuyf8623";
  static char const consumer_sec[]    = "HbY5h$N86hg5jjd987HGFsRjJcMkjLaJw44628sOh353gI3H23";
  static char const accesstoken[]     = "041657084136508135-F3BE63U4Y6b346kj6bnkdlvnjbGsd3V";
  static char const accesstoken_sec[] = "bsekjH8YT3dCWDdsgsdHUgdBiosesDgv43rknU4YY56Tj";
#endif
#ifndef AutoAP_password
  #define AutoAP_password "password"      // Dafault AP Password
#endif
#ifndef ota_location
  #define ota_location "/firmware"       // OTA update location
#endif
#ifndef ota_user
  #define ota_user "admin"               // OTA username
#endif
#ifndef ota_password
  #define ota_password "password"        // OTA password
#endif

//   Dont change anything below this line    //
///////////////////////////////////////////////

unsigned long api_mtbs = twi_update_interval * 1000; //mean time between api requests
unsigned long api_lasttime = 0; 
bool twit_update = false;
std::string search_msg = "No Message Yet!";
unsigned long lastMoved = 0;
int  messageOffset;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntp_server, timezone*3600, 60000);  // NTP server pool, offset (in seconds), update interval (in milliseconds)
WiFiClientSecure espclient;
TwitterClient tcr(espclient, timeClient, consumer_key, consumer_sec, accesstoken, accesstoken_sec);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Helper
#define MODEBUTTON 0
#define SERIALDEBUG true

#ifdef SERIALDEBUG
#define         DEBUG_PRINT(x)    Serial.print(x)
#define         DEBUG_PRINTLN(x)  Serial.println(x)
#define         DEBUG_PRINTF(x,y) Serial.printf(x,y)
#else
#define         DEBUG_PRINT(x)
#define         DEBUG_PRINTLN(x)
#define         DEBUG_PRINTF(x,y)
#endif

// flag for saving data
bool shouldSaveConfig = false;

// callback notifying us of the need to save config
void saveConfigCallback () {
  DEBUG_PRINTLN("Should save config");
  shouldSaveConfig = true;
}

// Write search_str to FS
bool writetoFS(bool saveConfig){
  if (saveConfig) {
    //FS save
    DEBUG_PRINT("Mounting FS...");
    if (SPIFFS.begin() and saveConfig) {
      DEBUG_PRINTLN("Mounted.");
      //save the custom parameters to FS
      DEBUG_PRINT("Saving config: ");
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["search"] = search_str.c_str();

//      SPIFFS.remove("/config.json") ? DEBUG_PRINTLN("removed file") : DEBUG_PRINTLN("failed removing file");

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) DEBUG_PRINTLN("failed to open config file for writing");
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      SPIFFS.end(); return true;
      //end save
    } else {
      DEBUG_PRINTLN("Failed to mount FS");
      SPIFFS.end(); return false;
    }
  } else {
    DEBUG_PRINTLN("SaveConfig is False!");
    SPIFFS.end(); return false;
  }
}

// Read search_str to FS
bool readfromFS() {
  //read configuration from FS json
  DEBUG_PRINT("Mounting FS...");
  if (resetsettings) { SPIFFS.begin(); SPIFFS.remove("/config.json"); SPIFFS.format(); delay(1000);}
  if (SPIFFS.begin()) {
    DEBUG_PRINTLN("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DEBUG_PRINTLN("Reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINTLN("Opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINTLN("\nparsed json");
          String tmpstr = json["search"];
          search_str = std::string(tmpstr.c_str(), tmpstr.length());
          SPIFFS.end(); return true;
        } else {
          DEBUG_PRINTLN("Failed to load json config");
          SPIFFS.end(); return false;
        }
      } else {
        DEBUG_PRINTLN("Failed to open /config.json");
        SPIFFS.end(); return false;
      }
    } else {
      DEBUG_PRINTLN("Coudnt find config.json");
      SPIFFS.end(); return false;
    }
  } else {
    DEBUG_PRINTLN("Failed to mount FS");
    SPIFFS.end(); return false;
  }
  //end read
}

void extractJSON(String tmsg) {
  const char* msg2 = const_cast <char*> (tmsg.c_str());
  DynamicJsonBuffer jsonBuffer;
  JsonObject& response = jsonBuffer.parseObject(msg2);
  
  if (!response.success()) {
    DEBUG_PRINTLN("Failed to parse JSON!");
    DEBUG_PRINTLN(msg2);
//    jsonBuffer.clear();
    return;
  }
  
  if (response.containsKey("statuses")) {
    String usert = response["statuses"][0]["user"]["screen_name"];
    String text = response["statuses"][0]["text"];
    if (text != "") {
      text = "@" + usert + " says " + text;
      search_msg = std::string(text.c_str(), text.length());
    }
  } else if(response.containsKey("errors")) {
    String err = response["errors"][0];
    search_msg = std::string(err.c_str(), err.length());
  } else {
    DEBUG_PRINTLN("No useful data");
  }
  
  jsonBuffer.clear();
  delete [] msg2;
}

void extractTweetText(String tmsg) {
  unsigned int msglen = tmsg.length();
  
  String seatchstr = ",\"text\":\""; 
  unsigned int searchlen = seatchstr.length();
  int pos1 = -1, pos2 = -1;
  for(int i=0; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == seatchstr) {
      pos1 = i + searchlen;
      break;
    }
  }
  seatchstr = "\",\""; 
  searchlen = seatchstr.length();
  for(int i=pos1; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == seatchstr) {
      pos2 = i;
      break;
    }
  }
  String text = tmsg.substring(pos1, pos2);

  seatchstr = ",\"screen_name\":\""; 
  searchlen = seatchstr.length();
  int pos3 = -1, pos4 = -1;
  for(int i=pos2; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == seatchstr) {
      pos3 = i + searchlen;
      break;
    }
  }
  seatchstr = "\",\""; 
  searchlen = seatchstr.length();
  for(int i=pos3; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == seatchstr) {
      pos4 = i;
      break;
    }
  }
  String usert = "@" + tmsg.substring(pos3, pos4);

  if (text.length() >0 ) {
    text =  usert + " says " + text;
    search_msg = std::string(text.c_str(), text.length());
  }
}

void updateDisplay(){
  char *msg = new char[search_msg.length() + 1];
  strcpy(msg, search_msg.c_str());
  #ifdef MAX7219DISPLAY
  display.sendSmooth (msg, messageOffset);

  // next time show one pixel onwards
  if (messageOffset++ >= (int) (strlen (msg) * 8)){
    messageOffset = - chips * 8;
  #endif
    if (twit_update) {
      digitalWrite(LED_BUILTIN, LOW);
	  #ifdef MAX7219DISPLAY
      display.sendString ("--------");
	  #endif
      extractTweetText(tcr.searchTwitter(search_str));
      DEBUG_PRINT("Search: ");
      DEBUG_PRINTLN(search_str.c_str());
      DEBUG_PRINT("MSG: ");
      DEBUG_PRINTLN(search_msg.c_str());
      twit_update = false;
    }
  #ifdef MAX7219DISPLAY
  }
  #endif
  delete [] msg;
  //free(msg);
}  // end of updateDisplay

void handleRoot() {
  String t;
  t += "<html>";
  t += "<head>";
  t += "<meta name='viewport' content='width=device-width,initial-scale=1' />";
  t += "<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />";
  t += "</had>";
  t += "<body>";
  t += "<h3><center>";
  t += "Post to Twitter";
  t += "</center></h3>";
  t += "<center>";
  t += "<form method='POST' action='/tweet'>";
  t += "<input type=text name=text style='width: 40em;' autofocus placeholder='Twitter Message'>";
  t += "<input type=submit name=submit value='Tweet'>";
  t += "</form>";
  t += "</center>";
  t += "</body>";
  t += "</html>";
  server.send(200, "text/html", t);
}

void getSearchWord() {
  String webpage;
  webpage =  "<html>";
  webpage += "<meta name='viewport' content='width=device-width,initial-scale=1' />";
   webpage += "<head><title>Twitter IOT Scrolling Text Display</title>";
    webpage += "<style>";
     webpage += "body { background-color: #E6E6FA; font-family: Arial, Helvetica, Sans-Serif; Color: blue;}";
    webpage += "</style>";
   webpage += "</head>";
   webpage += "<body>";
    webpage += "<br>";  
    webpage += "<form action='http://"+WiFi.localIP().toString()+"/processreadtweet' method='POST'>";
     webpage += "<center><input type='text' name='search_input' value='"+String(search_str.c_str())+"' placeholder='Twitter Search'></center><br>";
     webpage += "<center><input type='submit' value='Update Search Keyword'></center>";
    webpage += "</form>";
   webpage += "</body>";
  webpage += "</html>";
  server.send(200, "text/html", webpage); // Send a response to the client asking for input
}

void handleTweet() {
  if (server.method() == HTTP_POST) {
    std::string text;
    bool submit = false;
    for (uint8_t i=0; i<server.args(); i++){
      if (server.argName(i) == "text") {
        String s = server.arg(i);
        text = std::string(s.c_str(), s.length());
      } else if (server.argName(i) == "submit") {
        submit = true;
      }
    }
//   time_t timevalue = (time_t) timeClient.getEpochTime();
   if (submit && !text.empty()) tcr.tweet(text);
   server.sendHeader("Location", "/", true);
   server.send(302, "text/plain", "");
  }
}

void readTweet(){
  String t;
  t += "<html>";
  t += "<head>";
  t += "<meta name='viewport' content='width=device-width,initial-scale=1' />";
  t += "<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />";
  t += "</had>";
  t += "<body>";
  t += "<center><p>Searching Twitter for: " + String(search_str.c_str()) + "</p></center>";
  t += "<center><p>Latest Message: " + String(search_msg.c_str()) + "</p></center>";
  t += "<br><center><a href='http://" + WiFi.localIP().toString() + "/search'>Update Search Term?</a></center>";
  t += "</form>";
  t += "</body>";
  t += "</html>";
  server.send(200, "text/html", t);
}

void processReadTweet(){
  if (server.args() > 0 and server.method() == HTTP_POST) { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) {
      DEBUG_PRINT(server.argName(i)); // Display the argument
      if (server.argName(i) == "search_input") {
        search_str=std::string(server.arg(i).c_str());
      }
    }
  }
  
  String t;
  t += "<html>";
  t += "<head>";
  t += "<meta name='viewport' content='width=device-width,initial-scale=1' />";
  t += "<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />";
  t += "</had>";
  t += "<body>";
  t += "<center><p>Updated search term: " + String(search_str.c_str()) + "</p></center>";
  t += "<br><center><a href='http://" + WiFi.localIP().toString() + "/search'>Update again?</a></center>";
  t += "</form>";
  t += "</body>";
  t += "</html>";
  server.send(200, "text/html", t);
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void){
  if (twi_update_interval < 5) api_mtbs = 5000; // Cant update faster than 5s.

  #ifdef MAX7219DISPLAY
  display.begin();
  display.setIntensity(9);
  #endif
  
  pinMode(MODEBUTTON, INPUT);  // MODEBUTTON as input for Config mode selection

  //Begin Serial
  Serial.begin(115200);
  
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  WiFiManagerParameter custom_search_str("search", "Twitter Search", search_str.c_str(), 40);
  wifiManager.addParameter(&custom_search_str);

  //reset settings - for testing
  if (resetsettings) wifiManager.resetSettings();
  
  //set minimu quality of signal so it ignores AP's under that quality defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  // Debug output. TRUE: WM debug messages enabled, FALSE: WM debug messages disabled
  wifiManager.setDebugOutput(SERIALDEBUG);
  
  //sets timeout until configuration portal gets turned off, useful to make it all retry or go to sleep in seconds
  wifiManager.setTimeout(180);

  //fetches ssid and pass and tries to connect if it does not connect it starts an access point with the specified name here  "AutoConnectAP" and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(HOSTNAME, AutoAP_password)) {
    DEBUG_PRINTLN("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    //ESP.reset();
    ESP.restart();
    delay(5000);
  }
  
  DEBUG_PRINTLN("connected...yeey :)"); // if you get here you have connected to the WiFi
  search_str = std::string(custom_search_str.getValue()); //read updated parameters
  if(writetoFS(shouldSaveConfig)) DEBUG_PRINTLN("Done writing"); //save the custom parameters to FS
  DEBUG_PRINT("Local IP: ");
  DEBUG_PRINTLN(WiFi.localIP());

  delay(100);


  // Connect to NTP and force-update time
  tcr.startNTP();
  DEBUG_PRINTLN("NTP Synced");
  delay(100);
  // Setup internal LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // MDNS Server
  if (MDNS.begin(HOSTNAME)) {              // Start the mDNS responder for esp8266.local
    DEBUG_PRINTLN("mDNS responder started.");
    DEBUG_PRINT("Goto http://");
    DEBUG_PRINT(HOSTNAME);
    DEBUG_PRINTLN(".local/ on web-browser if you are on the same network!");
    DEBUG_PRINT("or goto http://");
    DEBUG_PRINTLN(WiFi.localIP());
  } else {
    DEBUG_PRINTLN("Error setting up MDNS responder!");
  }

  httpUpdater.setup(&server,ota_location,ota_user,ota_password);
  server.on("/", handleRoot);
  server.on("/search", getSearchWord);
  server.on("/tweet", handleTweet);
  server.on("/readtweet",readTweet);
  server.on("/processreadtweet",processReadTweet);
  server.onNotFound(handleNotFound);
  server.begin();

  MDNS.addService("http", "tcp", 80);
  DEBUG_PRINTLN("HTTP server started");
}

void loop(void){
  server.handleClient(); // WebServer
  
   if ( digitalRead(MODEBUTTON) == LOW ) {
    DEBUG_PRINTLN("+++");
  
    //WiFiManager
    WiFiManager wifiManager;
    wifiManager.setTimeout(180);
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //add all your parameters here
    WiFiManagerParameter custom_search_str("search", "Twitter Search", search_str.c_str(), 40);
    wifiManager.addParameter(&custom_search_str);

    if (!wifiManager.startConfigPortal((const char *) String(String(HOSTNAME) + String("OnDemandAP")).c_str(), AutoAP_password)) {
      DEBUG_PRINTLN("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }
    //read updated parameters
    search_str = std::string(custom_search_str.getValue());

    //FS save
    if(writetoFS(shouldSaveConfig)) DEBUG_PRINTLN("Done writing");;
  }
  
  if ((millis() > api_lasttime + api_mtbs))  {
    timeClient.update();   // NTP time update
    twit_update = true;
    api_lasttime = millis();
  }
  if (millis() - lastMoved >= MOVE_INTERVAL){
    updateDisplay();
    lastMoved = millis();
  }
  yield();
  digitalWrite(LED_BUILTIN, HIGH);
}
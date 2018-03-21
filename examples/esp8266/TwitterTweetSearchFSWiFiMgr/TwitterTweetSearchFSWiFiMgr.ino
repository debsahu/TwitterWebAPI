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
//#define TWI_TIMEOUT 3000                  // varies depending on network speed (msec), needs to be before TwitterWebAPI.h
#include <TwitterWebAPI.h>

// Choose one of the options below for MAX7219 Display
//#define MAX7219DISPLAY                    // uncomment if using MAX7219-4-digit-display-for-ESP8266
//#define MD_PAROLA_DISPLAY                 // uncomment if using MD Parola Library for MAX7219

#ifdef MAX7219DISPLAY
  #include <MAX7219_Dot_Matrix.h>           // https://github.com/SensorsIot/MAX7219-4-digit-display-for-ESP8266
  // VCC -> 5V, GND -> GND, DIN -> D7, CS -> D8 (configurable below), CLK -> D5
  const byte chips = 4;                     // Number of Display Chips
  MAX7219_Dot_Matrix display (chips, D8);   // Chips / LOAD
  unsigned long MOVE_INTERVAL = 20;         // (msec) increase to slow, decrease to fast
#endif

#ifdef MD_PAROLA_DISPLAY
  #include <MD_Parola.h>                    // https://github.com/MajicDesigns/MD_Parola
  #include <MD_MAX72xx.h>                   // https://github.com/MajicDesigns/MD_MAX72xx
  #include <SPI.h>                          // ^ edit MD_MAX72xx.h #define USE_PAROLA_HW 0 and #define USE_FC16_HW 1 if using FC16 dotmatrix display
  // VCC -> 5V, GND -> GND, DIN -> D7, CS -> D8, CLK -> D5
  #define MAX_DEVICES 4
  #define CLK_PIN    D5
  #define DATA_PIN   D7
  #define CS_PIN     D8
#endif

bool resetsettings = false;               // true to reset WiFiManager & delete FS files
const char *HOSTNAME= "TwitterDisplay";   // Hostname of your device
std::string search_str = "#dog";          // Default search word for twitter
const char *ntp_server = "pool.ntp.org";  // time1.google.com, time.nist.gov, pool.ntp.org
int timezone = -4;                        // US Eastern timezone -05:00 HRS
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

#if defined(MAX7219DISPLAY) and defined(MD_PAROLA_DISPLAY)
  #error "Cant have both MAX7219DISPLAY and MD_PAROLA_DISPLAY enabled."
#endif

unsigned long api_mtbs = twi_update_interval * 1000; //mean time between api requests
unsigned long api_lasttime = 0; 
bool twit_update = false;
std::string search_msg = "No Message Yet!";
#ifdef MAX7219DISPLAY
unsigned long lastMoved = 0;
int  messageOffset;
#endif
#ifdef MD_PAROLA_DISPLAY
MD_Parola P = MD_Parola(DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);
char curmsg[512];
#endif

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntp_server, timezone*3600, 60000);  // NTP server pool, offset (in seconds), update interval (in milliseconds)
TwitterClient tcr(timeClient, consumer_key, consumer_sec, accesstoken, accesstoken_sec);
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

// Helper
#define MODEBUTTON 0
#define LED_BUILTIN 2
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

String convertUnicode(String unicodeStr){
  String out = "";
  char iChar;
  char* error;
  for (int i = 0; i < unicodeStr.length(); i++){
     iChar = unicodeStr[i];
     if(iChar == '\\'){ // got escape char
       iChar = unicodeStr[++i];
       if(iChar == 'u'){ // got unicode hex
         char unicode[6];
         unicode[0] = '0';
         unicode[1] = 'x';
         for (int j = 0; j < 4; j++){
           iChar = unicodeStr[++i];
           unicode[j + 2] = iChar;
         }
         long unicodeVal = strtol(unicode, &error, 16); //convert the string
         out += (char)unicodeVal;
       } else if(iChar == '/'){
         out += iChar;
       } else if(iChar == 'n'){
         out += '\n';
       }
     } else {
       out += iChar;
     }
  }
  return out;
}

// flag for saving data
bool shouldSaveConfig = false;

// callback notifying us of the need to save config
void saveConfigCallback () {
  DEBUG_PRINTLN("Should save config");
  shouldSaveConfig = true;
}

bool updateFS = false;
// Write search_str to FS
bool writetoFS(bool saveConfig){
  if (saveConfig) {
    //FS save
    DEBUG_PRINT("Mounting FS...");
    if (SPIFFS.begin() and saveConfig) {
      updateFS = true;
      DEBUG_PRINTLN("Mounted.");
      //save the custom parameters to FS
      DEBUG_PRINT("Saving config: ");
//      DynamicJsonBuffer jsonBuffer;
      StaticJsonBuffer<200> jsonBuffer;
      JsonObject& json = jsonBuffer.createObject();
      json["search"] = search_str.c_str();

//      SPIFFS.remove("/config.json") ? DEBUG_PRINTLN("removed file") : DEBUG_PRINTLN("failed removing file");

      File configFile = SPIFFS.open("/config.json", "w");
      if (!configFile) DEBUG_PRINTLN("failed to open config file for writing");
  
      json.printTo(Serial);
      json.printTo(configFile);
      configFile.close();
      updateFS = false;
      SPIFFS.end();
      return true;
      //end save
    } else {
      DEBUG_PRINTLN("Failed to mount FS");
//      SPIFFS.end();
      return false;
    }
  } else {
    DEBUG_PRINTLN("SaveConfig is False!");
//    SPIFFS.end(); 
    return false;
  }
}

// Read search_str to FS
bool readfromFS() {
  //read configuration from FS json
  DEBUG_PRINT("Mounting FS...");
  updateFS = true;
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
//        DynamicJsonBuffer jsonBuffer;
        StaticJsonBuffer<JSON_OBJECT_SIZE(10)> jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINTLN("\nparsed json");
          String tmpstr = json["search"];
          search_str = std::string(tmpstr.c_str(), tmpstr.length());
          SPIFFS.end();
          updateFS = false;
          return true;
        } else {
          DEBUG_PRINTLN("Failed to load json config");
        }
      } else {
        DEBUG_PRINTLN("Failed to open /config.json");
      }
    } else {
      DEBUG_PRINTLN("Coudnt find config.json");
    }
  } else {
    DEBUG_PRINTLN("Failed to mount FS");
  }
  //end read
  updateFS = false;
  return false;
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
  DEBUG_PRINT("Recieved Message Length");
  long msglen = tmsg.length();
  DEBUG_PRINT(": ");
  DEBUG_PRINTLN(msglen);
//  DEBUG_PRINT("MSG: ");
//  DEBUG_PRINTLN(tmsg);
  if (msglen <= 31) return;
  
  String searchstr = ",\"text\":\""; 
  unsigned int searchlen = searchstr.length();
  int pos1 = -1, pos2 = -1;
  for(long i=0; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos1 = i + searchlen;
      break;
    }
  }
  searchstr = "\",\""; 
  searchlen = searchstr.length();
  for(long i=pos1; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos2 = i;
      break;
    }
  }
  String text = tmsg.substring(pos1, pos2);

  searchstr = ",\"screen_name\":\""; 
  searchlen = searchstr.length();
  int pos3 = -1, pos4 = -1;
  for(long i=pos2; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos3 = i + searchlen;
      break;
    }
  }
  searchstr = "\",\""; 
  searchlen = searchstr.length();
  for(long i=pos3; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos4 = i;
      break;
    }
  }
  String usert = "@" + tmsg.substring(pos3, pos4);

  if (text.length() > 0) {
    if (usert.length() > 1) text =  usert + " says " + text;
    text=convertUnicode(text);
    search_msg = std::string(text.c_str(), text.length());
  }
}

void updateDisplay(){
  #ifdef MAX7219DISPLAY
  char *msg = new char[search_msg.length() + 1];
  strcpy(msg, search_msg.c_str());
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
      #ifdef MD_PAROLA_DISPLAY
      P.displayClear();
      P.print("--------");
      #endif
      extractTweetText(tcr.searchTwitter(search_str));
//      extractJSON(tcr.searchTwitter(search_str)); // ArduinoJSON crashes esp8266, twitter info is too long
      DEBUG_PRINT("Search: ");
      DEBUG_PRINTLN(search_str.c_str());
      DEBUG_PRINT("MSG: ");
      DEBUG_PRINTLN(search_msg.c_str());
      #ifdef MD_PAROLA_DISPLAY
      strcpy(curmsg,search_msg.c_str());
      P.displayClear();
      P.displayText(curmsg,PA_LEFT,25,1000,PA_SCROLL_LEFT,PA_SCROLL_LEFT);
      #endif
      twit_update = false;
    }
  #ifdef MAX7219DISPLAY
  }
  delete [] msg;
  //free(msg);
  #endif
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
    webpage += "<form action='/processreadtweet' method='POST'>";
     webpage += "<center><input type='text' name='search_input' value='"+String(search_str.c_str())+"' placeholder='Twitter Search'></center><br>";
     webpage += "<center><input type='submit' value='Update Search Keyword'></center>";
      webpage += "<br><center><a href='/readtweet'>Latest Received Message</a></center>";
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
  t += "<br><center><a href='/search'>Update Search Term?</a></center>";
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
        DEBUG_PRINT(" : ");
        DEBUG_PRINTLN(server.arg(i));
        search_str=std::string(server.arg(i).c_str());
        if(writetoFS(true)) DEBUG_PRINTLN(". done writing!"); // FS save
      }
    }
  }
  
  String t;
  t += "<html>";
  t += "<head>";
  t += "<meta name='viewport' content='width=device-width,initial-scale=1' />";
  t += "<meta http-equiv='Content-Type' content='text/html; charset=utf-8' />";
  t += "<meta http-equiv='refresh' content='3;url=/search'/>";
  t += "</had>";
  t += "<body>";
  t += "<center><p>Updated search term: " + String(search_str.c_str()) + "</p></center>";
  t += "<br><center><a href='/search'>Update again?</a></center>";
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
  #ifdef MD_PAROLA_DISPLAY
  strcpy(curmsg,search_msg.c_str());
  P.begin();
  P.setInvert(false);
  P.setIntensity(7);
  P.displayText(curmsg,PA_LEFT,25,1000,PA_SCROLL_LEFT,PA_SCROLL_LEFT);
  #endif
  pinMode(MODEBUTTON, INPUT);  // MODEBUTTON as input for Config mode selection

  //Begin Serial
  Serial.begin(115200);
  if(readfromFS()) DEBUG_PRINTLN("Done reading");
  
  //WiFiManager
  WiFiManager wifiManager;                                          //Local intialization. Once its business is done, there is no need to keep it around
  wifiManager.setSaveConfigCallback(saveConfigCallback);            //set config save notify callback
  WiFiManagerParameter custom_search_str("search", "Twitter Search", search_str.c_str(), 40);
  wifiManager.addParameter(&custom_search_str);
  if (resetsettings) wifiManager.resetSettings();                   //reset settings - for testing
  //wifiManager.setMinimumSignalQuality();                            //set minimu quality of signal so it ignores AP's under that quality defaults to 8%
  wifiManager.setDebugOutput(SERIALDEBUG);                          // Debug output. TRUE: WM debug messages enabled, FALSE: WM debug messages disabled
  wifiManager.setTimeout(180);                                      //sets timeout until configuration portal gets turned off, useful to make it all retry or go to sleep in seconds
  if (!wifiManager.autoConnect(HOSTNAME, AutoAP_password)) {        //fetches ssid and pass and tries to connect if it does not connect it starts an access point with the specified name here  "AutoConnectAP" and goes into a blocking loop awaiting configuration
    DEBUG_PRINTLN("failed to connect and hit timeout");
    delay(3000);
    //ESP.reset(); //reset and try again, or maybe put it to deep sleep
    ESP.restart(); //restart and try again, or maybe put it to deep sleep
    delay(5000);
  }
  DEBUG_PRINT("Local IP: "); DEBUG_PRINTLN(WiFi.localIP());
  DEBUG_PRINTLN("connected.");                                       // if you get here you have connected to the WiFi
  search_str = std::string(custom_search_str.getValue());            //read updated parameters
  if(writetoFS(shouldSaveConfig)) DEBUG_PRINTLN("Done writing");     //save the custom parameters to FS
  delay(100);

  // Connect to NTP and force-update time
  tcr.startNTP();
  DEBUG_PRINTLN("NTP Synced");
  delay(100);
  
  // Setup internal LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  // MDNS Server
  if (MDNS.begin(HOSTNAME)) {                                        // Start the mDNS responder for esp8266.local
    DEBUG_PRINTLN("mDNS responder started.");
    DEBUG_PRINT("Goto http://");
    DEBUG_PRINT(HOSTNAME);
    DEBUG_PRINTLN(".local/ on bojour-client if you are on the same network!");
    DEBUG_PRINT("or goto http://"); DEBUG_PRINT(WiFi.localIP()); DEBUG_PRINTLN("/");
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
    WiFiManagerParameter custom_search_str("search", "Twitter Search", search_str.c_str(), 40);
    wifiManager.addParameter(&custom_search_str);
    if (!wifiManager.startConfigPortal((const char *) String(String(HOSTNAME) + String("OnDemandAP")).c_str(), AutoAP_password)) {
      DEBUG_PRINTLN("failed to connect and hit timeout");
      delay(3000); ESP.reset(); delay(5000);
    }
    search_str = std::string(custom_search_str.getValue());           //read updated parameters
    if(writetoFS(shouldSaveConfig)) DEBUG_PRINTLN("Done writing");;   //FS save
  }
  
  if ((millis() > api_lasttime + api_mtbs) and !(updateFS))  {
    twit_update = true;
    #ifndef MAX7219DISPLAY
    updateDisplay();
    #endif
    api_lasttime = millis();
  }
  #ifdef MAX7219DISPLAY
  if (millis() - lastMoved >= MOVE_INTERVAL){
    updateDisplay();
    lastMoved = millis();
  }
  #endif
  #ifdef MD_PAROLA_DISPLAY
  if (P.displayAnimate()) {
    updateDisplay();
    P.displayReset();
  }
  #endif
  yield();
  delay(2); //do something or else esp8266 is not happy, can remove this line if doing something else
  digitalWrite(LED_BUILTIN, HIGH);
}

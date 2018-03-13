#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>                  // https://github.com/bblanchon/ArduinoJson
//#include "secret.h"                       // uncomment if using secret.h file with credentials
//#define TWI_TIMEOUT 3000                  // varies depending on network speed (msec), needs to be before TwitterWebAPI.h
#include <TwitterWebAPI.h>

#ifndef WIFICONFIG
const char* ssid = "wifi_ssid";           // WiFi SSID
const char* password = "wifi_password";   // WiFi Password
#endif

std::string search_str = "@debsahu";      // Default search word for twitter
const char *ntp_server = "pool.ntp.org";  // time1.google.com, time.nist.gov, pool.ntp.org
int timezone = -5;                        // US Eastern timezone -05:00 HRS
unsigned long twi_update_interval = 20;   // (seconds) minimum 5s (180 API calls/15 min). Any value less than 5 is ignored!

#ifndef TWITTERINFO  // Obtain these by creating an app @ https://apps.twitter.com/
  static char const consumer_key[]    = "gkyjeH3EF32NJfiuheuyf8623";
  static char const consumer_sec[]    = "HbY5h$N86hg5jjd987HGFsRjJcMkjLaJw44628sOh353gI3H23";
  static char const accesstoken[]     = "041657084136508135-F3BE63U4Y6b346kj6bnkdlvnjbGsd3V";
  static char const accesstoken_sec[] = "bsekjH8YT3dCWDdsgsdHUgdBiosesDgv43rknU4YY56Tj";
#endif

//   Dont change anything below this line    //
///////////////////////////////////////////////

unsigned long api_mtbs = twi_update_interval * 1000; //mean time between api requests
unsigned long api_lasttime = 0; 
bool twit_update = false;
std::string search_msg = "No Message Yet!";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntp_server, timezone*3600, 60000);  // NTP server pool, offset (in seconds), update interval (in milliseconds)
TwitterClient tcr(timeClient, consumer_key, consumer_sec, accesstoken, accesstoken_sec);

void setup(void){
  //Begin Serial
  Serial.begin(115200);
  // WiFi Connection
  WiFi.begin(ssid, password);
  Serial.print("\nConnecting to ");
  Serial.print(ssid);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected. yay!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(100);
  // Connect to NTP and force-update time
  tcr.startNTP();
  Serial.println("NTP Synced");
  delay(100);
  // Setup internal LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  if (twi_update_interval < 5) api_mtbs = 5000; // Cant update faster than 5s.
}

void extractJSON(String tmsg) {
  const char* msg2 = const_cast <char*> (tmsg.c_str());
//  DynamicJsonBuffer jsonBuffer;
  const size_t bufferSize = 5*JSON_ARRAY_SIZE(0) + 4*JSON_ARRAY_SIZE(1) + 3*JSON_ARRAY_SIZE(2) + 3*JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(2) + 8*JSON_OBJECT_SIZE(3) + 3*JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 2*JSON_OBJECT_SIZE(10) + JSON_OBJECT_SIZE(24) + JSON_OBJECT_SIZE(43) + 6060;
  DynamicJsonBuffer jsonBuffer(bufferSize);
  JsonObject& response = jsonBuffer.parseObject(msg2);
  
  if (!response.success()) {
    Serial.println("Failed to parse JSON!");
    Serial.println(msg2);
//    jsonBuffer.clear();
    return;
  }
  String namet = response["name"];
  String followers_count = response["followers_count"];
  String text = namet + " has " + followers_count + " followers.";
  search_msg = std::string(text.c_str(), text.length());
  
  jsonBuffer.clear();
  delete [] msg2;
}

void extractTweetText(String tmsg) {
  Serial.print("Recieved Message Length");
  long msglen = tmsg.length();
  Serial.print(": ");
  Serial.println(msglen);
  if (msglen <= 32) return;
  
  String searchstr = ",\"name\":\""; 
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

  searchstr = ",\"followers_count\":"; 
  searchlen = searchstr.length();
  int pos3 = -1, pos4 = -1;
  for(long i=pos2; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos3 = i + searchlen;
      break;
    }
  }
  searchstr = ","; 
  searchlen = searchstr.length();
  for(long i=pos3; i <= msglen - searchlen; i++) {
    if(tmsg.substring(i,searchlen+i) == searchstr) {
      pos4 = i;
      break;
    }
  }
  String usert = tmsg.substring(pos3, pos4);

  if (text.length() > 0) {
    text =  text + " has " + usert + " followers.";
    search_msg = std::string(text.c_str(), text.length());
  }
}

void loop(void){
  if (millis() > api_lasttime + api_mtbs)  {
    digitalWrite(LED_BUILTIN, LOW);
//    extractJSON(tcr.searchUser(search_str));
    extractTweetText(tcr.searchUser(search_str));
    Serial.print("Search: ");
    Serial.println(search_str.c_str());
    Serial.print("MSG: ");
    Serial.println(search_msg.c_str());
    api_lasttime = millis();
  }
  delay(2);
  yield();
  digitalWrite(LED_BUILTIN, HIGH);
}

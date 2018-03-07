#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <ArduinoJson.h>                  // https://github.com/bblanchon/ArduinoJson
//#include "secret.h"                       // uncomment if using secret.h file with credentials
#include <TwitterWebAPI.h>

#ifndef WIFICONFIG
const char* ssid = "wifi_ssid";           // WiFi SSID
const char* password = "wifi_password";   // WiFi Password
#endif

std::string search_str = "#dog";          // Default search word for twitter
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
WiFiClientSecure espclient;
TwitterClient tcr(espclient, timeClient, consumer_key, consumer_sec, accesstoken, accesstoken_sec);

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

void loop(void){
  if (millis() > api_lasttime + api_mtbs)  {
    digitalWrite(LED_BUILTIN, LOW);
    String tmsg = tcr.searchtwitter(search_str);
    if (tmsg.length()>0 and tmsg.length()<500) search_msg = std::string(tmsg.c_str(), tmsg.length());
    }
    Serial.print("Search: ");
    Serial.println(search_str.c_str());
    Serial.print("MSG: ");
    Serial.println(search_msg.c_str());
    api_lasttime = millis();
  yield();
  digitalWrite(LED_BUILTIN, HIGH);
}
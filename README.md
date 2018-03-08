# TwitterWebAPI

An Arduino library to talk to Twitter using [Twitter Web API](https://dev.twitter.com/overview/api) made for ESP8266. This is based on a sketch posted [here](https://github.com/soramimi/ESP8266Tweet). There are other approaches like using a bearer token [arduino-twitter-api](https://github.com/witnessmenow/arduino-twitter-api), but there are limitations in terms of not being able to send tweet. This can both search/read and post tweets.

## Consumer Key, Consumer Secret, Access Token & Access Token Secret
In order to talk to Twitter,

* Goto https://apps.twitter.com/app/new and sign in (if you havent already).
* Fill in the fields, For website you can enter any webpage (e.g. http://google.com), and create your app
* Then click on the Keys & Tokens tab. Your Consumer Key and Consumer Secret will be there, if not click on Generate.

Fill the obtained Consumer Key, Consumer Secret, Access Token and Access Token Secret inside the sketch.

## Using the Library
* Download this GitHub [library](https://github.com/debsahu/TwitterWebAPI/archive/master.zip).
* In Arduino, Goto Sketch -> Include Library -> Add .ZIP Library... and point to the zip file downloaded.

To use in your sketch include these lines.
```
#include <TwitterWebAPI.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
```
Setup correct timezone to correct the time obtained from NTP server.
```
const char *ntp_server = "pool.ntp.org";  // time1.google.com, time.nist.gov, pool.ntp.org
int timezone = -5;                        // US Eastern timezone -05:00 HRS
```
You **WILL** need **Consumer Key, Consumer Secret, Access Token and Access Token Secret** that can be obtained from the above steps. 
```
// Values below are just a placeholder
// Obtain these by creating an app @ https://apps.twitter.com/
  static char const consumer_key[]    = "gkyjeH3EF32NJfiuheuyf8623";
  static char const consumer_sec[]    = "HbY5h$N86hg5jjd987HGFsRjJcMkjLaJw44628sOh353gI3H23";
  static char const accesstoken[]     = "041657084136508135-F3BE63U4Y6b346kj6bnkdlvnjbGsd3V";
  static char const accesstoken_sec[] = "bsekjH8YT3dCWDdsgsdHUgdBiosesDgv43rknU4YY56Tj";
```
Declare clients before setup().
```
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntp_server, timezone*3600, 60000);  // NTP server pool, offset (in seconds), update interval (in milliseconds)
WiFiClientSecure espclient;
TwitterClient tcr(espclient, timeClient, consumer_key, consumer_sec, accesstoken, accesstoken_sec);
```
In setup make sure to start NTP connection. A correct time is required to be able to post/search on Twitter.
```
tcr.startNTP();
```
**Search for a key word**
```
std::string search_str;
String tmsg = tcr.searchtwitter(search_str);
```
**Post to Twitter**
```
std::string twitter_post_msg;
tcr.tweet(twitter_post_msg);
```

//WiFiManager AP Password
#define AutoAP_password "wifiappassword" //password for WiFiManager AP

//OTA
#define ota_location "/firmware"        // OTA update location
#define ota_user "ota_admin"            // OTA username
#define ota_password "ota_pwd"          // OTA password

//Display
//#define MAX7219DISPLAY                    // uncomment if using MAX7219-4-digit-display-for-ESP8266
#define MD_PAROLA_DISPLAY                 // uncomment to use MD Parola Library

// Twitter info
#define TWITTERINFO
// Values below are just a placeholder
#ifdef TWITTERINFO  // Obtain these by creating an app @ https://apps.twitter.com/
  static char const consumer_key[]    = "gkyjeH3EF32NJfiuheuyf8623";
  static char const consumer_sec[]    = "HbY5h$N86hg5jjd987HGFsRjJcMkjLaJw44628sOh353gI3H23";
  static char const accesstoken[]     = "041657084136508135-F3BE63U4Y6b346kj6bnkdlvnjbGsd3V";
  static char const accesstoken_sec[] = "bsekjH8YT3dCWDdsgsdHUgdBiosesDgv43rknU4YY56Tj";
#endif
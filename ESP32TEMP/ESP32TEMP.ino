/*
Please read README.md file in this folder, or on the web:
https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFiProv/examples/WiFiProv

Note: This sketch takes up a lot of space for the app and may not be able to flash with default setting on some chips.
  If you see Error like this: "Sketch too big"
  In Arduino IDE go to: Tools > Partition scheme > chose anything that has more than 1.4MB APP
   - for example "No OTA (2MB APP/2MB SPIFFS)"
*/
/* Comment this out to disable prints and save space */
#define BLYNK_PRINT Serial
/* Fill in information from Blynk Device Info here */
//#define BLYNK_TEMPLATE_ID "TMPL6fQP4aXvb"
//#define BLYNK_TEMPLATE_NAME "Humanoid"
//#define BLYNK_AUTH_TOKEN "fLaAB_KTDRqopOsoreXXiCNCvQT3Ff3H"
#define BLYNK_TEMPLATE_ID   "TMPLibh8kPh5"
#define BLYNK_TEMPLATE_NAME "Boronia"
#define BLYNK_AUTH_TOKEN    "TH81HedGK74cZl8MQvfG-w8yD5LA9m9g" // Boronia

#include "sdkconfig.h"
#if CONFIG_ESP_WIFI_REMOTE_ENABLED
#error "WiFiProv is only supported in SoCs with native Wi-Fi support"
#endif

#include <WiFiProv.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

#include <EEPROM.h>
#include <math.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TimeLib.h>
#include <WidgetRTC.h>
#include <OneButton.h>

#include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
#include "SSD1306Wire.h" // legacy include: `#include "SSD1306.h"`


#define OLED

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 26// 25

#define  RELAY_ON   1 //LOW
#define  RELAY_OFF  0 //HIGH

#define  LED_ON   1 //LOW
#define  LED_OFF  0 //HIGH

#define PIN_INPUT 15

#define BLYNK_RED       "#D3435C"

#define BLYNK_GREEN     "#23C48E"

#define BLYNK_DARK_BLUE "#5F7CD8"
#define BLYNK_BLUE      "#04C0F8"
#define BLYNK_WHITE     "#FFFFFF"
#define BLYNK_YELLOW    "#ED9D00"

#define TEMPERATURE_PRECISION 11 //9

const int relay_pin = 4; //16;   // gpio4
const int led_pin   = 32; //23;   // gpio12
const int btn_pin   = 15; //23;   

SSD1306Wire display(0x3c, SDA, SCL);   

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

// arrays to hold device addresses
DeviceAddress insideThermometer, outsideThermometer;

BlynkTimer timer;
WidgetLCD lcd(V9);
WidgetRTC rtc;
WidgetLED led1(V7), led2(V8);

OneButton button(PIN_INPUT, true);

String strShortMessage;

int    ScanResult, ConnectResult;

int  s_led_now, s_display_sw;

float  g_temperature;  
float  TempC = 0.0, TempF = 0.0, TempCI = 0.0, TempCO = 0.0; 


 
struct WIFI {
  char  SID[51];
  char  PWD[51];
  char  AUT[51];
  char  REV[21];
};

struct NOW {
  boolean  g_checked;
  uint8_t  g_duration;
};

struct TEMP {
  float    g_temperature;
  uint8_t  g_start_hour;
  uint8_t  g_end_hour;
};

struct {
  struct WIFI W;
  struct NOW  N;
  struct TEMP T[4];
} Info;

// #define USE_SOFT_AP // Uncomment if you want to enforce using the Soft AP method instead of BLE
const char *pop = "abcd1234";           // Proof of possession - otherwise called a PIN - string provided by the device, entered by the user in the phone app
const char *service_name = "PROV_123";  // Name of your device (the Espressif apps expects by default device name starting with "Prov_")
const char *service_key = NULL;         // Password used for SofAP method (NULL = no password needed)
bool reset_provisioned = true;          // When true the library will automatically delete previously provisioned data.

// WARNING: SysProvEvent is called from a separate FreeRTOS task (thread)!
void SysProvEvent(arduino_event_t *sys_event) {
  switch (sys_event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.print("\nConnected IP address : ");
      Serial.println(IPAddress(sys_event->event_info.got_ip.ip_info.ip.addr));
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.println("\nDisconnected. Connecting to the AP again... "); break;
    case ARDUINO_EVENT_PROV_START:            Serial.println("\nProvisioning started\nGive Credentials of your access point using smartphone app"); break;
    case ARDUINO_EVENT_PROV_CRED_RECV:
    {
      Serial.println("\nReceived Wi-Fi credentials");
      Serial.print("\tSSID : ");
      Serial.println((const char *)sys_event->event_info.prov_cred_recv.ssid);
      Serial.print("\tPassword : ");
      Serial.println((char const *)sys_event->event_info.prov_cred_recv.password);
      strcpy(Info.W.SID, (char const *)sys_event->event_info.prov_cred_recv.ssid);
      strcpy(Info.W.PWD, (char const *)sys_event->event_info.prov_cred_recv.password);
      WriteAllToEEPROM();
      DisplayMessage("Temp Control", "Provision", "credentials");
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_FAIL:
    {
      Serial.println("\nProvisioning failed!\nPlease reset to factory and retry provisioning\n");
      if (sys_event->event_info.prov_fail_reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) {
        Serial.println("\nWi-Fi AP password incorrect");
      } else {
        Serial.println("\nWi-Fi AP not found....Add API \" nvs_flash_erase() \" before beginProvision()");
      }
      break;
    }
    case ARDUINO_EVENT_PROV_CRED_SUCCESS: {
      DisplayMessage("Temp Control", "Provision", "success");
      Serial.println("\nProvisioning Successful");
      break;
    }
    case ARDUINO_EVENT_PROV_END:          Serial.println("\nProvisioning Ends"); ESP.restart();  break;
    default:                              break;
  }
}


// BLYNK Subroutine
BLYNK_CONNECTED() {
    // Request Blynk server to re-send latest values for all pins
 
    Blynk.syncAll();
    Serial.println("Sync!");
/*
    Blynk.virtualWrite(V1, Info.N.g_checked); 
    Blynk.virtualWrite(V2, Info.N.g_duration );   
    Blynk.virtualWrite(V3, Info.T[0].g_temperature );   
    Blynk.virtualWrite(V4, Info.T[1].g_temperature );   
    Blynk.virtualWrite(V5, Info.T[2].g_temperature );   
    Blynk.virtualWrite(V6, Info.T[3].g_temperature );   
*/   
    led1.off();
    led2.off();

}

BLYNK_WRITE(V1) // Button
{
  int pinValue = param.asInt();

  if( pinValue == 1 ) {
       led2.off(); // time mode 
       HeaterOn(1, "by button");  
  } else {
       HeaterOff(0, "by button");
  }
  Info.N.g_checked = pinValue;

}

BLYNK_WRITE(V2) // Duration Slider
{
  Info.N.g_duration = param.asInt();
  WriteToEEPROM(long(&Info.N.g_duration), sizeof(Info.N.g_duration));  
}

BLYNK_WRITE(V3) // 22-04h Slider 
{
  Info.T[0].g_temperature = param.asFloat();
  WriteToEEPROM(long(&Info.T[0].g_temperature), sizeof(Info.T[0].g_temperature));  
}

BLYNK_WRITE(V4) // 04-08h Slider
{
  Info.T[1].g_temperature = param.asFloat();
  WriteToEEPROM(long(&Info.T[1].g_temperature), sizeof(Info.T[1].g_temperature)); 
}

BLYNK_WRITE(V5) // 08-16h Slider
{
  Info.T[2].g_temperature = param.asFloat();
  WriteToEEPROM(long(&Info.T[2].g_temperature), sizeof(Info.T[2].g_temperature));  
}

BLYNK_WRITE(V6) // 16-22h Slider
{
  Info.T[3].g_temperature = param.asFloat(); 
  WriteToEEPROM(long(&Info.T[3].g_temperature), sizeof(Info.T[3].g_temperature));  
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

// function to print the temperature for a device
float printTemperature(DeviceAddress deviceAddress)
{
  float tempC = sensors.getTempC(deviceAddress);
  if (tempC == DEVICE_DISCONNECTED_C)
  {
    Serial.println("Error: Could not read temperature data");
    return 0;
  }
  Serial.print("Temp C: ");
  Serial.println(tempC);
//  Serial.print(" Temp F: ");
//  Serial.print(DallasTemperature::toFahrenheit(tempC));
  return tempC;
}

// function to print a device's resolution
void printResolution(DeviceAddress deviceAddress)
{
  Serial.print("Resolution: ");
  Serial.print(sensors.getResolution(deviceAddress));
  Serial.println();
}

// main function to print information about a device
float printData(DeviceAddress deviceAddress)
{
  Serial.print("Device Address: ");
  printAddress(deviceAddress);
  Serial.print(" ");
  return printTemperature(deviceAddress);
}

void SensorSetup(void) // Temperature sensor set up
{
  // start serial port
  Serial.println("Dallas Temperature IC Control Set up");

  // Start up the library
  sensors.begin();

  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  // report parasite power requirements
  Serial.print("Parasite power is: ");
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");

  // Search for devices on the bus and assign based on an index. Ideally,
  // you would do this to initially discover addresses on the bus and then
  // use those addresses and manually assign them (see above) once you know
  // the devices on your bus (and assuming they don't change).
  //
  // method 1: by index
  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(outsideThermometer, 1)) Serial.println("Unable to find address for Device 1");

  // method 2: search()
  // search() looks for the next device. Returns 1 if a new address has been
  // returned. A zero might mean that the bus is shorted, there are no devices,
  // or you have already retrieved all of them. It might be a good idea to
  // check the CRC to make sure you didn't get garbage. The order is
  // deterministic. You will always get the same devices in the same order
  //
  // Must be called before search()
  //oneWire.reset_search();
  // assigns the first address found to insideThermometer
  //if (!oneWire.search(insideThermometer)) Serial.println("Unable to find address for insideThermometer");
  // assigns the seconds address found to outsideThermometer
  //if (!oneWire.search(outsideThermometer)) Serial.println("Unable to find address for outsideThermometer");

  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(insideThermometer);
  Serial.println();

  Serial.print("Device 1 Address: ");
  printAddress(outsideThermometer);
  Serial.println();

  // set the resolution to 9 bit per device
  sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);
  sensors.setResolution(outsideThermometer, TEMPERATURE_PRECISION);

  Serial.print("Device 0 Resolution: ");
  Serial.print(sensors.getResolution(insideThermometer), DEC);
  Serial.println();

  Serial.print("Device 1 Resolution: ");
  Serial.print(sensors.getResolution(outsideThermometer), DEC);
  Serial.println();
}

//
void Initialize()
{ 

  s_display_sw = 1;
  g_temperature = 0.0f;
  s_led_now   = 0;
  strShortMessage  = "";
  ScanResult    = 0;
  ConnectResult = -1;

  pinMode(relay_pin, OUTPUT);
  pinMode(32, OUTPUT);
  
  digitalWrite(32,   1);    
  digitalWrite(relay_pin, RELAY_OFF); 

  Serial.begin(115200); 

  SensorSetup();
  
  EEPROM.begin( sizeof(Info) );

  ReadAllFromEEPROM();

  Info.N.g_checked = false;

  Info.T[0].g_start_hour = 22;
  Info.T[0].g_end_hour = 5;
  Info.T[1].g_start_hour = 5;
  Info.T[1].g_end_hour = 8;
  Info.T[2].g_start_hour = 8;
  Info.T[2].g_end_hour = 16;
  Info.T[3].g_start_hour = 16;
  Info.T[3].g_end_hour = 22;      

  Serial.println("MSG\tSystem Initialize !");
  delay(2000);

  Serial.println(Info.N.g_duration);
  Serial.println(Info.T[0].g_temperature);
  Serial.println(Info.T[1].g_temperature);
  Serial.println(Info.T[2].g_temperature);
  Serial.println(Info.T[3].g_temperature);
  // link functions to be called on events.
  //button.attachLongPressStart(LongPressStart, &button);
  //button.attachDuringLongPress(DuringLongPress, &button);
  //button.attachLongPressStop(LongPressStop, &button);

    button.attachClick(ClickPress, &button);
  //  button.attachDoubleClick(DoublePress, &button);


 // button.setLongPressIntervalMs(1000);

 test();

}

void test() {

   String strTime, strTemp;
 
   while(1) {
    getTemperature();

   strTime = String(hour()) + ":" + String(minute()) + ":" + String(second()) + " " + String(day()) + "/" + monthShortStr(month());
   strTemp = String(TempCI) + " : " + String(TempCO);
// String(TempC); // String(TempCI) + " : " + String(TempCO);

    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "Test");
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 22, strTime);
    display.drawString(64, 42, strTemp);
    display.display();

    digitalWrite(relay_pin, RELAY_ON);
    delay(1000);
    digitalWrite(relay_pin, RELAY_OFF);
    delay(1000);
    
   }

}
void DisplayMessage(const char *msg1, const char *msg2, const char *msg3)
{
 if( s_display_sw == 1 ) {
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, msg1);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 22, msg2);
    display.drawString(64, 42, msg3);
    display.display();
  } else {
    display.clear();
    display.display();
  }

 lcd.clear();
 lcd.print(0, 0, msg1 );   
 lcd.print(0, 1, msg3 );  
}

void DisplayAction(const char *msg3)
{
  /*
  #ifdef OLED
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 42, msg3);
    display.display();
  #endif
  */

 lcd.print(0, 1, "                " );  
 lcd.print(0, 1, msg3 );  
}

void CheckStatus()
{
  static int gab_minute;
  int    t_hour;

  if( Info.N.g_checked == true ) {       // Now On
     gab_minute ++;           
     if( gab_minute  >  Info.N.g_duration  ) {
        Info.N.g_checked  =  false;
        Blynk.virtualWrite(V1, Info.N.g_checked); // button off automatically 
        HeaterOff(0, "by time");              
        gab_minute = 0; 
     }
     return;
  }    
  if( Info.N.g_checked == false ) {       // Now Off & Start by temp     
     t_hour = hour();    
     for(int i=0; i<4; i++ ) {      
        if( i == 0 ) {
          if(( t_hour >= Info.T[0].g_start_hour ) || ( t_hour < Info.T[0].g_end_hour ) ) {
              led2.setColor(BLYNK_DARK_BLUE); // 22-04h temp mode
              g_temperature = Info.T[i].g_temperature;
              break;
          }          
        } else {  
          if(( Info.T[i].g_start_hour <= t_hour) && ( t_hour < Info.T[i].g_end_hour ) ) {
              switch(i) {
                case 1:
                          led2.setColor(BLYNK_BLUE); // 04-10h temp mode
                          break;
                case 2:
                          led2.setColor(BLYNK_GREEN); // 10-17h temp mode
                          break;
                case 3:
                          led2.setColor(BLYNK_YELLOW); // 17-22h temp mode
                          break;
              }
              g_temperature = Info.T[i].g_temperature;
              break;
          }          
        }
    }
    HeaterByTemperature();            
    led2.on(); // temp mode          
  }
}

void SendStatus()
{
 String strTime, strTemp;
 
  getTemperature();

  Blynk.virtualWrite(V0, TempC);  // Gadge 

// strTime = String(hour()) + ":" + String(minute()) + ":" + String(second()) + " " + dayShortStr(weekday()) + " " + String(day()) + "/" + monthShortStr(month()) + "/" + String(year());
 strTime = String(hour()) + ":" + String(minute()) + ":" + String(second()) + " " + String(day()) + "/" + monthShortStr(month());
 strTemp = String(TempC) + " : " + String(g_temperature);

 DisplayMessage(strTime.c_str(), strTemp.c_str(), strShortMessage.c_str());

}

// Adapted from Adafruit_SSD1306
void drawRect(void) {
  for (int16_t i = 0; i < display.getHeight() / 2; i += 2) {
    display.drawRect(i, i, display.getWidth() - 2 * i, display.getHeight() - 2 * i);
    display.display();
    delay(10);
  }
}

void HeaterOn(int Status, String shortMsg)
{
     led1.on();
     s_led_now  = Status;
     digitalWrite(relay_pin, RELAY_ON);
     
     strShortMessage = " ON:" + shortMsg;
     Serial.println("MSG\t"+strShortMessage );   
     DisplayAction(strShortMessage.c_str());    
}

void HeaterOff(int Status, String shortMsg)
{
     led1.off();
     s_led_now  = Status;
     digitalWrite(relay_pin, RELAY_OFF);
     
     strShortMessage = "OFF:" + shortMsg;
     Serial.println("MSG\t"+strShortMessage );
     DisplayAction(strShortMessage.c_str());           
}

void HeaterByTemperature(void)
{
  String strTemp;
  
  getTemperature();

  Blynk.virtualWrite(V0, TempC);  // Gadge 

  strTemp = String(g_temperature - TempC);
  
     if(  (g_temperature - TempC) > 0.7f  ) {
          HeaterOn(2,  strTemp+"by temp!");
     }    
     if(  TempC >= g_temperature  ) {
          HeaterOff(0, strTemp+"by temp!");
     }
}

void getTemperature(void)
{  

  sensors.requestTemperatures();

  // print the device information
  TempCI = printData(insideThermometer);
  TempCO = printData(outsideThermometer);
  TempC = TempCI; // (tempCI + tempCO) / 2 - 3.0f;

  Serial.print("Temperature : ");
  Serial.println(TempC);

  return;



  sensors.requestTemperatures();

  TempC = sensors.getTempCByIndex(0); // outside 

 // Check if reading was successful
  if(TempC != DEVICE_DISCONNECTED_C) 
  {
    Serial.print("Temperature : ");
    Serial.println(TempC);
  } 
  else
  {
    TempC = -0.0;
    Serial.println("Error: Could not read temperature data");
    DisplayMessage("Temp Control", "Error!", "can't read temp");    
  }

  TempF  = DallasTemperature::toFahrenheit(TempC);

}

void ReadAllFromEEPROM()
{
 int i;
byte value;

  // read the value to the appropriate byte of the EEPROM.
  for( i=0; i<sizeof(Info); i ++) {
    value = EEPROM.read(i);
    *(Info.W.SID + i) =  value;
    //delay(100);
  }
  
}

void WriteAllToEEPROM()
{
   int  i;
  byte value;

  // write the value to the appropriate byte of the EEPROM.
  for( i=0; i<sizeof(Info); i ++) {
    value = *(Info.W.SID + i);
    EEPROM.write(i, value);
    delay(100);
  }  

  EEPROM.commit();
}

void WriteToEEPROM(long taddr, int len)
{
  int addr, i;
  byte value;

  addr = taddr - long( &Info );  
  for( i=0; i<len; i ++) {
    value = *(Info.W.SID + addr + i);    
    EEPROM.write(addr + i, value);
    delay(100);
  }

  EEPROM.commit();
}

void SystemReset()
{
  memset(Info.W.SID, 0, sizeof(Info) );
  WriteAllToEEPROM();
  Serial.println("Reseted!");
}

void Provisioning() {
 // Serial.begin(115200);

    WiFi.begin();  // no SSID/PWD - get it from the Provisioning APP or from NVS (last successful connection)
    WiFi.onEvent(SysProvEvent);

  // BLE Provisioning using the ESP SoftAP Prov works fine for any BLE SoC, including ESP32, ESP32S3 and ESP32C3.
  #if CONFIG_BLUEDROID_ENABLED && !defined(USE_SOFT_AP)
    Serial.println("Begin Provisioning using BLE");
    // Sample uuid that user can pass during provisioning using BLE
    uint8_t uuid[16] = {0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf, 0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02};
    WiFiProv.beginProvision(
      NETWORK_PROV_SCHEME_BLE, NETWORK_PROV_SCHEME_HANDLER_FREE_BLE, NETWORK_PROV_SECURITY_1, pop, service_name, service_key, uuid, reset_provisioned
    );
    DisplayMessage("Temp Control", "Provision", "ble qr scan");
    Serial.println("ble qr");
    WiFiProv.printQR(service_name, pop, "ble");
  #else
    Serial.println("Begin Provisioning using Soft AP");
    WiFiProv.beginProvision(NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE, NETWORK_PROV_SECURITY_1, pop, service_name, service_key);
    DisplayMessage("Temp Control", "Provision", "wifi qr scan");
    Serial.println("wifi qr");
    WiFiProv.printQR(service_name, pop, "softap");
  #endif

}


void Scanning()
{
  
  Serial.println("Ready WiFi Scan!");
  delay(1000);
 
 // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();  
  delay(1000);

  #ifdef OLED
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.setFont(ArialMT_Plain_16);
    display.drawString(64, 0, "Start WiFi Scan!");
    display.display();
  #endif

  Serial.println("Start WiFi Scan!");
  // WiFi.scanNetworks will return the number of networks found
  int ScanListCount = WiFi.scanNetworks();
  if (ScanListCount == 0) {
    ScanResult = -1;
    Serial.println("No networks found!");
    display.drawString(64, 22, "No networks");
    display.display();
  } else {
    Serial.print(ScanListCount);
    Serial.println(" networks found!");
    display.drawString(64, 22, "Network founds");
    display.display();
    ScanResult = 0;
    for (int i = 0; i < ScanListCount; i++)
    {
      if( WiFi.SSID(i).compareTo(Info.W.SID) == 0 ) { 
        Serial.println("Matched!");
        display.drawString(64, 42, "Matched!");
        display.display();
        ScanResult   = 1;        
      }
      delay(10);
    }
  }
  WiFi.disconnect();

  if( ScanResult == 0 ) {
    SystemReset();
    Provisioning(); // no more matched ssid
  }

}

void setup(void)
{ 

#ifdef OLED
  display.init();

  display.setContrast(255);

  drawRect();
 // delay(1000);
  display.clear();
#endif

  Initialize(); 
  Scanning();

  switch( ScanResult ) {
       case 1: // Matched
            Blynk.begin(BLYNK_AUTH_TOKEN, Info.W.SID, Info.W.PWD);            
            // Success                        
            rtc.begin();
            // Setup a function to be called every timing            

            led1.setColor(BLYNK_RED);
            //digitalWrite(led_pin,   LED_ON);  
            DisplayMessage("Hello", "Temp Control", "Matched");
            ConnectResult = 1;

            timer.setInterval(10000L, SendStatus ); 
            timer.setInterval(60000L, CheckStatus );

            CheckStatus();
            SendStatus();

            break;
       case 0:  // Provisioning     
            DisplayMessage("Hello", "Temp Control", "Provision");
            ConnectResult = 0;
            break;
       case -1: // No networks
            DisplayMessage("Hello", "Temp Control", "No networks");
            ConnectResult = -1;
           // digitalWrite(led_pin,   LED_OFF);  
       default : 
          break;
  } 
}

void loop(void)
{

  if( ConnectResult == 1 ) {
     Blynk.run();
     timer.run();   
     button.tick();
  } 
  
}

// this function will be called when the button started long pressed.
void LongPressStart(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  Serial.println("\t - LongPressStart()");
}

// this function will be called when the button is released.
void LongPressStop(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  Serial.println("\t - LongPressStop()\n");
}

// this function will be called when the button is held down.
void DuringLongPress(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  Serial.println("\t - DuringLongPress()");
}
// this function will be called when the button is held down.
void ClickPress(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  Serial.println("\t - ClickPress()");
  if( s_display_sw == 0 ) s_display_sw == 1;
  else s_display_sw == 0;
}
// this function will be called when the button is held down.
void DoublePress(void *oneButton)
{
  Serial.print(((OneButton *)oneButton)->getPressedMs());
  Serial.println("\t - DoublePress()");
}

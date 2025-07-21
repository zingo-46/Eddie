/************************************************************

https://randomnerdtutorials.com/esp32-http-get-post-arduino/#http-post

https://electropeak.com/learn/interfacing-am2301-dht21-temperature-humidity-sensor-module-with-arduino/

https://github.com/letscontrolit/ESPEasy/issues/691

https://www.ardumotive.com/how-to-use-dht-21-sensor-en.html

https://chewett.co.uk/blog/1476/using-the-dht22-temperature-sensor-with-a-wemos-d1-mini-esp8266/

https://www.arduino.cc/reference/en/libraries/dht-sensor-library/

https://arduino-esp8266.readthedocs.io/en/2.6.1/esp8266wifi/client-examples.html

https://randomnerdtutorials.com/esp8266-ds18b20-temperature-sensor-web-server-with-arduino-ide/
https://randomnerdtutorials.com/guide-for-ds18b20-temperature-sensor-with-arduino/


Eddie
Eddie är den glättiga och optimistiska datorn på skeppet Hjärtat av Guld. Han är känd för sin extremt 
positiva attityd, vilket ofta irriterar besättningen. Eddie styr skeppets funktioner och kommunicerar 
med besättningen, men hans entusiastiska personlighet står ibland i kontrast till de farliga 
situationerna de hamnar i.

************************************************************/

/***********************************
Version history

241125 testdrift
250227 uppdaterat med tail-kommand
250529 förbättrad reboot, starta om DHT
250702 omstart om sändning misslyckas eller om servern inte går att nå
      vid komkoppling av landström kommer antagligen inte modemet igång lika
      snabbt och då misslyckas uppkopplingen. NumhttpErrors ska lösa det.
      Dubbel röd blink om den inte kommer utpå nätet.
      DHT strömförsörjs från D0, sätts låg innan reboot för att starta om DHT
      Det har varit problem med timeout, httpsettimeout är nu på 12 sek o då 
      verkar det funka. Omstart vid returkod -11 (timeout) verkar också funka
250705  timeout behöver vara upp mot 20 sek för att den inte ska boota om pga timeout
      varje hel timme pga långa svarstider då
      Av nån anledning läser den ibland in nollor som ow-id och det verkar inte räcka
      att boota om noden?
      Alla sensorer strömförsörjs av port D0 och nytt kommando reset startar om alla
      Påbörjat koll om ntpHour är lika som hostHour för att kunna rätta till daylightsavings

***********************************/

//#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <stdio.h>
#include <string.h>
#include <cstring> // För strstr()
#include <ArduinoJson.h>

const char* prgver = "1.21t";
char compiledDateTime[20];

/*******************************************
DHT temp-/hum sensor stuff
*******************************************/
const int DHTPIN = D1;
const int DHTTYPE = DHT22; // DHT22 or DHT11 
DHT dht(DHTPIN, DHTTYPE);
const int DHT_PWR_PIN = D0;  //pwrpin för DHT

/**********************************************
LED  RG-led & ow
***********************************************/
const int LED_PIN_RED = D3;
const int LED_PIN_GREEN = D2;
const int oneWireBus = D4;     // GPIO for DS18B20

/**********************************************
Dallas DS18B20 temp sensor
***********************************************/
#include <OneWire.h>
#include <DallasTemperature.h>

// Setup a oneWire instance 
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor 
DallasTemperature sensors(&oneWire);    //scan the bus for devices

// Number of temperature devices found
uint8_t numberOfDevices;
uint8_t InitialOw;  // number of detected ow-devices
uint8_t numberOfDHT = 0;  //default DHT-num

// We'll use this variable to store a found device address
DeviceAddress tempDeviceAddress; 

/*
uint8_t tempcorrDHT = 0;  // correction temp DHT
uint8_t tempcorr1 = 0;  // correction temp o/w 1
uint8_t tempcorr2 = 0;  // correction temp o/w 2
*/

/*******************************************
Server address
https://arcwind.se/platforms/work/v150/receiver.php?object=id01&location=v150&title=Motorrum&temp=15&maxtemp=80&mintemp=10&humid=50&maxhumid=60&minhumid=40

version2:
https://arcwind.se/platforms/work/v150/receiver.php?version=2&unit=2051537&sensor=28B1DF81E3583C76&temp=15 
https://arcwind.se/platforms/work/v150/receiver.php?version=2&unit=2051537&sensor=DHT&temp=15&humid=50

********************************************/
const char* host = "https://arcwind.se/platforms/work/v150/receiver.php?";
const char* notifyhost = "https://ntfy.sh/6558024_V150";;

/******************************************
WiFi
*******************************************/
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>   // https

// Define WiFi-details
struct WiFiCredentials {
  const char* ssid;
  const char* password;
};

// add SSID and passwd
WiFiCredentials wifiNetworks[] = {
  {"Jagaren", ""},
  {"V150", "armborst"},
  {"103", "abc12abc12345"}
};

// number of WiFi-networks
const int numNetworks = sizeof(wifiNetworks) / sizeof(WiFiCredentials);

//WiFiClient client;    //http
WiFiClientSecure client;    //https
HTTPClient http;
int httpCode = 0;
//uint httpTimeout = 12000;
uint httpTimeout = 20000;   // http response
String lastIpOctet = "";

// create Telnet-server
WiFiServer telnetServer(23);
WiFiClient telnetClient;

bool tail = false;  //tailing http-msgs to telnet

// NTP-settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600; //3600 is timezone in secs off UTC
uint daylightOffset_sec = 3600;

uint hostHour = 0;
uint ntpHour = 0;

// create a WiFiUDP instance
WiFiUDP ntpUDP;
// create a NTPClient instance
NTPClient timeClient(ntpUDP, ntpServer,gmtOffset_sec, 30*60000); // 60*60000 is update interval in ms

char chipIdStr[12]; 
char owaddrStr[20];

// TX-data to send
char TXData[80];
char notifyPrio[10];

// payloadbuffer
const int bufferSize = 1024; //RX-limits to 110 
char payload[bufferSize];

// delaytimer
const unsigned long txInterval = 60000; // 60 sec report interval
const unsigned long Ledinterval = 5000; // 5 sec blink rate

/*******************************************

Logging

*******************************************/
#define BUFFER_SIZE 20 // number of logentries
#define ALARM_LENGTH 30 // max lenght of each line
// alarmline   mmdd.hhmm:xxxxxxxxxxxxxx   24 chars + null
char alarmBuffer[BUFFER_SIZE][ALARM_LENGTH]; // Buffert för larm
int currentIndex = 0; // Pekar på var nästa larm ska sparas
int totalAlarms = 0; // Totalt antal lagrade larm

char dateStr[12];    // e.g. 1229:2345

uint8 consecErrors = 0; // consec DHT errors to stop alarms
const uint8 LimconsecErrors = 5;
uint8 httpErrors = 0; // consec http errors to reboot
const uint8 NumhttpErrors = 5; // consec http errors to reboot
uint8 httpFail = 0; // failflag

uint32_t httpStart = 0; // http timing
uint32_t httpStop = 0; // http timing

bool stopNotify = false;  //avoid sending notify every min
bool firstFlag = true; //flag for first attempt after restar

/*******************************************

Setup()

*******************************************/

void setup() {
  Serial.begin(115200);
  pinMode(oneWireBus, INPUT_PULLUP);
  pinMode(LED_PIN_RED, OUTPUT);
  pinMode(LED_PIN_GREEN, OUTPUT);
  pinMode(DHT_PWR_PIN, OUTPUT);
  digitalWrite(LED_PIN_RED, HIGH);  //red led on until wifi ok
  digitalWrite(LED_PIN_GREEN, LOW);
  digitalWrite(DHT_PWR_PIN, LOW);  //deactivate the DHT

    // Read chip-ID
  uint32_t chipId = ESP.getChipId();
  sprintf(chipIdStr, "%u", chipId); // Konvertera uint32_t till sträng

  // initiera temp/fukt-sensorn
  dht.begin();

  // starta wifi och logga in på routern
  setup_wifi() ;

  // Start NTP-client
  timeClient.begin();

  // Get current time
  timeClient.update();
  
  ntpHour = timeClient.getHours();

  digitalWrite(LED_PIN_RED, LOW);  //red led off - wifi ok
  digitalWrite(DHT_PWR_PIN, HIGH);  //activate sensors

  // greeting
  Serial.print("Greetings from Eddie! Starting...  ID: ");
  Serial.println(chipId);

  // display time
  String DispString = "Time is: "+timeClient.getFormattedTime();
  Serial.println(DispString);

  Serial.print("Program version:");
  Serial.println(prgver);
  
  // Start up the ow-library
  sensors.begin();
  InitDallas(); // locate devices on the bus and print addresses

  // send startmsg
  strcpy(TXData, "version=");
  strcat(TXData, prgver);
  strcat(TXData, "&unit=");
  strcat(TXData, chipIdStr);
  strcat(TXData, "&start=");
    
  if (strlen(TXData) + timeClient.getFormattedTime().length() < sizeof(TXData)) {
    strcat(TXData, timeClient.getFormattedTime().c_str());
  } else {
    //Serial.println("TXData too small for DispString - weird...");
  }

  strcat(TXData, "&ip=");
  strcat(TXData, lastIpOctet.c_str());

  //Serial.println (TXData);
  send2server(TXData);    // send startup message

  // Start Telnet-server
  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("\nTelnet-server started");

  //char compiledDateTime[20];
  snprintf(compiledDateTime, sizeof(compiledDateTime), "%04d-%02d-%02d %s", 
    atoi(&__DATE__[7]), // År
    monthToNumber(__DATE__), // Månad
    atoi(&__DATE__[4]), // Dag
    __TIME__); // Tid

  String almStr = "Restart ";
  almStr += timeClient.getFormattedTime();

  addAlarm(almStr);
  Serial.println(almStr);

  strcpy(TXData, "Restart - unit: ");
  strcat(TXData, chipIdStr);
  strcpy (notifyPrio, "high");
  sendNotify(TXData, notifyPrio);
} // setup()

int monthToNumber(const char* dateStr) {
  if (strncmp(dateStr, "Jan", 3) == 0) return 1;
  if (strncmp(dateStr, "Feb", 3) == 0) return 2;
  if (strncmp(dateStr, "Mar", 3) == 0) return 3;
  if (strncmp(dateStr, "Apr", 3) == 0) return 4;
  if (strncmp(dateStr, "May", 3) == 0) return 5;
  if (strncmp(dateStr, "Jun", 3) == 0) return 6;
  if (strncmp(dateStr, "Jul", 3) == 0) return 7;
  if (strncmp(dateStr, "Aug", 3) == 0) return 8;
  if (strncmp(dateStr, "Sep", 3) == 0) return 9;
  if (strncmp(dateStr, "Oct", 3) == 0) return 10;
  if (strncmp(dateStr, "Nov", 3) == 0) return 11;
  if (strncmp(dateStr, "Dec", 3) == 0) return 12;
  return 0; // Felhantering
}

/*******************************************

Loop()

*******************************************/

void loop() {
  static unsigned long prevMillis = 0;
  static unsigned long prevLedMillis = 0;
  bool txflag = false;

  if (millis() - prevLedMillis >= Ledinterval) {  // time for red blink
    prevLedMillis = millis();
    if (txflag == false){
      blink_red_led();
    }
  }

  if (millis() - prevMillis >= txInterval) {  // time to send update
    prevMillis = millis();
    txflag = true;  // tx in progress

    httpFail = false;
    readDHT_sensor();
    readOW_sensor();
    txflag = false;

    if (httpFail) {
      httpErrors++;
      if (tail){
      telnetClient.print("httpErrors: ");
      telnetClient.println(httpErrors);
      Serial.println(httpErrors);
    } 
          }
   // if sendtime
  } 

  {
    float temp = dht.readTemperature(); //check the sensor
    if (isnan(temp)){
      numberOfDHT = 0;
    } else {
      numberOfDHT = 1;
    }
  }

  if (firstFlag) {
    //Serial.println("firstflg");
    Serial.println(numberOfDHT);
    if ((numberOfDHT == 0) && (numberOfDevices == 0)){
      Serial.println(numberOfDHT);
      Serial.println("Nosens");
      strcpy(TXData, "Restart - unit: ");
      strcat(TXData, chipIdStr);
      strcat(TXData, "  inga sensorer!");
      strcpy (notifyPrio, "high");
      sendNotify(TXData, notifyPrio);
    }
    firstFlag = false;
  }

  check_telnet_conn();  //telnet client connect attempt?
  telnet_comm();  // get and execute telnet commands
} // loop

/****************************

  Telnet connections

****************************/

void check_telnet_conn(){
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      // accept new client
      telnetClient = telnetServer.accept();
      Serial.println("Telnet-client connected!");
      telnetClient.println("Welcome to Eddies Telnet-server!");

      String DispString = "Time is: "+timeClient.getFormattedTime()+'\0';
      logMessage(DispString);
      telnetClient.print("> ");

    } else {
      // max one client
      WiFiClient rejectClient = telnetServer.accept();
      rejectClient.stop();
    }
  }
}

 /****************************

  receive cmd from telnet

 ****************************/

void telnet_comm(){
  if (telnetClient && telnetClient.connected() && telnetClient.available()) {
    String command = telnetClient.readStringUntil('\n');
    command.trim();
    
    if (command[0] == 255 ){
      //Serial.println("junk");
    } else {
      Serial.println("Received command: " + command);
      handleCommand(command);
    }
  }
}

 /****************************

  execute telnet command

 ****************************/

void handleCommand(String command) {
  if (command == "status") {
    telnetClient.println("Systemstatus: All systems green!");

  } else if (command.startsWith("set ")) {
    String parameter = command.substring(4); // Extrahera parameter
    telnetClient.println("Settings changed to: " + parameter);
    // 
  } else if (command == "log") {
    telnetClient.println("Last log entries:");
      int start = (currentIndex + BUFFER_SIZE - totalAlarms) % BUFFER_SIZE;
      for (int i = 0; i < totalAlarms; i++) {
        int index = (start + i) % BUFFER_SIZE;
        telnetClient.println(alarmBuffer[index]);
      }
  } else if (command == "ver") {
    telnetClient.print("Program version:");
    telnetClient.println(prgver);
    telnetClient.println(compiledDateTime);
  } else if (command == "sens") {
    telnetClient.print("Number of DHT-sensors: ");
    telnetClient.print(numberOfDHT);
    telnetClient.print(", o/w sensors: ");
    telnetClient.println(numberOfDevices);

    for(int i=0;i<numberOfDevices; i++){
    // Search the wire for address
      if(sensors.getAddress(tempDeviceAddress, i)){
        addressToHex(tempDeviceAddress, owaddrStr);
        telnetClient.println(owaddrStr);
      }
    }
  } else if (command == "time") {
    String DispString = "Time is: "+timeClient.getFormattedTime()+'\0';
    telnetClient.println(DispString);
  } else if (command == "boot") {
    //ESP.restart();
    ESP.reset();
  } else if (command == "id") {
    telnetClient.print("Unit Id: ");
    telnetClient.println(chipIdStr);
  } else if (command == "blink") {
    blink_red_led();
    delay (500);
    blink_red_led();
    delay (500);
    blink_red_led();

    telnetClient.println(' ');
  } else if (command == "ip") {
    telnetClient.println(WiFi.localIP());
  } else if (command == "tail") {
    addAlarm("tail command");
    tail = !tail; //toggle tail setting
  } else if (command == "reset") {  // reset sensors
    digitalWrite(DHT_PWR_PIN, LOW);
    delay(1000); //delay slightly
    digitalWrite(DHT_PWR_PIN, HIGH);  //st5art sensors
    telnetClient.println("Sensors reset");
  } else if (command == "exit") {  // quit session
    String DispString = "telnet exit(): "+timeClient.getFormattedTime()+'\0';
    logMessage(DispString);
    addAlarm("telnet exit()");
    delay(100);
    telnetClient.stop();
  } else {
    telnetClient.println("Unknown cmd (log/ver/sens/time/boot/id/blink/ip/tail/reset/exit)");
  }
  telnetClient.print("> ");
}

 /****************************

  log message on telnet and serial

 ****************************/

void logMessage(String message) {
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(message);
  }
  Serial.println(message);
}

/****************************

  One Wire - Dallas

****************************/

void readOW_sensor(){
  noInterrupts(); // stop interrupts
  numberOfDevices = sensors.getDeviceCount();
  Serial.print("Antal ow: ");
  Serial.println(numberOfDevices, DEC);

  for(int i=0;i<numberOfDevices; i++){
    sensors.getAddress(tempDeviceAddress, i);
    addressToHex(tempDeviceAddress, owaddrStr);
    sensors.requestTemperatures();
    float temperatureC = sensors.getTempCByIndex(i);

    // check readings
    if (isnan(temperatureC) || temperatureC < -10) {
      String DispString = timeClient.getFormattedTime()+"  NaN value from 1w-sensor!"+'\0';
      logMessage(DispString);
      temperatureC = -99;
      consecErrors++;
      if (consecErrors < LimconsecErrors) {
        addAlarm("NaN from ow");
      }
    } 

    Serial.print(i);
    Serial.print(": ");
    Serial.print(temperatureC);
    Serial.println("ºC");

  // version 2 o/w-sensors

    strcpy(TXData, "version=");
    strcat(TXData, prgver);
    strcat(TXData, "&unit=");
    strcat(TXData, chipIdStr);
    strcat(TXData, "&sensor=");
    strcat(TXData, owaddrStr);
    
    strcat(TXData, "&temp=");
    //sprintf(TXData + strlen(TXData), "%d", int(temperatureC));
    sprintf(TXData + strlen(TXData), "%.1f", (temperatureC));

    strcat(TXData, "&ip=");
    strcat(TXData, lastIpOctet.c_str());
    //Serial.println (TXData);

    send2server(TXData);
  }
  interrupts(); // enable interrupts
}

 /****************************

    DHT - sensor

 ****************************/
void readDHT_sensor(){
  // Get temp and humid
  float humid = dht.readHumidity();
  float temp = dht.readTemperature();
  //temp = temp + tempcorrDHT;  // add correction if needed

  // check readings
  if (isnan(humid) || isnan(temp)) {

    humid = -99;
    temp = -99;
    
    consecErrors++;
    if (consecErrors < LimconsecErrors) {
      String almStr = "NaN from DHT ";
      almStr += timeClient.getFormattedTime();  // Lägg till tiden
      addAlarm(almStr);
      String DispString = timeClient.getFormattedTime()+"  NaN value from DHT-sensor!"+'\0';
      logMessage(DispString);
    } else {
      if (!stopNotify) {
        strcpy (notifyPrio, "high");
        strcpy (TXData, "NaN from DHT: ");
        strcat (TXData, chipIdStr);
        sendNotify(TXData, notifyPrio);
        stopNotify = true;
      }
      Serial.println("restart");
      digitalWrite(DHT_PWR_PIN, LOW);  //deactivate the DHT
      delay(1000); //delay slightly
      digitalWrite(DHT_PWR_PIN, HIGH);  //activate the DHT
    }
    numberOfDHT = 0;
  } else {
    numberOfDHT = 1;
    consecErrors = 0;
    stopNotify = false;

    Serial.print("DHT Temperatur: ");
    Serial.print(temp);
    Serial.println(" *C");

    Serial.print("DHT Fuktighet: ");
    Serial.print(humid);
    Serial.println(" %");

    // version 2  DHT

    strcpy(TXData, "version=");
    strcat(TXData, prgver);
    strcat(TXData, "&unit=");
    strcat(TXData, chipIdStr);
    strcat(TXData, "&sensor=DHT");
    strcat(TXData, "&temp=");
    //sprintf(TXData + strlen(TXData), "%d", int(temp));
    sprintf(TXData + strlen(TXData), "%.1f", (temp));
    strcat(TXData, "&humid=");
    //sprintf(TXData + strlen(TXData), "%d", int(humid));
    sprintf(TXData + strlen(TXData), "%.1f", (humid));
    
    strcat(TXData, "&ip=");
    strcat(TXData, lastIpOctet.c_str());

    send2server(TXData);
    //Serial.println(TXData);
  }
  if (tail){
    Serial.print("Consec:");
    Serial.println(consecErrors);
  }
}

//void sendNotification(const char* message) {
void sendNotify(char mess[], char prio[]){
  if (WiFi.status() == WL_CONNECTED) { // check connection

    if (tail){
      telnetClient.println("Notify sent:");
      telnetClient.println(mess);
      Serial.println(mess);
    }
  
    digitalWrite(LED_PIN_GREEN, HIGH);

    client.setInsecure();

    HTTPClient http;
    http.begin(client, notifyhost);
    http.addHeader("Content-Type", "text/plain");
    //http.addHeader("Priority", "high");
    http.addHeader("Priority", prio);

    int httpResponseCode = http.POST(mess);

//    Serial.print("HTTP Response code: ");
//    Serial.println(httpResponseCode);

    http.end();

    digitalWrite(LED_PIN_GREEN, LOW);

  } else {
        Serial.println("WiFi ej ansluten, kunde ej skicka notifikation.");
  }
}

/****************************

Update web db

****************************/
void send2server(char mess[]){
  if (WiFi.status() == WL_CONNECTED) { // check connection
    //Serial.println(mess);

    if (tail){
      telnetClient.println(mess);
      Serial.println(mess);
    }
  
    //HTTPClient http;
    client.setInsecure();
    HTTPClient http;

    http.setTimeout(httpTimeout); // vänta på responsekod fr servern
    if (http.begin(client, host)) {    // url to server dest
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");

      digitalWrite(LED_PIN_GREEN, HIGH);
      httpStart = millis(); // http timing

      httpCode = http.POST(mess); // send to server

      digitalWrite(LED_PIN_GREEN, LOW);
      httpStop = millis(); // http timing

      if (tail){
      //if (1){
        int txtime = httpStop - httpStart;
        telnetClient.println(httpCode);
        Serial.println(httpCode);
        telnetClient.print("TXtime: ");
        telnetClient.println(txtime/1000);
        Serial.print("TXtime: ");
        Serial.println(txtime/1000);
      }

      if (httpCode > 0) { 
        String payload = http.getString(); // Hämta hela svaret

        if (httpCode == 200){
          if (tail){
            telnetClient.println(payload);
            Serial.println(payload);
          }
          httpErrors = 0;

          // Skapa ett JsonDocument med dynamisk minnesallokering
          //StaticJsonDocument<1024> doc;  // Anpassa storleken efter din payload
          DynamicJsonDocument doc(1024);

          // Parsar payload direkt
          DeserializationError error = deserializeJson(doc, payload);
          if ((error) && (tail)){
            Serial.print(F("JSON-parsing misslyckades: "));
            Serial.println(error.c_str());
            return;
          }

          // Extrahera tidsfältet
          const char* timeStr = doc["data"]["time"];  // "250719 22:37:47"

          if (timeStr != nullptr) {
            // Rensa tiden från escape-tecken
            char cleanedTime[20];
            size_t j = 0;
            for (size_t i = 0; timeStr[i] != '\0'; i++) {
              if (timeStr[i] != '\\') {
                cleanedTime[j++] = timeStr[i];
              }
            }
            cleanedTime[j] = '\0';
            //Serial.println(cleanedTime);
            //Serial.println(timeStr);

            char formattedTime[15];

            // make condensed date and time  
            snprintf(formattedTime, sizeof(formattedTime), "%.2s%.2s:%.2s%.2s",
              cleanedTime,       // 09
              cleanedTime + 3,   // 12
              cleanedTime + 6,   // 23
              cleanedTime + 9);  // 08
            
            strcpy(dateStr, formattedTime);
            
            char hostHour[3];
            snprintf(hostHour, sizeof(formattedTime), "%.2s", cleanedTime + 6);   // 23
            if (tail){
              Serial.println(hostHour);
            }

            const char* hourMinute = timeStr + 7;      // pekar på "22:37:47"

            // Extrahera bara hh:mm
            char hhmm[6];
            strncpy(hhmm, hourMinute, 5);
            hhmm[5] = '\0';


            //strcpy(dateStr, cleanedTime);
            if (tail){
              Serial.print(F("Tid (hh:mm): "));
              Serial.println(hhmm);
            }
          }

          // Kolla om reboot=true finns
          if (doc["content"][0]["reboot"] == "1") {
            // Serial.println("boot");
            digitalWrite(DHT_PWR_PIN, LOW);  //reset the DHT
            delay(1000); //delay slightly
            ESP.reset();
          } else {
            // Serial.println("no boot");
          }
        }   // httpCode == 200
      } else {  // not 200
        if (tail){
          telnetClient.println(httpCode);
          Serial.println(httpCode);
        }
        //httpErrors ++;  // incr error counter
        httpFail = true;
        addAlarm("http error code");
      }
    }
    http.end(); 
  } else {
    blink_red_led();
    delay(100);
    blink_red_led();
    //httpErrors++;
    httpFail = true;
  }

  if (httpErrors > NumhttpErrors){
    if (tail){
            telnetClient.print("httpErrors: ");
            telnetClient.println(httpErrors);
            Serial.print("httpErrors: ");
            Serial.println(httpErrors);
          }

    digitalWrite(DHT_PWR_PIN, LOW);  //reset the DHT
    //Serial.println("reset http...");
     strcpy(TXData, "Httptimeout - unit: ");
    strcat(TXData, chipIdStr);
    strcpy (notifyPrio, "high");
    sendNotify(TXData, notifyPrio);
    delay(1000); //delay slightly
    ESP.reset();
  }
}

/****************************

  Connect to WiFi

****************************/

void setup_wifi() {
  for (int i = 0; i < numNetworks; i++) {
    Serial.print("Försöker ansluta till: ");
    Serial.println(wifiNetworks[i].ssid);

    WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      http.setTimeout(httpTimeout); // connection timeout
      //Serial.println("\nConnected");
      Serial.print("\nIP-adress: ");
//      Serial.println(WiFi.localIP());
      IPAddress ip = WiFi.localIP();
      Serial.print(ip);
      Serial.print("   (");
      Serial.print(ip[3]);
      Serial.println(")");
      lastIpOctet = String(WiFi.localIP()[3]);
      
      break;
    } else {
      Serial.println("\nFailed to connect");
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("No wifi available");
    addAlarm("No wifi.connection");
  }
}

 /****************************

    log HTTP-error

 ****************************/

void logHttpError(int httpCode, NTPClient& timeClient) {
  String logEntry = "[" + timeClient.getFormattedTime() + "] HTTP error: " + String(httpCode);
  logMessage(logEntry);
}

 /****************************

  Init Dallas ow-sensors

 ****************************/

void InitDallas() {
  // Grab a count of devices on the wire
  numberOfDevices = sensors.getDeviceCount();
  
  // locate devices on the bus
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" devices.");

  // Loop through each device, print out address
  for(int i=0;i<numberOfDevices; i++){
    // Search the wire for address
    if(sensors.getAddress(tempDeviceAddress, i)){
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: ");
      printAddress(tempDeviceAddress);
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");

      strcpy(TXData, "Ghost ow-device - unit: ");
      strcat(TXData, chipIdStr);
      strcpy (notifyPrio, "high");
      sendNotify(TXData, notifyPrio);
    }
  }
}

 /****************************

  Make ow device address printable

 ****************************/

void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++){
    if (deviceAddress[i] < 16) Serial.print("0");
      Serial.print(deviceAddress[i], HEX);
  }
}

void addressToHex(const uint8_t* addr, char* owaddrStr) {
    // convert each byte to two hex-chars
    for (int i = 0; i < 8; i++) {
        sprintf(owaddrStr + i * 2, "%02X", addr[i]);
    }
    owaddrStr[16] = '\0';
}

void blink_red_led(){
  digitalWrite(LED_PIN_RED, HIGH);
  delay(100);
  digitalWrite(LED_PIN_RED, LOW);
}

// add log entry
void addAlarm(const String& alarmMessage) {
  String logMessage = dateStr;
  logMessage += ":";
  logMessage += alarmMessage;
  //snprintf(logMessage, sizeof(logMessage), "%s:%s", dateStr, alarmMessage);
  
  Serial.print("Logg: ");
  Serial.println(logMessage);
  
  snprintf(alarmBuffer[currentIndex], ALARM_LENGTH, "%s", logMessage.c_str());
  currentIndex = (currentIndex + 1) % BUFFER_SIZE; // circular buffer
  if (totalAlarms < BUFFER_SIZE) {
    totalAlarms++;
  }
}

// show all alarms 
void showAlarms(Print& output) {
    int start = (currentIndex + BUFFER_SIZE - totalAlarms) % BUFFER_SIZE;
    for (int i = 0; i < totalAlarms; i++) {
        int index = (start + i) % BUFFER_SIZE;
        output.println(alarmBuffer[index]);
    }
}

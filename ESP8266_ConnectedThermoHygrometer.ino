/**
 * Connected Thermo-Hygrometer using an ESP8266 and a DHT22 sensor
 * 
 * Can post data to the OVH IOT PaaS (https://cloud.runabove.com/#/iot)
 * or to ThingSpeak: https://thingspeak.com/
 */

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
 
#include <WifiUdp.h>
#include <ESP8266HTTPClient.h>
#include <DHT.h>
//comment-out to disable DEBUG and Serial logging
#define DEBUG
//comment-out to disable wifi (usefull during dev of sensor code)
#define ENABLE_WIFI
//comment-out to enable sending metrics to OVH IOT PaaS (exclusive with thingspeak)
#define ENABLE_OVH_IOT_PAAS
//comment-out to enable sending metrics to thingspeak
//#define ENABLE_THINGSPEAK

//disable to disconnect from wifi between iterations of loop()
#define KEEP_WIFI_CONNECTION
//main loop delay in ms
#define LOOP_DELAY 300000

#define DHTTYPE DHT22
#define DHTPIN  2

//location sub-metric (e.g.: temperature.home)
#define LOCATION "home"
//wifi network name
const char* ssid = "MY_SSID";
//wifi network password
const char* password = "MY_WIFI_PASSWORD";

//OVH IoT PaaS credentials
const char* writeid = "writeIdForThisApp";
const char* key = "associatedWriteKey";

//thingspeak channel credentials
const char* thingspeak_key = "WriteKeyForThisChannel";


float temperature,humidity=0;  
String payload;
WiFiUDP udp;
ESP8266WiFiMulti WiFiMulti;
HTTPClient client;
DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266

/*
 * © Francesco Potortì 2013 - GPLv3 - Revision: 1.13
 *
 * Send an NTP packet and wait for the response, return the Unix time
 *
 * To lower the memory footprint, no buffers are allocated for sending
 * and receiving the NTP packets.  Four bytes of memory are allocated
 * for transmision, the rest is random garbage collected from the data
 * memory segment, and the received packet is read one byte at a time.
 * The Unix time is returned, that is, seconds from 1970-01-01T00:00.
 */
#ifdef ENABLE_OVH_IOT_PAAS
unsigned long  ntpUnixTime (UDP &udp)
{
  static int udpInited = udp.begin(123); // open socket on arbitrary port

  const char timeServer[] = "pool.ntp.org";  // NTP server

  // Only the first four bytes of an outgoing NTP packet need to be set
  // appropriately, the rest can be whatever.
  const long ntpFirstFourBytes = 0xEC0600E3; // NTP request header

  // Fail if WiFiUdp.begin() could not init a socket
  if (! udpInited)
    return 0;

  // Clear received data from possible stray received packets
  udp.flush();

  // Send an NTP request
  if (! (udp.beginPacket(timeServer, 123) // 123 is the NTP port
   && udp.write((byte *)&ntpFirstFourBytes, 48) == 48
   && udp.endPacket()))
    return 0;       // sending request failed

  // Wait for response; check every pollIntv ms up to maxPoll times
  const int pollIntv = 150;   // poll every this many ms
  const byte maxPoll = 15;    // poll up to this many times
  int pktLen;       // received packet length
  for (byte i=0; i<maxPoll; i++) {
    if ((pktLen = udp.parsePacket()) == 48)
      break;
    delay(pollIntv);
  }
  if (pktLen != 48)
    return 0;       // no correct packet received

  // Read and discard the first useless bytes
  // Set useless to 32 for speed; set to 40 for accuracy.
  const byte useless = 40;
  for (byte i = 0; i < useless; ++i)
    udp.read();

  // Read the integer part of sending time
  unsigned long time = udp.read();  // NTP time
  for (byte i = 1; i < 4; i++)
    time = time << 8 | udp.read();

  // Round to the nearest second if we want accuracy
  // The fractionary part is the next byte divided by 256: if it is
  // greater than 500ms we round to the next second; we also account
  // for an assumed network delay of 50ms, and (0.5-0.05)*256=115;
  // additionally, we account for how much we delayed reading the packet
  // since its arrival, which we assume on average to be pollIntv/2.
  time += (udp.read() > 115 - pollIntv/8);

  // Discard the rest of the packet
  udp.flush();
  udp.stop();

  return time - 2208988800ul;   // convert NTP time to Unix time
}

/*
 * Builds the OpenTSDB JSON payload
 */
void buildOpenTSDBPayLoad(float temp,float humidity,const char* location){
  unsigned long ts = 0;
#ifdef ENABLE_WIFI
  //get current timestamp via NTP
  ts = ntpUnixTime(udp);
#endif
  payload = "[{\"metric\":\"temperature.";
  payload += location;
  payload += "\",\"timestamp\":";
  payload.concat(ts);
  payload += ",";
  payload += "\"value\":";
  payload.concat(temp);
  payload += ",";
  payload += "\"tags\":{\"location\":\"";
  payload += location;
  payload += "\"}";
  payload += "}";

  payload += ",{\"metric\":\"humidity.";
  payload += location;
  payload += "\",\"timestamp\":";
  payload.concat(ts);
  payload += ",";
  payload += "\"value\":";
  payload.concat(humidity);
  payload += ",";
  payload += "\"tags\":{\"location\":\"";
  payload += location;
  payload += "\"}";
  payload += "}";
  payload += "]";

#ifdef DEBUG
  Serial.print("payload= ");
  Serial.println(payload);
#endif
}

/*
 * Post a REST resquest with JSON payload
 */
void postOpenTSDBData(const char* host,  const char* httpsFingerPrint, const char* writeid, const char*key, const String* payload){
  String url = "https://";
/*  url += writeid;
  url += ":";
  url += key;
  url += "@";
 */
  url += host;
  url += "/api/put";
#ifdef DEBUG
  Serial.print("connecting to ");
  Serial.println(url);
  Serial.print("writeid= ");
  Serial.print(writeid);
  Serial.print(", key= ");
  Serial.println(key);
#endif
  client.setAuthorization(writeid,key);
  int res = client.begin(url,httpsFingerPrint);
  if(res == false){
#ifdef DEBUG  
  Serial.println("http connection failed");
#endif
    return;
  }
  
  //int status = client.POST(*payload);
  int status = client.POST((uint8_t *) payload->c_str(), payload->length());
  client.end();
#ifdef DEBUG  
  Serial.print("status= ");
  Serial.println(status);
#endif
}
#endif

#ifdef ENABLE_THINGSPEAK
/*
 * Post a request to thingspeak
 */
void postData(const char* host,  const char* httpsFingerPrint, const char*key, const String* payload){
#ifdef DEBUG
  Serial.print("connecting to ");
  Serial.println(host);
#endif
  String url = "https://";
  url += host;
  url += "/update?api_key=";
  url += +key;
  url += "&";
  url.concat(*payload);
  int res = client.begin(url,httpsFingerPrint);
  if(res == false){
#ifdef DEBUG  
  Serial.print("http connection failed.");
#endif
    return;
  }
 
  int status = client.GET();
  client.end();
#ifdef DEBUG  
  Serial.print("status= ");
  Serial.println(status);
#endif
}

void buildPayLoad(float temp,float humidity,const char* location){
  payload = "field1=";
  payload += temp;
  payload += "&fiedl2=";
  payload += humidity;

  #ifdef DEBUG
  Serial.print("payload= ");
  Serial.println(payload);
#endif
}
#endif

void connectWifi(){
#ifdef DEBUG  
  Serial.print("Connecting to ");
  Serial.println(ssid);
#endif
  WiFiMulti.addAP(ssid,password);
  delay(500);
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
  }
#ifdef DEBUG
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif
}

void disconnectWifi(){
#ifdef DEBUG
  Serial.println("Disconnecting Wifi"); 
#endif
  WiFi.disconnect();
}




void readTempAndHumidity(){
   humidity = dht.readHumidity(); 
   temperature = dht.readTemperature(false);
}

void logValues()
{
#ifdef DEBUG
  Serial.print("Temperature= ");
  Serial.print(temperature);
  Serial.print(", Humidity= ");
  Serial.println(humidity);
#endif
}


void setup() {
#ifdef DEBUG  
  Serial.begin(9600);
  while (!Serial); // wait for serial attach
  delay(5000);
#endif
  
#ifdef ENABLE_WIFI
#ifdef KEEP_WIFI_CONNECTION  
  connectWifi();
#endif
#endif
  readTempAndHumidity();
}


void loop() {
#ifdef ENABLE_WIFI
#ifndef KEEP_WIFI_CONNECTION
  connectWifi();
#endif
#endif  
  readTempAndHumidity();
  logValues();  
#ifdef ENABLE_WIFI
#ifdef ENABLE_OVH_IOT_PAAS
  buildOpenTSDBPayLoad(temperature,humidity,LOCATION);
  postOpenTSDBData("opentsdb.iot.runabove.io","50 d2 67 15 6c 86 28 43 6e 27 f1 c7 9f 60 7d e3 be 60 9a 9d",writeid,key, &payload);
#endif
#ifdef ENABLE_THINGSPEAK
 buildPayLoad(temperature,humidity,LOCATION);
 postData("api.thingspeak.com","78 60 18 44 81 35 bf df 77 84 d4 0a 22 0d 9b 4e 6c dc 57 2c",thingspeak_key,&payload);
#endif
#ifndef KEEP_WIFI_CONNECTION
  disconnectWifi();
#endif
#endif
  
 // delay(LOOP_DELAY); //no longer used since we use deepSleep() instead
 
 // now using deepSleep (with required wiring) to save energy
  ESP.deepSleep(LOOP_DELAY * 1000, WAKE_RF_DEFAULT);
}




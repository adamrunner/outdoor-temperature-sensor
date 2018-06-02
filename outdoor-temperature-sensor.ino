/*
* MQTT Client
* Reads temp from a DS18B20 Sensor
* Sends the temp to MQTT Topic
* Deep Sleeps until the RTC wakes it up (5 min)
*/

#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WifiCreds.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "MAX1704.h"


#define ONE_WIRE_BUS D3
#define TIME_TO_SLEEP 5 * 60 * 1000 * 1000 // 300,000,000 µs 5m
#define TEMP_SERVER "temp.adamrunner.com"
#define MQTT_PORT 1883
#define MAX_AWAKE_MS 10 * 1500 // 15,000ms 15s
#define TEMP_MESSAGE_INTERVAL_MS 10 * 1000 // 10,000ms 10s
#define TEMP_READ_INTERVAL_MS 2000 // 2s
uint8_t MAC_array[6];
char MAC_char[18];

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
WiFiClient espClient;
PubSubClient client(espClient);
MAX1704 fuelGauge;
unsigned long lastTempMessageSentAt = 0;
unsigned long lastTempReadAt = 0;
char  currentHostname[14];
bool result = false;

void setup() {
  Wire.begin();
  fuelGauge.reset();
  fuelGauge.quickStart();
  // set D0 to INPUT_PULLUP for deepSleep
  pinMode(D0, INPUT_PULLUP);
  Serial.begin(115200);
  setupWifi();
  client.setServer(TEMP_SERVER, MQTT_PORT);
  // sendDiagMessage("ready - online");
  // TODO: check for firmware update?!
  // use Arduino OTA library
}

void setupWifi() {

  delay(10);

  Serial.println();
  Serial.print("Connecting to ");
  WiFi.mode(WIFI_STA);
  Serial.println(MY_SSID);
  WiFi.begin(MY_SSID, MY_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // Store the hostname to use later for MQTT ID
  String hostnameString = WiFi.hostname();
  hostnameString.toCharArray(currentHostname, 14);
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(currentHostname)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      // TODO: print out actual message constants from PubSubClient.h
      // Wait 2 seconds before retrying
      delay(2000);
    }
  }
  client.loop();
}

bool sendDiagMessage(char* message){
  char topic[26];
  sprintf(topic, "debugger-%s", currentHostname);
  bool result = sendMessage(topic, message);
  return result;
}

bool sendTemperatureMessage(float temperatureFloat) {
  Serial.println("sendTemperatureMessage called");
  char temperatureString[7];
  // dtostrf(FLOAT,WIDTH,PRECSISION,BUFFER);
  dtostrf(temperatureFloat,4,2,temperatureString);
  char  message[17];
  sprintf(message, "%s,%s", currentHostname, temperatureString);
  bool result = sendMessage("outTopic", message);
  return result;
}

bool sendMessage(char* topic, char* message){
  reconnect();
  client.loop();

  serialLogMessage(topic, message);

  bool result = client.publish(topic, message);
  Serial.print("result: ");
  Serial.println(result);
  client.loop();
  return result;
}

bool sendMessage_v2(char* hostname, char* temperature, char* battery, char* voltage)
{

  char message[48];
  sprintf(message, "HOSTNAME:%s,TEMP:%s,BATTERY:%s,VOLTAGE:%s", hostname, temperature, battery, voltage);
  serialLogMessage("data", message);
  bool result = sendMessage("data", message);
  return result;
}

void serialLogMessage(char* topic, char* message){
  Serial.println("sending..");
  Serial.print("topic: ");
  Serial.println(topic);
  Serial.print("message: ");
  Serial.println(message);
}

float getTemp(){
  float temp;
  Serial.println("getTemp called");
  sensors.requestTemperatures();
  temp = sensors.getTempFByIndex(0);
  serialLogTemp(temp);
  lastTempReadAt = millis();
  return temp;
}

void serialLogTemp(int temperature){
  Serial.print("Current Temp: ");
  Serial.print(temperature);
  Serial.println("ºF");
}

void goToSleep(int lengthOfNap) {
  char diagMessage[36];
  sprintf(diagMessage, "sleeping for: %i µs", lengthOfNap);
  sendDiagMessage(diagMessage);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep(lengthOfNap);
}

bool invalidTempReading(float temp){
  bool result = (temp < -20.0 || temp > 120.0 || temp == 0.000 );
  return result;
}

void loop(){
  float temp = 0.000;
  float batt = 0.000;
  float volt = 0.000;
  char battery[10];
  char temperature[10];
  char voltage[10];
  client.loop();

  while(invalidTempReading(temp) && millis() > (lastTempReadAt + TEMP_READ_INTERVAL_MS) ){
    // read the temperature from the sensor constantly
    // if there isnt a good reading
    temp = getTemp();
    batt = fuelGauge.stateOfCharge();
    volt = fuelGauge.getVoltage();

    dtostrf(temp,4,2,temperature);
    dtostrf(batt,4,2,battery);
    dtostrf(volt,4,2,voltage);

    Serial.print("Temperature: ");
    Serial.println(temperature);
    Serial.print("Battery: ");
    Serial.println(battery);
    Serial.print("Voltage: ");
    Serial.println(voltage);

  }
  // if we haven't sent a successful update
  // if it's time to send an update and we've got a valid temperature
  // send the update
  if(!result) {
    if(millis() > (lastTempMessageSentAt + TEMP_MESSAGE_INTERVAL_MS) && !invalidTempReading(temp) ){


      sendMessage_v2(currentHostname, temperature, battery, voltage);
      result = sendTemperatureMessage(temp);
      // continue with v1
      if(result){
        lastTempMessageSentAt = millis();
      }
      delay(1500);
    }
  }


  if(result && (millis() > MAX_AWAKE_MS)){

    Serial.println("awake too long!");
    Serial.print("ms :");
    Serial.println(millis());
    goToSleep(TIME_TO_SLEEP);
  }
}
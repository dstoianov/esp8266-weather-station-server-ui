


// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>
#include <math.h>
#include <FS.h>
#include <DHT.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <CircularBuffer.h>

#define DHT_TYPE DHT22  // DHT 22  (AM2302), AM2321
#define DHT_PIN 4       // D2  what digital pin we're connected to

//http://www.weather-forecast.com/weather-stations/Berlin-Tegel-Airport
#define SEALEVELPRESSURE_HPA (1016)

// Data wire is plugged into port 2 on the Arduino
#define ONE_WIRE_BUS 14  //D5

#define CONTENT_TYPE_TEXT_PLAIN "text/plain"
#define CONTENT_TYPE_TEXT_HTML "text/html"
#define CONTENT_TYPE_JSON "application/json"


const char *build_version_str = "Version 1.0, " __DATE__ ", " __TIME__;


// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);

// arrays to hold device address
DeviceAddress insideThermometer;

Adafruit_BME280 bme; // I2C

DHT dht(DHT_PIN, DHT_TYPE);

// Set up ESP8266 ADC for voltage read
ADC_MODE(ADC_VCC);


WiFiUDP ntpUDP;
//NTPClient timeClient(ntpUDP);
NTPClient timeClient(ntpUDP, "de.pool.ntp.org", 3600, 60 * 60000);
String last_measure_time;

//const char *ssid = "FRITZ";
//const char *password = "32570220417897809444";

const char* ssid = "kiwi Guest";
const char* password = "Opening Doors";

struct sensorMeasure {
  char *name;
  float temp;
  float humidity;
  float pressure;
  float pressure_2;
  float altitude;
};

struct record_log {
  float temp;
  float hum;
  float pres;
  float pres_2;
};

const byte bufSize = 100; // store 24*60/15=96 values for weeks and months
CircularBuffer <record_log, bufSize> day_log_buffer;

ESP8266WebServer server(80);

const int led = 13;

unsigned long last_time = 0;

const int interval_min = 15; // collect data every 15 minutes

void setup(void) {

  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.softAPdisconnect();
  WiFi.disconnect();
  WiFi.mode(WIFI_STA); // client mode
  delay(100);


  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  delay(100);

  Serial.println("Sensors Test..");

  //for BME280 D4,D3  -- I2C
  Wire.begin(0, 2); // GPIO0, GPIO2

  if (!bme.begin()) {
    Serial.println("Could not find a valid BME280 sensor, Check your wiring or I2C ADDR!");
    while (1);
  } else {
    Serial.println("BME280 ready!");
  }

  dht.begin();
  Serial.println("DHT22 ready!");

  // locate devices on the bus
  Serial.println("Locating DS18B20 devices...");
  sensors.begin();
  Serial.println("Found '" + String(sensors.getDeviceCount(), DEC) + "' devices.");

  if (!sensors.getAddress(insideThermometer, 0)) {
    Serial.println("Unable to find DS18B20 sensor address for Device 0");
  } else {
    Serial.println("DS18B20 ready!");
  }


  // show the addresses we found on the bus
  Serial.print("Device 0 Address: ");
  printAddress(insideThermometer);
  Serial.println();

  // set the resolution to 11 bit (Each Dallas/Maxim device is capable of several different resolutions)
  sensors.setResolution(insideThermometer, 11);
  Serial.println("Device 0 resolution: '" + String(sensors.getResolution(insideThermometer), DEC) + "' bit");
  // ========end of sensors================

  connectWiFi();
  routes();

  Serial.println("\nFiles on ESP8266");
  SPIFFS.begin();
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    String fileName = dir.fileName();
    size_t fileSize = dir.fileSize();
    Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
  }

  server.begin();
  Serial.println("HTTP server started");

  timeClient.begin();
}


void loop() {
  server.handleClient();

  if (day_log_buffer.size() == 0) {
    do_measure();
  }

  if (millis() - last_time > interval_min * 60 * 1000) {
    do_measure();
    last_time = millis();
  }
}


void do_measure() {
  sensorMeasure bm = readBMEValues();
  record_log new_record = { bm.temp, bm.humidity, bm.pressure, bm.pressure_2 };
  day_log_buffer.push(new_record);
  Serial.println("Size of day_log buffer.. " + String(day_log_buffer.size()));
  timeClient.update();
  last_measure_time = String(timeClient.getHours() + 1) + ":" + String(timeClient.getMinutes());
  Serial.println("Last mesure time '" + last_measure_time + "'");
}


void connectWiFi() {
  Serial.println();
  Serial.println(F("Starting Wi-Fi.."));
  WiFi.hostname("ESP-Station"); // change name for router detection
  WiFi.begin();
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println();
  Serial.println("Connected to '" + String(ssid) + "'");
  Serial.println("IP address: '" + WiFi.localIP().toString() + "'");

  if (MDNS.begin("esp8266")) {
    Serial.println(F("mDNS responder started"));
  }
}


struct sensorMeasure readBMEValues() {
  sensorMeasure data;

  data.name = (char *) "BME280";
  data.temp = bme.readTemperature();
  data.humidity = round(bme.readHumidity());
  float pressure_tmp = bme.readPressure();
  data.pressure = round(pressure_tmp / 100.0F);
  data.pressure_2 = round(pressure_tmp / 133.33F);
  data.altitude = bme.readAltitude(SEALEVELPRESSURE_HPA);

  // Check if any reads failed and exit early (to try again).
  if (isnan(data.humidity) || isnan(data.temp) || isnan(data.pressure)) {
    Serial.println(F("Failed to read from BME280 sensor!"));
  }

  Serial.println(F("Requesting BME280 temperatures... "));
  Serial.println("Temperature: " + String(data.temp) + " *C");
  Serial.println("Humidity:    " + String(data.humidity) + " %");
  Serial.println("Pressure:    " + String(data.pressure) + " hPa");
  Serial.println("Pressure:    " + String(data.pressure_2) + " mm");
  //  Serial.println("Approx. Altitude: " + String(data.altitude) + " m");
  Serial.println();
  return data;
}

struct sensorMeasure readDS18b20Values() {
  sensorMeasure data;

  Serial.print(F("Requesting DS18B20 temperatures... "));
  sensors.requestTemperatures(); // Send the command to get temperatures
  Serial.println(F("DONE"));

  data.name = (char *) "DS18B20";
  data.temp = sensors.getTempCByIndex(0);

  // Check if any reads failed and exit early (to try again).
  if (isnan(data.temp)) {
    Serial.println(F("Failed to read from DS18B20 sensor!"));
  }

  Serial.println("Temperature: " + String(data.temp) + " *C");
  return data;
}

struct sensorMeasure readDHT22Values() {
  sensorMeasure data;
  delay(300);
  Serial.print(F("Requesting DHT22 temperatures... "));
  data.name = (char *) "DHT22/AM2302";
  data.humidity = round(dht.readHumidity());
  data.temp = dht.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(data.humidity) || isnan(data.temp)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    delay(1000);
    data.humidity = round(dht.readHumidity());
    data.temp = dht.readTemperature();
  }
  Serial.println(F("DONE"));
  Serial.println("Temperature: " + String(data.temp) + " *C ");
  Serial.println("Humidity: " + String(data.humidity) + " %");

  return data;
}

//void printVccValues() {
//  float vcc = ESP.getVcc();
//  Serial.println();
//  Serial.print("Voltage: ");
//  Serial.print(vcc / 1000);
//  Serial.println(" V");
//}

//float round_up(float f) {
//  float result;
//  result = ceilf(f * 100) / 100;
//  Serial.println("Round " + String(f, 4) + "->" + String(result, 4));
//  return result;
//}
//===============HTTP Server================================

void routes() {
  server.on("/", handleRoot);
  server.on("/info", handleInfo);
  server.on("/sensors", handleSensors);
  server.on("/day", handleDay);
  //  server.on("/settings", handleSettings); //to be developed
  //    server.onNotFound(handleNotFound);

  server.onNotFound([]() {
    if (!handleFileRead(server.uri()))
      handleNotFound();
  });
}

void handleSensors() {
  sensorMeasure dh = readDHT22Values();
  sensorMeasure ds = readDS18b20Values();
  sensorMeasure bm = readBMEValues();

  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();
  root["success"] = "OK";
  root["uptime"] = millis();
  JsonObject &sensors = root.createNestedObject("sensors");
  JsonObject &sensor1 = sensors.createNestedObject("ds18b20");
  sensor1["name"] = ds.name;
  sensor1["temp"] = ds.temp;
  JsonObject &sensor2 = sensors.createNestedObject("bme280");
  sensor2["name"] = bm.name;
  sensor2["humidity"] = bm.humidity;
  sensor2["temp"] = bm.temp;
  sensor2["pressure"] = bm.pressure;
  sensor2["pressure_2"] = bm.pressure_2;
  sensor2["altitude"] = bm.altitude;
  JsonObject &sensor3 = sensors.createNestedObject("dht22");
  sensor3["name"] = dh.name;
  sensor3["humidity"] = dh.humidity;
  sensor3["temp"] = dh.temp;

  if (isnan(dh.temp)) {
    sendError();
  }


  char buffer[512];
  root.printTo(buffer, sizeof(buffer));
  //    root.prettyPrintTo(Serial);
  setHeader();
  server.send(200, CONTENT_TYPE_JSON, buffer);
}


void handleDay() {
  DynamicJsonBuffer jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();

  root["success"] = "OK";
  root["uptime"] = millis();
  root["total_records"] = day_log_buffer.size();
  root["vcc"] = (float)ESP.getVcc() / 1024.0f;
  root["last_measure_time"] = last_measure_time;

  JsonArray &records = root.createNestedArray("records");

  uint8_t i = 0;
  while (i < day_log_buffer.size()) {
    JsonObject &record = records.createNestedObject();
    record["t"] = round(day_log_buffer[i].temp * 10); // on a front-end side need to divide by temp/10
    record["h"] = day_log_buffer[i].hum;
    //    record["p"] = day_log_buffer[i].pres;
    record["p"] = round(day_log_buffer[i].pres_2 * 10);
    //    record["pres_2"] = day_log_buffer[i].pres_2;
    i++;
    //    records.add(record); //do not need it, it creates dubs
  }

  Serial.println("Total records are " + String(records.size()));

  char buffer[2900];
  root.printTo(buffer, sizeof(buffer));
  //  root.prettyPrintTo(Serial);
  setHeader();
  server.send(200, CONTENT_TYPE_JSON, buffer);
}


void setHeader() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("user-agent", "esp8266");
}


void handleInfo() {
  StaticJsonBuffer<1024> jsonBuffer;
  JsonObject &root = jsonBuffer.createObject();

  root["success"] = "OK";
  root["uptime"] = millis(); // get the current milliseconds from arduino
  root["build_vesrion"] = build_version_str;

  JsonObject &network = root.createNestedObject("network");
  network["ssid"] = ssid;
  network["signal"] = String(WiFi.RSSI()) + "dBm";
  network["ip"] = WiFi.localIP().toString();
  network["mac"] = WiFi.BSSIDstr();

  JsonObject &esp = root.createNestedObject("esp");
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  esp["boot_mode"] = ESP.getBootMode();
  esp["boot_version"] = ESP.getBootVersion();
  esp["sdk_version"] = String(ESP.getSdkVersion());
  esp["chip_id"] = ESP.getChipId();
  esp["flash_id"] = ESP.getFlashChipId();
  esp["free_heap"] = ESP.getFreeHeap();
  esp["chip_size"] = ideSize;
  esp["real_size"] = realSize;
  esp["flash_frequency"] = ESP.getFlashChipSpeed();
  esp["flash_mode"] = (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" :
                       ideMode == FM_DOUT ? "DOUT"
                       : "UNKNOWN");

  if (ideSize != realSize) {
    esp["message"] = "Flash Chip configuration wrong!";
  } else {
    esp["message"] = "Flash Chip configuration OK";
  }

  char buffer[1024];
  root.printTo(buffer, sizeof(buffer));
  //    root.prettyPrintTo(Serial);
  setHeader();
  server.send(200, CONTENT_TYPE_JSON, buffer);
}

void handleRoot() {
  if (!handleFileRead("/"))
    handleNotFound();
  digitalWrite(led, 0);
}

//void handleRoot() {
//    digitalWrite(led, 1);
//    dht22 dh = readDHT22Values();
//    ds18b20 sens = readDS18b20Values();
//    bme280 bm = readBMEValues();
//
//    String html;
//    html = "<h1>hello from esp8266!</h2>"
//                   "<br> <h2>Uptime " + String(millis()) + "</h2>" +
//           "<br> DS18B20 temp " + String(sens.temp) + " C" +
//           "<br><br> DHT22 temp " + String(dh.temp) + " C" +
//           "<br> DHT22 humidity " + String(dh.humidity) + " %" +
//           "<br><br> BME280 temp " + String(bm.temp) + " C" +
//           "<br> BME280 humidity " + String(bm.humidity) + " %" +
//           "<br> BME280 pressure " + String(bm.pressure) + " hPa" +
//           "<br> BME280 pressure " + String(bm.pressure_2) + " mm";
//
//    server.send(200, CONTENT_TYPE_TEXT_HTML, html);
//    digitalWrite(led, 0);
//}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "Page Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  setHeader();
  server.send(404, CONTENT_TYPE_TEXT_PLAIN, message);
  digitalWrite(led, 0);
}

void sendError() {
  setHeader();
  server.send(404, CONTENT_TYPE_JSON, "{\"success\":\"false\"}");
}


//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return CONTENT_TYPE_TEXT_PLAIN;
}


bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// function to get a device address
String getSensorAddress(DeviceAddress deviceAddress) {
  String out;
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) {
      out += "0";
    }
    out += String(deviceAddress[i], HEX);
  }
  out.toUpperCase();
  //  Serial.println("Converted adderess.. " + out);
  return out;
}

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}


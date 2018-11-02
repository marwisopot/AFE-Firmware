/*
AFE Firmware for smarthome devices based on ESP8266/ESP8285 chips

This code combains AFE Firmware versions:
   - T0 and T0 for Shelly-1
   - T1 (DS18B29)
   - T2 (DHTxx)
   - T4 (Up to 4 relays)

More about the versions (PL): https://www.smartnydom.pl/afe-firmware-pl/wersje/
LICENSE: https://github.com/tschaban/AFE-Firmware/blob/master/LICENSE
DOC (PL): https://www.smartnydom.pl/afe-firmware-pl/
*/

/* Includes libraries for debugging in development compilation only */
#if defined(DEBUG)
#include <Streaming.h>
#endif

#include <AFE-API-Domoticz.h>
#include <AFE-API-MQTT.h>
#include <AFE-Data-Access.h>
#include <AFE-Device.h>

/* Shelly-1 device does not have LED. Excluding LED related code */
#if !defined(T0_SHELLY_1_CONFIG)
#include <AFE-LED.h>
AFELED Led;
#endif

#include <AFE-Relay.h>
#include <AFE-Switch.h>
#include <AFE-Upgrader.h>
#include <AFE-Web-Server.h>
#include <AFE-WiFi.h>

/* T1 Set up, DS18B20 sensor */
#if defined(T1_CONFIG)
#include <AFE-Sensor-DS18B20.h>
AFESensorDS18B20 Sensor;
#endif

/* T1 and T2 Set up */
#if defined(T1_CONFIG) || defined(T2_CONFIG)
float temperature;
#endif

/* T2 Setup, DHxx sensor */
#if defined(T2_CONFIG)
#include <AFE-Sensor-DHT.h>
#include <PietteTech_DHT.h>
void dht_wrapper();
PietteTech_DHT dht;
AFESensorDHT Sensor;
float humidity;
#endif

AFEDataAccess Data;
AFEDevice Device;
AFEWiFi Network;
AFEMQTT Mqtt;
AFEDomoticz Domoticz;
AFEWebServer WebServer;
AFESwitch Switch[sizeof(Device.configuration.isSwitch)];
AFERelay Relay[sizeof(Device.configuration.isRelay)];
MQTT MQTTConfiguration;

void setup() {

  Serial.begin(115200);
  delay(10);

/* Turn off publishing information to Serial for production compilation */
#if !defined(DEBUG)
  Serial.swap();
#endif

  /* Checking if the device is launched for a first time. If so it loades
   * default configuration to EEPROM */
  if (Device.isFirstTimeLaunch()) {
    Device.setDevice();
  }

  /* Checking if the firmware has been upgraded. Potentially runs post upgrade
   * code */
  AFEUpgrader Upgrader;
  if (Upgrader.upgraded()) {
    Upgrader.upgrade();
  }
  Upgrader = {};

  /* Checking if WiFi is configured, if not then it runs configuration panel in
   * access point mode */
  if (Device.getMode() != MODE_ACCESS_POINT && !Device.isConfigured()) {
    Device.reboot(MODE_ACCESS_POINT);
  }

  /* Initializing relay */
  initRelay();

  /* Initialzing network */
  Network.begin(Device.getMode());

  /* Initializing LED, checking if LED exists is made on Class level  */
#if !defined(T0_SHELLY_1_CONFIG)
  uint8_t systeLedID = Data.getSystemLedID();
  if (systeLedID > 0) {
    Led.begin(systeLedID - 1);
  }

  /* If device in configuration mode then it starts LED blinking */
  if (Device.getMode() == MODE_ACCESS_POINT) {
    Led.blinkingOn(100);
  }
#endif

  Network.listener();
  /* Initializing HTTP WebServer */
  WebServer.handle("/", handleHTTPRequests);
  WebServer.handle("/favicon.ico", handleFavicon);
  WebServer.begin();

  /* Initializing switches */
  initSwitch();

  /* Initializing DS18B20 pr DHTxx sensor */
#if defined(T1_CONFIG) || defined(T2_CONFIG)
  initSensor();
#endif

  /* Initializing APIs */
  MQTTInit();
  DomoticzInit();
}

void loop() {

  if (Device.getMode() != MODE_ACCESS_POINT) {
    if (Network.connected()) {
      if (Device.getMode() == MODE_NORMAL) {

        /* Triggerd when connectes/reconnects to WiFi */
        eventsListener();

        /* If MQTT API is on it listens for MQTT messages. If the device is not
         * connected to MQTT Broker, it connects the device to it */
        if (Device.configuration.mqttAPI) {
          Mqtt.listener();
        }

        /* Listens for HTTP requsts. Both for configuration panel HTTP requests
         * or HTTP API requests if it's turned on */
        WebServer.listener();

        /* Checking if there was received HTTP API Command */
        mainHTTPRequestsHandler();

        /* Relay related code */
        mainRelay();

        /* Sensor: DS18B20 or DHT related code */
#if defined(T1_CONFIG) || defined(T2_CONFIG)
        mainSensor();
#endif

      } else { /* Device runs in configuration mode over WiFi */
#if !defined(T0_SHELLY_1_CONFIG)
        if (!Led.isBlinking()) {
          Led.blinkingOn(100);
        }
#endif
        WebServer.listener();
      }
    }

#if !defined(T0_SHELLY_1_CONFIG)
    else {
      if (Device.getMode() == MODE_CONFIGURATION && Led.isBlinking()) {
        Led.blinkingOff();
      }
    }
#endif
    Network.listener();
  } else { /* Deviced runs in Access Point mode */
    Network.APListener();
    WebServer.listener();
  }

  /* Listens for switch events */
  mainSwitchListener();
  mainSwitch();

  /* Led listener */
#if !defined(T0_SHELLY_1_CONFIG)
  Led.loop();
#endif
}

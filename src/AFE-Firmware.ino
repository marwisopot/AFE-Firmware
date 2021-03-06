/*
AFE Firmware for smarthome devices based on ESP8266/ESP8285 chips

This code combains AFE Firmware versions:
   - T0 and T0 for Shelly-1
   - T1 (DS18B20)
   - T2 (DHTxx)
   - T3 (PIRs)
   - T4 (Up to 4 relays)
   - T5 Gate
   - T6 Wheater station

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
#if defined(T1_CONFIG) || defined(T2_CONFIG) || defined(T5_CONFIG)
float temperature;
#endif

/* T2 Setup, DHxx sensor */
#if defined(T2_CONFIG) || defined(T5_CONFIG)
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

#if defined(T3_CONFIG)
#include <AFE-PIR.h>
AFEPIR Pir[sizeof(Device.configuration.isPIR)];
#endif

#if defined(T5_CONFIG)
#include <AFE-Gate.h>
GATE GateState;
AFEGate Gate;
uint8_t lastPublishedGateStatus = GATE_UNKNOWN;
byte lastPublishedContactronState[sizeof(Device.configuration.isContactron)];
#endif

#if defined(T6_CONFIG)
#include <AFE-Sensor-BME680.h>
#include <AFE-Sensor-HPMA115S0.h>
//#include <Wire.h>
#include <AFE-Sensor-BH1750.h>
AFESensorHPMA115S0 ParticleSensor;
AFESensorBME680 BME680Sensor;
AFESensorBH1750 BH1750Sensor;
#endif

void setup() {

  Serial.begin(9600);
  delay(10);

/* Turn off publishing information to Serial for production compilation */
#if !defined(DEBUG)
  Serial.swap();
#endif

#ifdef DEBUG
  Serial << endl
         << endl
         << "################################ BOOTING "
            "################################"
         << endl
         << "All classes and global variables initialized";
#endif

  /* Checking if the device is launched for a first time. If so it loades
   * default configuration to EEPROM */
  if (Device.isFirstTimeLaunch()) {
    Device.setDevice();
#ifdef DEBUG
    Serial << endl << "First time launched";
#endif
  }

  /* Checking if the firmware has been upgraded. Potentially runs post upgrade
   * code */
  AFEUpgrader Upgrader;
  if (Upgrader.upgraded()) {
    Upgrader.upgrade();
#ifdef DEBUG
    Serial << endl << "Firmware upgraded";
#endif
  }
  Upgrader = {};

  /* Checking if WiFi is configured, if not then it runs configuration panel in
   * access point mode */
  if (Device.getMode() != MODE_ACCESS_POINT && !Device.isConfigured()) {
#ifdef DEBUG
    Serial << endl << "Going to configuration mode (HotSpot)";
#endif
    Device.reboot(MODE_ACCESS_POINT);
  }

  /* Initializing relay */
  initRelay();
#ifdef DEBUG
  Serial << endl << "Relay(s) initialized";
#endif

  /* Initialzing network */
  Network.begin(Device.getMode());
#ifdef DEBUG
  Serial << endl << "Network initialized";
#endif

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
#ifdef DEBUG
  Serial << endl << "WebServer initialized";
#endif

  /* Initializing switches */
  initSwitch();
#ifdef DEBUG
  Serial << endl << "Switch(es) initialized";
#endif

  if (Device.getMode() == MODE_NORMAL) {

/* Initializing Gate */
#if defined(T5_CONFIG)
    Gate.begin();
    GateState = Data.getGateConfiguration();
#ifdef DEBUG
    Serial << endl << "Gate initialized";
#endif
#endif

    /* Initializing DS18B20 or DHTxx sensor */
#if defined(T1_CONFIG) || defined(T2_CONFIG) || defined(T5_CONFIG)
    initSensor();
#ifdef DEBUG
    Serial << endl << "Sensors initialized";
#endif
#endif

/* Initializing T6 sesnors */
#if defined(T6_CONFIG)
    Wire.begin();
    if (Device.configuration.isHPMA115S0) {
      initHPMA115S0Sensor();
    }
    if (Device.configuration.isBME680) {
      BME680Sensor.begin();
    }
    if (Device.configuration.isBH1750) {
      BH1750Sensor.begin();
    }
#endif

#if defined(T3_CONFIG)
    /* Initializing PIRs */
    initPIR();
#ifdef DEBUG
    Serial << endl << "PIR initialized";
#endif
#endif
  }

  /* Initializing APIs */
  MQTTInit();
  DomoticzInit();

#ifdef DEBUG
  Serial << endl
         << "########################### BOOTING COMPLETED "
            "###########################"
         << endl;
#endif
}

void loop() {

  if (Device.getMode() != MODE_ACCESS_POINT) {
    if (Network.connected()) {
      if (Device.getMode() == MODE_NORMAL) {

        /* Triggerd when connectes/reconnects to WiFi */
        eventsListener();

        /* If MQTT API is on it listens for MQTT messages. If the device is
         * not connected to MQTT Broker, it connects the device to it */
        if (Device.configuration.mqttAPI) {
          Mqtt.listener();
        }

        /* Listens for HTTP requsts. Both for configuration panel HTTP
         * requests or HTTP API requests if it's turned on */
        WebServer.listener();

#if defined(T5_CONFIG)
        /* Listening for gate events */
        Gate.listener();
#endif

        /* Checking if there was received HTTP API Command */
        mainHTTPRequestsHandler();

#if defined(T5_CONFIG)
        /* Gate related events */
        mainGate();
#endif

#if !(defined(T3_CONFIG) || defined(T5_CONFIG))
        /* Relay related events */
        mainRelay();
#endif

#if defined(T1_CONFIG) || defined(T2_CONFIG) || defined(T5_CONFIG)
        /* Sensor: DS18B20 or DHT related code */
        mainSensor();
#endif

/* Sensor: HPMA115S0 related code  */
#if defined(T6_CONFIG)
        mainHPMA115S0Sensor();
        mainBME680Sensor();
        mainBH1750Sensor();
#endif

#if defined(T3_CONFIG)
        mainPIR();
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

/* Debug information */
#if defined(DEBUG)
  if (Device.getMode() == MODE_NORMAL) {
    debugListener();
  }
#endif
}

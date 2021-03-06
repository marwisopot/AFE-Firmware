/* AFE Firmware for smart home devices
  LICENSE: https://github.com/tschaban/AFE-Firmware/blob/master/LICENSE
  DOC: https://www.smartnydom.pl/afe-firmware-pl/ */

#ifndef _AFE_UPGRADER_h
#define _AFE_UPGRADER_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <AFE-Data-Access.h>
#include <AFE-Defaults.h>
// #include <Streaming.h>
class AFEUpgrader {

private:
  FIRMWARE FirmwareConfiguration;
  AFEDataAccess Data;
  AFEDefaults Defaults;

  /* Method erase eeprom but restores WiFi and Language configuration */
  void upgradeTypeOfFirmware();

#if defined(T1_CONFIG)
  /* Method upgrades to v1.1.0 */
  void upgradeToVersion110();
#endif

#if !(defined(T0_SHELLY_1_CONFIG) || defined(T6_CONFIG))
  /* Methods upgrades to v1.2.0 */
  void upgradeToVersion120();
#endif

#if defined(T2_CONFIG)
  void upgradeToVersion121();
#endif

#if defined(T2_CONFIG) || (T3_CONFIG) || (T5_CONFIG)
  void upgradeToVersion130();
#endif

public:
  /* Constructor */
  AFEUpgrader();

  /* Checks if firmware has been upgraded */
  boolean upgraded();

  /* Perform post upgrade changes */
  void upgrade();
};

#endif

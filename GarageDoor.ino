#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>
#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

#include "Secrets.h"


// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------

#define SERIAL_BAUDRATE     115200

#ifdef RGB_BUILTIN
const uint8_t ledPin = RGB_BUILTIN;
#else
const uint8_t ledPin = 2;  // Set your pin here if your board has not defined RGB_BUILTIN
#warning "Do not forget to set the RGB LED pin"
#endif

#define PIN_DOOR_BUTTON     1

const uint8_t DOOR_MOVE_SECONDS = 12;


// -----------------------------------------------------------------------------
// Door Button
// -----------------------------------------------------------------------------

enum DoorButtonState {
    DBS_Released = 0,
    DBS_Pressing = 1
};

class DoorButton {
private:
    uint8_t pin;
    DoorButtonState state;
    bool needsPress;
    long lastStateTransitionMillis;

public:
    DoorButton(uint8_t pin)
        : pin(pin)
        , state(DBS_Released)
        , needsPress(false)
        , lastStateTransitionMillis(millis()) {
    }

    void press() {
        needsPress = true;
    }

    void setup() {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        Serial.printf("[DOOR_BUTTON] Setup GPIO #%d\r\n", pin);
    }

    void update() {
        const long now = millis();
        if ((now - lastStateTransitionMillis) < 250) {
            return;
        }
        switch (state) {
        case DBS_Pressing:
            Serial.printf("[DOOR_BUTTON] Depressed after %d ms\r\n", now - lastStateTransitionMillis);
            digitalWrite(pin, LOW);
            state = DBS_Released;
            lastStateTransitionMillis = now;
            break;
        default:
        case DBS_Released:
            if (needsPress) {
                Serial.printf("[DOOR_BUTTON] Pressing...\r\n");
                digitalWrite(pin, HIGH);
                needsPress = false;
                state = DBS_Pressing;
                lastStateTransitionMillis = now;
            }
            break;
        }
    }
};


// -----------------------------------------------------------------------------
// Door
// -----------------------------------------------------------------------------

Preferences matterPref;
const char *liftPercentPrefKey = "LiftPercent";

enum DoorState {
  DS_Opened = 0,
  DS_Closed = 1,
  DS_Moving = 2,
};

class Door {
private:
  DoorButton button;
  MatterWindowCovering matterEndPoint;
  uint8_t currentLiftPercent;
  uint8_t targetLiftPercent;

  DoorState state;
  long lastStateTransitionMillis;

  long lastMatterUpdateMillis;
  long lastLEDUpdateMillis;
  long lastSerialUpdateMillis;

private:
  bool goToLiftPercentage(uint8_t liftPercent) {
    targetLiftPercent = liftPercent;

    Serial.printf("[DOOR] Moving from %d%% to %d%%\r\n", currentLiftPercent, targetLiftPercent);
    button.press();
    state = DS_Moving;
    lastStateTransitionMillis = millis();

    return true;
  }

  void setCurrentLiftPercent(uint8_t newLiftPercent, bool forceUpdateAndDisplay = false) {
    currentLiftPercent = newLiftPercent;
    const auto now = millis();

    if (forceUpdateAndDisplay || (now - lastMatterUpdateMillis) >= 1000) {
      lastMatterUpdateMillis = now;

      // Update CurrentPosition to reflect actual position (setLiftPercentage now only updates CurrentPosition)
      matterEndPoint.setLiftPercentage(currentLiftPercent);

      // Set operational status
      if (targetLiftPercent > currentLiftPercent) {
        matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN);
      }
      else if (targetLiftPercent < currentLiftPercent) {
        matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_DOWN_OR_CLOSE);
      }
      else {
        matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);
      }

      // Store state
      matterPref.putUChar(liftPercentPrefKey, currentLiftPercent);
    }

    if (forceUpdateAndDisplay || (now - lastLEDUpdateMillis) >= 100) {
      lastLEDUpdateMillis = now;
      visualize();
    }
    
    if (forceUpdateAndDisplay || (now - lastSerialUpdateMillis) >= 1000) {
      lastSerialUpdateMillis = now;
      Serial.printf("[DOOR] Current: %d%%\r\n", currentLiftPercent);
    }
  }

  void visualize() {
    const auto liftPercent = currentLiftPercent;
#ifdef RGB_BUILTIN
    // Use RGB LED to visualize lift position (brightness)
    // At LIFT 0% the opening is 100%
    // At LIFT 100% the opening is 0%
    float brightness = 1.0f - (float)liftPercent / 100.0f;  // 0.0 to 1.0
    uint8_t red = (uint8_t)(255 * brightness);
    uint8_t blue = red;
    uint8_t green = red;
    rgbLedWrite(ledPin, red, green, blue);
#else
    // For non-RGB boards, just use brightness
    uint8_t brightnessValue = map(liftPercent, 0, 100, 0, 255);
    analogWrite(ledPin, brightnessValue);
#endif
  }

public:
  Door(uint8_t buttonPin)
    : button(buttonPin)
    , currentLiftPercent(0)
    , targetLiftPercent(0)
    , state(DS_Closed)
    , lastStateTransitionMillis(0)
    , lastMatterUpdateMillis(0)
    , lastLEDUpdateMillis(0)
    , lastSerialUpdateMillis(0)
    {
  }

  void setup() {
    button.setup();

    // Initialize Matter EndPoint
    matterPref.begin("MatterPrefs", false);

    // default lift percentage is 0% (fully closed) if not stored before
    uint8_t oldLiftPercent = matterPref.getUChar(liftPercentPrefKey, 0);
    targetLiftPercent = oldLiftPercent;
    state = currentLiftPercent < 50 ? DS_Opened : DS_Closed;
    lastStateTransitionMillis = millis();

    matterEndPoint.begin(currentLiftPercent, 0, MatterWindowCovering::ROLLERSHADE_EXTERIOR);

    // Configure installed limits for lift. These are not used.
    matterEndPoint.setInstalledOpenLimitLift(0);
    matterEndPoint.setInstalledClosedLimitLift(100);

    Serial.printf("[DOOR] Initial position: Lift=%d%%\r\n", currentLiftPercent);

    matterEndPoint.onOpen([]() {
      Serial.printf("[DOOR] OPEN commanded\r\n");
      return true;
    });
    matterEndPoint.onClose([]() {
      Serial.printf("[DOOR] CLOSE commanded\r\n");
      return true;
    });
    matterEndPoint.onGoToLiftPercentage([this](uint8_t liftPercent) {
      return this->goToLiftPercentage(liftPercent);
    });
    matterEndPoint.onStop([]() {
      Serial.printf("[DOOR] STOP commanded\r\n");
      return true;
    });
  }

  void update() {
    button.update();

    const auto now = millis();
    
    if (state == DS_Moving) {
      const auto durationMillis = now - lastStateTransitionMillis;
      const float fraction = (float)durationMillis / ((float)DOOR_MOVE_SECONDS * 1000.0f);
      if (fraction >= 1.0f) {
        state = targetLiftPercent < 50 ? DS_Opened : DS_Closed;
        lastStateTransitionMillis = now;
        setCurrentLiftPercent(targetLiftPercent, true);
      }
      else {
        uint8_t newPercent = 0;
        if (targetLiftPercent < 50) {
          newPercent = (uint8_t)((1.0f - fraction) * 100.0f + 0.5f);
        }
        else {
          newPercent = (uint8_t)(fraction * 100.0f + 0.5f);
        }
        setCurrentLiftPercent(newPercent);
      }
    }
  }
};

Door door(PIN_DOOR_BUTTON);


// -----------------------------------------------------------------------------
// Matter
// -----------------------------------------------------------------------------

void matterSetup() {
  // Matter beginning - Last step, after all EndPoints are initialized
  Matter.begin();
  // This may be a restart of a already commissioned Matter accessory
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
    // Serial.printf("Initial state: Lift=%d%%\r\n", WindowBlinds.getLiftPercentage());
    // Update visualization based on initial state
    
  }
}

void matterUpdate() {
  // Check Matter Window Covering Commissioning state, which may change during execution of loop()
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("");
    Serial.println("Matter Node is not commissioned yet.");
    Serial.println("Initiate the device discovery in your Matter environment.");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
    const auto qrCodeUrl = Matter.getOnboardingQRCodeUrl();
    Serial.printf("QR code URL: %s\r\n", qrCodeUrl.c_str());
    // waits for Matter Window Covering Commissioning.
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0) {  // 50*100ms = 5 sec
        Serial.println("Matter Node not commissioned yet. Waiting for commissioning.");
      }
    }
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  }
}


// -----------------------------------------------------------------------------
// Wifi
// -----------------------------------------------------------------------------

void wifiSetup() {

    // Set WIFI module to STA mode
    WiFi.mode(WIFI_STA);

    // Connect
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Wait
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();

    // Connected!
    Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}


// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

void setup() {
    // Init serial port and clean garbage
    Serial.begin(SERIAL_BAUDRATE);
    Serial.println();
    Serial.println();

    // Wifi
    wifiSetup();

    // Door
    door.setup();

    // Matter
    matterSetup();
}

void loop() {
    matterUpdate();
    door.update();
}

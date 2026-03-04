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

const uint8_t LIFT_SECONDS = 12;

// Window covering limits
// Lift limits in centimeters (physical position)
const uint16_t MAX_LIFT = 200;  // Maximum lift position (fully open)
const uint16_t MIN_LIFT = 0;    // Minimum lift position (fully closed)


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

    void setNeedsPress() {
        needsPress = true;
    }

    void setup() {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
        Serial.printf("[DOOR] Button GPIO #%d\n", pin);
    }

    void update() {
        const long now = millis();
        if ((now - lastStateTransitionMillis) < 250) {
            return;
        }
        switch (state) {
        case DBS_Pressing:
            digitalWrite(pin, LOW);
            state = DBS_Released;
            lastStateTransitionMillis = now;
            break;
        default:
        case DBS_Released:
            if (needsPress) {
                digitalWrite(pin, HIGH);
                state = DBS_Pressing;
                needsPress = false;
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

class Door {
private:
  DoorButton button;
  MatterWindowCovering matterEndPoint;
  uint16_t currentLift = 0;
  uint8_t currentLiftPercent = 0;

private:
  bool goToLiftPercentage(uint8_t liftPercent) {
    Serial.printf("Moving from %d%% to %d%% (current position: %d cm)\r\n", currentLiftPercent, liftPercent, currentLift);
    // update Lift operational state
    if (liftPercent > currentLiftPercent) {
      // Set operational status to OPEN
      matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN);
    }
    if (liftPercent < currentLiftPercent) {
      // Set operational status to CLOSE
      matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_DOWN_OR_CLOSE);
    }

    // This is where you would trigger your motor to go towards liftPercent
    // For simulation, we update instantly
    // Calculate absolute position based on installed limits
    uint16_t openLimit = matterEndPoint.getInstalledOpenLimitLift();
    uint16_t closedLimit = matterEndPoint.getInstalledClosedLimitLift();

    // Linear interpolation: 0% = openLimit, 100% = closedLimit
    if (openLimit < closedLimit) {
      currentLift = openLimit + ((closedLimit - openLimit) * liftPercent) / 100;
    } else {
      currentLift = openLimit - ((openLimit - closedLimit) * liftPercent) / 100;
    }
    currentLiftPercent = liftPercent;
    Serial.printf("Moved to %d%% (position: %d cm)\r\n", currentLiftPercent, currentLift);

    // Update CurrentPosition to reflect actual position (setLiftPercentage now only updates CurrentPosition)
    matterEndPoint.setLiftPercentage(currentLiftPercent);

    // Set operational status to STALL when movement is complete
    matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);

    // Store state
    matterPref.putUChar(liftPercentPrefKey, currentLiftPercent);

    return true;
  }

  void visualizeDoor(uint8_t liftPercent) {
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

  void visualize() {
    visualizeDoor(matterEndPoint.getLiftPercentage());
  }

public:
  Door(uint8_t buttonPin) : button(buttonPin) {
  }

  void setup() {
    button.setup();

    // Initialize Matter EndPoint
    matterPref.begin("MatterPrefs", false);

    // default lift percentage is 0% (fully closed) if not stored before
    uint8_t lastLiftPercent = matterPref.getUChar(liftPercentPrefKey, 0);

    matterEndPoint.begin(lastLiftPercent, 0, MatterWindowCovering::ROLLERSHADE_EXTERIOR);

    // Configure installed limits for lift
    matterEndPoint.setInstalledOpenLimitLift(MIN_LIFT);
    matterEndPoint.setInstalledClosedLimitLift(MAX_LIFT);

    // Initialize current positions based on percentages and installed limits
    uint16_t openLimitLift = matterEndPoint.getInstalledOpenLimitLift();
    uint16_t closedLimitLift = matterEndPoint.getInstalledClosedLimitLift();
    currentLiftPercent = lastLiftPercent;
    if (openLimitLift < closedLimitLift) {
      currentLift = openLimitLift + ((closedLimitLift - openLimitLift) * lastLiftPercent) / 100;
    } else {
      currentLift = openLimitLift - ((openLimitLift - closedLimitLift) * lastLiftPercent) / 100;
    }

    Serial.printf(
      "Door limits configured: Lift [%d-%d cm]\r\n",
      matterEndPoint.getInstalledOpenLimitLift(),
      matterEndPoint.getInstalledClosedLimitLift());
    Serial.printf("Initial positions: Lift=%d cm (%d%%)\r\n", currentLift, currentLiftPercent);

    matterEndPoint.onOpen([]() {
      Serial.printf("OPEN commanded\r\n");
      return true;
    });
    matterEndPoint.onClose([]() {
      Serial.printf("CLOSE commanded\r\n");
      return true;
    });
    matterEndPoint.onGoToLiftPercentage([this](uint8_t liftPercent) {
      return this->goToLiftPercentage(liftPercent);
    });
    matterEndPoint.onStop([]() {
      Serial.printf("STOP commanded\r\n");
      return true;
    });
    matterEndPoint.onChange([this](uint8_t liftPercent, uint8_t tiltPercent) {
      Serial.printf("Door changed: Lift=%d%%, Tilt=%d%%\r\n", liftPercent, tiltPercent);
      this->visualizeDoor(liftPercent);
      return true;
    });
  }

  void update() {
    button.update();
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

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

private:
  bool goToLiftPercentage(uint8_t liftPercent) {
    targetLiftPercent = liftPercent;
    Serial.printf("Moving from %d%% to %d%%\r\n", currentLiftPercent, liftPercent);
    // update Lift operational state
    if (liftPercent > currentLiftPercent) {
      // Set operational status to OPEN
      matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN);
    }
    if (liftPercent < currentLiftPercent) {
      // Set operational status to CLOSE
      matterEndPoint.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_DOWN_OR_CLOSE);
    }

    currentLiftPercent = targetLiftPercent;
    Serial.printf("Moved to %d%%\r\n", currentLiftPercent);

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
  Door(uint8_t buttonPin)
    : button(buttonPin)
    , currentLiftPercent(0)
    , targetLiftPercent(0)
    , state(DS_Closed)
    , lastStateTransitionMillis(millis()) {
  }

  void setup() {
    button.setup();

    // Initialize Matter EndPoint
    matterPref.begin("MatterPrefs", false);

    // default lift percentage is 0% (fully closed) if not stored before
    uint8_t currentLiftPercent = matterPref.getUChar(liftPercentPrefKey, 0);
    state = currentLiftPercent < 50 ? DS_Opened : DS_Closed;

    matterEndPoint.begin(currentLiftPercent, 0, MatterWindowCovering::ROLLERSHADE_EXTERIOR);

    // Configure installed limits for lift. These are not used.
    matterEndPoint.setInstalledOpenLimitLift(0);
    matterEndPoint.setInstalledClosedLimitLift(100);

    Serial.printf("Initial position: Lift=%d%%\r\n", currentLiftPercent);

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

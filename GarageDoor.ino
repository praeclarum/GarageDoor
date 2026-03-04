#include <Arduino.h>
#include <Matter.h>
#include <Preferences.h>
#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

// Rename the credentials.sample.h file to credentials.h and 
// edit it according to your router configuration
#include "Secrets.h"

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------

#define NUM_DOORS           1

#define SERIAL_BAUDRATE     115200

#ifdef RGB_BUILTIN
const uint8_t ledPin = RGB_BUILTIN;
#else
const uint8_t ledPin = 2;  // Set your pin here if your board has not defined RGB_BUILTIN
#warning "Do not forget to set the RGB LED pin"
#endif

#define PIN_DOOR1           1
#define PIN_DOOR2           2
#define PIN_DOOR3           3
#define PIN_DOOR4           4
#define PIN_DOOR5           5

#define ID_DOOR1            "Door 1"
#define ID_DOOR2            "Door 2"
#define ID_DOOR3            "Door 3"
#define ID_DOOR4            "Door 4"
#define ID_DOOR5            "Door 5"

// -----------------------------------------------------------------------------
// Door
// -----------------------------------------------------------------------------

const uint8_t LIFT_SECONDS = 12;

// Window covering limits
// Lift limits in centimeters (physical position)
const uint16_t MAX_LIFT = 200;  // Maximum lift position (fully open)
const uint16_t MIN_LIFT = 0;    // Minimum lift position (fully closed)

// Current window covering state
// These will be initialized in setup() based on installed limits and saved percentages
uint16_t currentLift = 0;  // Lift position in cm
uint8_t currentLiftPercent = 100;

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

DoorButton door1(PIN_DOOR1);
DoorButton door2(PIN_DOOR2);
DoorButton door3(PIN_DOOR3);
DoorButton door4(PIN_DOOR4);
DoorButton door5(PIN_DOOR5);

DoorButton *doorButtons[5] = {
  &door1,
  &door2,
  &door3,
  &door4,
  &door5,
};

void doorSetup() {
  for (int i = 0; i < NUM_DOORS; i++) {
    doorButtons[i]->setup();
  }
}

void visualizeWindowBlinds(uint8_t liftPercent) {
#ifdef RGB_BUILTIN
  // Use RGB LED to visualize lift position (brightness)
  float brightness = (float)liftPercent / 100.0f;  // 0.0 to 1.0
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


// -----------------------------------------------------------------------------
// Matter
// -----------------------------------------------------------------------------

Preferences matterPref;
const char *liftPercentPrefKey = "LiftPercent";
// List of Matter Endpoints for this Node
// Window Covering Endpoint
MatterWindowCovering WindowBlinds;

bool goToLiftPercentage(uint8_t liftPercent) {
  // update Lift operational state
  if (liftPercent > currentLiftPercent) {
    // Set operational status to OPEN
    WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN);
  }
  if (liftPercent < currentLiftPercent) {
    // Set operational status to CLOSE
    WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_DOWN_OR_CLOSE);
  }

  // This is where you would trigger your motor to go towards liftPercent
  // For simulation, we update instantly
  // Calculate absolute position based on installed limits
  uint16_t openLimit = WindowBlinds.getInstalledOpenLimitLift();
  uint16_t closedLimit = WindowBlinds.getInstalledClosedLimitLift();

  // Linear interpolation: 0% = openLimit, 100% = closedLimit
  if (openLimit < closedLimit) {
    currentLift = openLimit + ((closedLimit - openLimit) * liftPercent) / 100;
  } else {
    currentLift = openLimit - ((openLimit - closedLimit) * liftPercent) / 100;
  }
  currentLiftPercent = liftPercent;
  Serial.printf("Moving lift to %d%% (position: %d cm)\r\n", currentLiftPercent, currentLift);

  // Update CurrentPosition to reflect actual position (setLiftPercentage now only updates CurrentPosition)
  WindowBlinds.setLiftPercentage(currentLiftPercent);

  // Set operational status to STALL when movement is complete
  WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);

  // Store state
  matterPref.putUChar(liftPercentPrefKey, currentLiftPercent);

  return true;
}

bool stopMotor() {
  // Motor can be stopped while moving cover toward current target
  Serial.println("Stopping window covering motor");

  // Update CurrentPosition to reflect actual position when stopped
  WindowBlinds.setLiftPercentage(currentLiftPercent);

  // Set operational status to STALL for both lift
  WindowBlinds.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::STALL);

  return true;
}

void matterSetup() {
  // Initialize Matter EndPoint
  matterPref.begin("MatterPrefs", false);
  // default lift percentage is 0% (fully closed) if not stored before
  uint8_t lastLiftPercent = matterPref.getUChar(liftPercentPrefKey, 0);

  WindowBlinds.begin(lastLiftPercent, 0, MatterWindowCovering::ROLLERSHADE_EXTERIOR);

  // Configure installed limits for lift
  WindowBlinds.setInstalledOpenLimitLift(MIN_LIFT);
  WindowBlinds.setInstalledClosedLimitLift(MAX_LIFT);

  // Initialize current positions based on percentages and installed limits
  uint16_t openLimitLift = WindowBlinds.getInstalledOpenLimitLift();
  uint16_t closedLimitLift = WindowBlinds.getInstalledClosedLimitLift();
  currentLiftPercent = lastLiftPercent;
  if (openLimitLift < closedLimitLift) {
    currentLift = openLimitLift + ((closedLimitLift - openLimitLift) * lastLiftPercent) / 100;
  } else {
    currentLift = openLimitLift - ((openLimitLift - closedLimitLift) * lastLiftPercent) / 100;
  }

  Serial.printf(
    "Window Covering limits configured: Lift [%d-%d cm]\r\n",
    WindowBlinds.getInstalledOpenLimitLift(),
    WindowBlinds.getInstalledClosedLimitLift());
  Serial.printf("Initial positions: Lift=%d cm (%d%%)\r\n", currentLift, currentLiftPercent);

  // Set callback functions
  // WindowBlinds.onOpen(fullOpen);
  // WindowBlinds.onClose(fullClose);
  WindowBlinds.onGoToLiftPercentage(goToLiftPercentage);
  WindowBlinds.onStop(stopMotor);

  // Generic callback for Lift or Tilt change
  WindowBlinds.onChange([](uint8_t liftPercent, uint8_t tiltPercent) {
    Serial.printf("Window Covering changed: Lift=%d%%, Tilt=%d%%\r\n", liftPercent, tiltPercent);
    visualizeWindowBlinds(liftPercent);
    return true;
  });

  // Matter beginning - Last step, after all EndPoints are initialized
  Matter.begin();
  // This may be a restart of a already commissioned Matter accessory
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
    Serial.printf("Initial state: Lift=%d%%\r\n", WindowBlinds.getLiftPercentage());
    // Update visualization based on initial state
    visualizeWindowBlinds(WindowBlinds.getLiftPercentage());
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
    Serial.printf("Initial state: Lift=%d%%\r\n", WindowBlinds.getLiftPercentage());
    // Update visualization based on initial state
    visualizeWindowBlinds(WindowBlinds.getLiftPercentage());
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

    // Doors
    doorSetup();

    // Matter
    matterSetup();
}

void loop() {

    matterUpdate();

}

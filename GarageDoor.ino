#include <Arduino.h>
#ifdef ESP32
    #include <WiFi.h>
#else
    #include <ESP8266WiFi.h>
#endif
#include "fauxmoESP.h"

// Rename the credentials.sample.h file to credentials.h and 
// edit it according to your router configuration
#include "Secrets.h"

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------

#define NUM_DOORS           5

#define SERIAL_BAUDRATE     115200

#define PIN_RGB_LED         48

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
// Door Button Emulation
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


// -----------------------------------------------------------------------------
// Fauxmo
// -----------------------------------------------------------------------------

fauxmoESP fauxmo;

void fauxmoSetup() {
    // By default, fauxmoESP creates it's own webserver on the defined port
    // The TCP port must be 80 for gen3 devices (default is 1901)
    // This has to be done before the call to enable()
    fauxmo.createServer(true); // not needed, this is the default value
    fauxmo.setPort(80); // This is required for gen3 devices

    // You have to call enable(true) once you have a WiFi connection
    // You can enable or disable the library at any moment
    // Disabling it will prevent the devices from being discovered and switched
    fauxmo.enable(true);

    // You can use different ways to invoke alexa to modify the devices state:
    // "Alexa, turn yellow lamp on"
    // "Alexa, turn on yellow lamp
    // "Alexa, set yellow lamp to fifty" (50 means 50% of brightness, note, this example does not use this functionality)

    // Add virtual devices
    if (NUM_DOORS >= 1) fauxmo.addDevice(ID_DOOR1);
    if (NUM_DOORS >= 2) fauxmo.addDevice(ID_DOOR2);
    if (NUM_DOORS >= 3) fauxmo.addDevice(ID_DOOR3);
    if (NUM_DOORS >= 4) fauxmo.addDevice(ID_DOOR4);
    if (NUM_DOORS >= 5) fauxmo.addDevice(ID_DOOR5);

    fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
        
        // Callback when a command from Alexa is received. 
        // You can use device_id or device_name to choose the element to perform an action onto (relay, LED,...)
        // State is a boolean (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
        // Just remember not to delay too much here, this is a callback, exit as soon as possible.
        // If you have to do something more involved here set a flag and process it in your main loop.
        
        Serial.printf("[FAUXMO] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);

        // Checking for device_id is simpler if you are certain about the order they are loaded and it does not change.
        // Otherwise comparing the device_name is safer.

        if (strcmp(device_name, ID_DOOR1)==0) {
            door1.setNeedsPress();
        } else if (strcmp(device_name, ID_DOOR2)==0) {
            door2.setNeedsPress();
        } else if (strcmp(device_name, ID_DOOR3)==0) {
            door3.setNeedsPress();
        } else if (strcmp(device_name, ID_DOOR4)==0) {
            door4.setNeedsPress();
        } else if (strcmp(device_name, ID_DOOR5)==0) {
            door5.setNeedsPress();
        }

    });
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

    // Fauxmo
    fauxmoSetup();
}

void loop() {

    fauxmo.handle();
    // fauxmo.setState(ID_DOOR1, true, 255);

}

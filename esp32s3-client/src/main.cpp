#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
// Client Code
#include "BLEDevice.h"
// Devices
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_SSD1306.h"
#include "Stepper.h"

// #define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(128, 64, &Wire);

// GPIO pins
#define BUTTON_PIN D8
#define GAUGE_PIN1 1
#define GAUGE_PIN2 2
#define GAUGE_PIN3 3
#define GAUGE_PIN4 4

// Stepper motor control
const int stepsPerRevolution = 200; // change this to fit the number of steps per revolution for your motor
Stepper myStepper(stepsPerRevolution, GAUGE_PIN1, GAUGE_PIN2, GAUGE_PIN3, GAUGE_PIN4);
int currentStepPosition = 0;

// BLE UUIDs
static BLEUUID serviceUUID("a533a097-1378-4b70-ba63-73ec362618c6");
static BLEUUID charUUID("e4e7aaee-85d0-4ff1-a4e2-b1087b5e6147");

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

static void notifyCallback(
    BLERemoteCharacteristic *pBLERemoteCharacteristic,
    uint8_t *pData,
    size_t length,
    bool isNotify)
{
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.write(pData, length);

  // Convert pData to a String for easier manipulation
  String dataStr = String((char *)pData).substring(0, length);

  // -- X27.168 bipolar Gague ---
  // Check for cat activity within the received data string
  if (dataStr.indexOf("Your cat is running") != -1)
  {
    // If the cat is running, move the stepper motor forward a full revolution
    myStepper.setSpeed(60); // Set speed
    Serial.println("Cat is running, moving forward a revolution.");
    myStepper.step(stepsPerRevolution); // Move forward a full revolution
  }
  else if (dataStr.indexOf("Your cat is resting") != -1)
  {
    // If the cat is resting, move the stepper motor back a full revolution
    myStepper.setSpeed(60); // Set speed
    Serial.println("Cat is resting, moving back a revolution.");
    myStepper.step(-stepsPerRevolution); // Move back a full revolution
  }

  // --- OLED print received data ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(dataStr);
  display.display();
  delay(2000); // Pause for 2 seconds for readability

  Serial.println();
}

class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
  }

  void onDisconnect(BLEClient *pclient)
  {
    connected = false;
    Serial.println("onDisconnect");
  }
};

// -- Clear OLED messages --
void clearDisplayAndShowMessage()
{
  display.clearDisplay();
  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.println("\n Display Cleared! \n \n   =^..^=");
  display.display();
  Serial.println("Display cleared and message shown.");
}

// --- Connect to server ---
bool connectToServer()
{
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remote BLE Server.
  pClient->connect(myDevice);
  Serial.println(" - Connected to server");
  pClient->setMTU(517);

  // Obtain a reference to the service in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr)
  {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  connected = true;
  return true;
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());

    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID))
    {
      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;
    }
  }
};

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  BLEDevice::init("");

  // --- Initialize OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);              // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE); // Draw white text
  display.setCursor(0, 0);             // Start at top-left corner
  display.print("Connecting...");
  display.display();
  delay(2000); // Pause for 2 seconds

  // --- Initialize Button ---
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // --- BLE Scan setup ---
  BLEScan *pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->start(5, false);
}

void loop()
{
  int buttonState = digitalRead(BUTTON_PIN);
  // Serial.println(buttonState);

  bool btnFlag = false;

  if (buttonState == LOW)
  {
    Serial.println("Button pressed!");
    clearDisplayAndShowMessage();
  }
  else
  {
    Serial.println("not clicked");
  }

  if (doConnect == true)
  {
    if (connectToServer())
    {
      Serial.println("We are now connected to the BLE Server.");
    }
    else
    {
      Serial.println("We have failed to connect to the server; there is nothing more we will do.");
    }
    doConnect = false;
  }

  if (connected)
  {
    // TODO: Here you can add code that needs to run when connected
  }
  else if (doScan)
  {
    BLEDevice::getScan()->start(0);
  }

  delay(1000); // Delay a second between loops.
}

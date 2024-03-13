#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <stdlib.h>
#include <Wire.h>
// Sensors
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MPU6050.h>
// For calculation
#include <CircularBuffer.h>
// ML library
#include <chelsey_uw-project-1_inferencing.h>

// Define buffers for X, Y, Z readings
CircularBuffer<float, 20> xReadings;
CircularBuffer<float, 20> yReadings;
CircularBuffer<float, 20> zReadings;

// WiFi and Firebase configuration details
const char *ssid = "UW MPSK";
const char *password = "K!(t7n4$#j";
#define DATABASE_URL "https://esp32-firebase-demo-4050d-default-rtdb.firebaseio.com/"
#define API_KEY "AIzaSyACPXeTJMeQ6rpxPSZTm4ZbSYWh96iHra4"
#define STAGE_INTERVAL 12000 // Time in milliseconds for each operational stage
#define MAX_WIFI_RETRIES 5   // Maximum attempts to connect to WiFi

int uploadInterval = 1000; // Interval for uploading data to Firebase in milliseconds

/* Private variables ------------------------------------------------------- */
static const bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long previousMillis = 0;
const long interval = 1000;

Adafruit_MPU6050 mpu;
Adafruit_BME280 bme;

#define SERVICE_UUID "a533a097-1378-4b70-ba63-73ec362618c6"
#define CHARACTERISTIC_UUID "e4e7aaee-85d0-4ff1-a4e2-b1087b5e6147"

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting BLE work!");

  // --- Sensor connection check ---
  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
      ;
  }

  if (!bme.begin(0x76)) // Use 0x76 or 0x77 depending on your wiring
  {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  Serial.println("MPU6050 and BME280 initialized");

  // --- Bluetooth connection part ---
  BLEDevice::init("XIAO_ESP32S3");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue("Hello World");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}
void loop()
{

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float temperature = bme.readTemperature();
  float humidity = bme.readHumidity();

  // --- calculate current reading diff average ---
  xReadings.push(g.gyro.x);
  yReadings.push(g.gyro.y);
  zReadings.push(g.gyro.z);

  float avgGyroX = 0, avgGyroY = 0, avgGyroZ = 0;

  if (xReadings.size() > 1)
  {
    float sumDiffX = 0, sumDiffY = 0, sumDiffZ = 0;
    for (int i = 1; i < xReadings.size(); ++i)
    {
      sumDiffX += abs(xReadings[i] - xReadings[i - 1]);
      sumDiffY += abs(yReadings[i] - yReadings[i - 1]);
      sumDiffZ += abs(zReadings[i] - zReadings[i - 1]);
    }
    avgGyroX = sumDiffX / (xReadings.size() - 1);
    avgGyroY = sumDiffY / (yReadings.size() - 1);
    avgGyroZ = sumDiffZ / (zReadings.size() - 1);
  }
  Serial.print("------");
  Serial.println("readings" + String(avgGyroX) + String(avgGyroY) + String(avgGyroZ));

  String dataToSend = "empty";

  // --- MLlogic ---
  // Run the classifier
  ei_impulse_result_t result = {0};
  signal_t signal;
  float buffer[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE] = {0};
  if (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE >= 3)
  {
    buffer[0] = avgGyroX;
    buffer[1] = avgGyroY;
    buffer[2] = avgGyroZ;
    // If your model expects more features, continue filling the buffer here
  }
  numpy::signal_from_buffer(buffer, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
  if (err != EI_IMPULSE_OK)
  {
    ei_printf("ERR: run_classifier returned %d\r\n", err);
    return;
  }

  // Print the predictions
  ei_printf("Predictions:\r\n");
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
  {
    ei_printf("    %s: %.5f\r\n", result.classification[ix].label, result.classification[ix].value);
  }

  // Example of handling predictions (adjust according to your application)
  String activityLabel = "";
  float highestConfidence = 0.0f;
  for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++)
  {
    if (result.classification[ix].value > highestConfidence)
    {
      highestConfidence = result.classification[ix].value;
      activityLabel = result.classification[ix].label;
    }
  }

  if (activityLabel != "")
  {
    Serial.println("Most likely activity: " + activityLabel);
    dataToSend = "Your cat is " + activityLabel + "! \n\n";

    // Here you can add your code to act upon the detected activity
    // For example, send activityLabel and highestConfidence via BLE
    if (activityLabel == "running")
    {
      if (temperature > 24)
      {
        dataToSend = dataToSend + "The temperature is " + temperature + "C. Lower it into 24. \n\n";
      }
      else if (temperature < 24)
      {
        dataToSend = dataToSend + "The temperature is " + temperature + "C. Increase it into 24. \n\n";
      }
      else
      {
        dataToSend = dataToSend + "The temperature is 24 C. Perfect! \n\n";
      }

      if (humidity > 35)
      {
        dataToSend = dataToSend + "The humidity is " + humidity + "%. Lower it into 35%. \n";
      }
      else if (humidity < 35)
      {
        dataToSend = dataToSend + "The humidity is " + humidity + "%. Increase it into 35%. \n";
      }
      else
      {
        dataToSend = dataToSend + "The humidity is 35%. Perfect! \n";
      }
    }
    else
    {
      if (temperature > 26)
      {
        dataToSend = dataToSend + "The temperature is " + temperature + "C. Lower it into 26. \n\n";
      }
      else if (temperature < 26)
      {
        dataToSend = dataToSend + "The temperature is " + temperature + "C. Increase it into 26. \n\n";
      }
      else
      {
        dataToSend = dataToSend + "The temperature is 26 C. Perfect! \n\n";
      }

      if (humidity > 30)
      {
        dataToSend = dataToSend + "The humidity is " + humidity + "%. Lower it into 30%. \n";
      }
      else if (humidity < 30)
      {
        dataToSend = dataToSend + "The humidity is " + humidity + "%. Increase it into 30%. \n";
      }
      else
      {
        dataToSend = dataToSend + "The humidity is 30%. Perfect! \n";
      }
    }
  }

  // --- Device connect ---
  if (deviceConnected)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      Serial.println("Notify value: " + dataToSend);
      pCharacteristic->setValue(dataToSend.c_str());
      pCharacteristic->notify();
      previousMillis = currentMillis;
    }
  }

  // --- Device disconnect ---
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }

  if (deviceConnected && !oldDeviceConnected)
  {
    oldDeviceConnected = deviceConnected;
  }

  delay(1000);
}

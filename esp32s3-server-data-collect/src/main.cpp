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

// Define buffers for X, Y, Z readings
CircularBuffer<float, 50> xReadings;
CircularBuffer<float, 50> yReadings;
CircularBuffer<float, 50> zReadings;

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

  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip");
    while (1)
      ;
  }

  if (!bme.begin(0x76))
  { // Use 0x76 or 0x77 depending on your wiring
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  Serial.println("MPU6050 and BME280 initialized");

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

  // Add the new readings to the buffers
  xReadings.push(g.gyro.x);
  yReadings.push(g.gyro.y);
  zReadings.push(g.gyro.z);

  // Calculate the average of the differences
  float avgDiffX = 0, avgDiffY = 0, avgDiffZ = 0;

  if (xReadings.size() > 1)
  { // Ensure we have at least two readings to compare
    float sumDiffX = 0, sumDiffY = 0, sumDiffZ = 0;
    for (int i = 1; i < xReadings.size(); ++i)
    {
      sumDiffX += abs(xReadings[i] - xReadings[i - 1]);
      sumDiffY += abs(yReadings[i] - yReadings[i - 1]);
      sumDiffZ += abs(zReadings[i] - zReadings[i - 1]);
    }
    avgDiffX = sumDiffX / (xReadings.size() - 1);
    avgDiffY = sumDiffY / (yReadings.size() - 1);
    avgDiffZ = sumDiffZ / (zReadings.size() - 1);
  }

  Serial.println("--------");
  Serial.print("Temperature = ");
  Serial.print(temperature);
  Serial.println("C");
  Serial.print("Humidity = ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("Average Gyroscope Difference X: ");
  Serial.print(avgDiffX);
  Serial.print(", Y: ");
  Serial.print(avgDiffY);
  Serial.print(", Z: ");
  Serial.println(avgDiffZ);
  Serial.println("--------");

  if (deviceConnected)
  {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= interval)
    {
      // Prepare the data string to send
      String dataToSend = "Humidity:" + String(humidity, 2) +
                          "Temperature:" + String(temperature, 2) +
                          ",AvgGyroX:" + String(avgDiffX, 2) +
                          ",AvgGyroY:" + String(avgDiffY, 2) +
                          ",AvgGyroZ:" + String(avgDiffZ, 2);
      pCharacteristic->setValue(dataToSend.c_str());
      pCharacteristic->notify();
      Serial.println("Notify value: " + dataToSend);
      previousMillis = currentMillis;
    }
  }

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

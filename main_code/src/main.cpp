#include <Wire.h>    // For I2C
#include "MPU6050.h" // Assuming you have this library
#include "BME280.h"  // Assuming you have this library

// MPU6050 mpu;
// BME280 bme;

const int mpuPin = 2;
const int bmePin = 3;

void setup()
{
  Serial.begin(115200);
  Wire.begin();

  if (!mpuPin.begin())
  {
    Serial.println("Could not find a valid MPU6050 sensor, check wiring!");
    while (1)
      ;
  }

  if (!bmePin.begin())
  {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1)
      ;
  }

  // Setup sensor configurations as needed
}

void loop()
{
  // Read data from MPU6050
  float ax, ay, az, gx, gy, gz;
  mpuPin.getAcceleration(&ax, &ay, &az);
  mpuPin.getRotation(&gx, &gy, &gz);

  // Read data from BME280
  float temperature = bmePin.readTemperature();
  float humidity = bmePin.readHumidity();

  // Process data to define cat's status
  String status = defineCatStatus(ax, ay, az, gx, gy, gz);
  checkEnvironment(temperature, humidity, status);

  // Delay between readings
  delay(1000);
}

String defineCatStatus(float ax, float ay, float az, float gx, float gy, float gz)
{
  // Simplified logic to determine status based on accelerometer data
  float totalAcceleration = sqrt(ax * ax + ay * ay + az * az);
  // Define thresholds for sleeping, walking, running
  if (totalAcceleration < thresholdSleeping)
  {
    return "Sleeping";
  }
  else if (totalAcceleration < thresholdWalking)
  {
    return "Walking";
  }
  else
  {
    return "Running";
  }
}

void checkEnvironment(float temperature, float humidity, String status)
{
  // Check if environment is ideal based on status
  if (status == "Sleeping" && (temperature != 28 || humidity != 40))
  {
    Serial.println("Adjust environment: Sleeping requires 28C, 40% humidity");
  }
  else if (status == "Walking" && (temperature != 28 || humidity != 40))
  {
    Serial.println("Adjust environment: Walking requires 28C, 40% humidity");
  }
  else if (status == "Running" && (temperature != 25 || humidity != 40))
  {
    Serial.println("Adjust environment: Running requires 25C, 40% humidity");
  }
}

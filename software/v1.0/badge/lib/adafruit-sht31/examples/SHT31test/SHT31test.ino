
#include "math.h"
#include "adafruit-sht31.h"

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void setup() {
  Serial.begin(115200);
  Serial.println("SHT31 test");
  if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    }
  }

void loop() {
    
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  float tF = (t* 9) /5 + 32;

  if (! isnan(t)) {  // check if 'is not a number'
     //Temperature in C
    Serial.print("Temp *C = "); Serial.println(t);
    //Temperature in F
    Serial.print("Temp *F = "); Serial.println(tF);
  } else { 
    Serial.println("Failed to read temperature");
  }
  
  if (! isnan(h)) {  // check if 'is not a number'
    Serial.print("Hum. % = "); Serial.println(h);
  } else { 
    Serial.println("Failed to read humidity");
  }
  Serial.println();
  delay(1000);
}

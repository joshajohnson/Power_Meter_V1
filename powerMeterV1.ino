#include<Wire.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "HX711.h"

#define MPU6050 0x68  // I2C address of the MPU-6050
#define GYRO_CONFIG 0x1B // Register for gyro range / sensitivity

/* Strain gauge initialization*/
HX711 scale(D5, D4);    // parameter "gain" is ommited; the default value 128 is used by the library

/* WiFi Config */
const char* ssid = "ssid";
const char* password = "password";
const char* mqtt_server = "address";

/* Variable Config */
int16_t GyZ;
float GyZ_Radians, GyZ_RPM, torque, reading, offset, power; // All set to zero and will be defined as required
String rpm_str, torque_str, power_str; // For MQTT transmission
float calibrationFactor = 0.0000406; // Calibration factor for the strain gauge

/* MQTT Setup (Pub Sub Client) */
WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char rpm_msg[50];
char power_msg[50];
char torque_msg[50];

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  randomSeed(micros());

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}


void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect("address", "username", "password")) { // User / password for the MQTT server
      Serial.println("connected"); 
      } 
      else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup(){
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  /* Get the zero offset value for the strain gauges */
  offset = scale.read();     // NOTE: Crank needs to be at 6 o'clock on powerup for this to work

  /* GYRO STUFF BEGINS HERE */
  Wire.begin();

  /* Wake sensor up */
  Wire.beginTransmission(MPU6050);
  Wire.write(0x6B);  // PWR_MGMT_1 register
  Wire.write(0);     // set to zero (wakes up the MPU-6050)
  Wire.endTransmission();

  /* Set range of gyro */
  Wire.beginTransmission(MPU6050);
  Wire.write(GYRO_CONFIG);  // GYRO_CONFIG register
  Wire.write(0b00010000);  // Sets to 1000 dps sensitvity (166 rpm max). See register map for more details
  Wire.endTransmission();

  /* Check the register for the range has been set correctly */
  //Only required for troubleshooting
  //Wire.beginTransmission(MPU6050);
  //Wire.write(GYRO_CONFIG);
  //Wire.endTransmission();
  //Wire.requestFrom(MPU6050,1);
  //int GCVal = Wire.read();
  //Serial.print("GYRO_CONFIG Register Value = "); Serial.println(GCVal);
}

void loop(){

  if (!client.connected()) {
  reconnect();
  }
  client.loop();

  /* Get the rotation value from the Gyro */
  Wire.beginTransmission(MPU6050);
  Wire.write(0x47);  // starting with register 0x47 (GYRO_ZOUT_H)
  Wire.endTransmission();
  Wire.requestFrom(MPU6050,2);  // request a total of 2 registers
  GyZ = Wire.read() << 8;  // read most significant byte first
  GyZ |= Wire.read();      // then least significant byte
  // 0x47 (GYRO_ZOUT_H) & 0x48 (GYRO_ZOUT_L) together give the 16 bit GyZ value
  // Serial.println(GyZ); // Used for initally setting the zero point of the Z gyro
  GyZ -= 40; // Zero offset the gyro 

  /* Get values from the strain gauges */
  reading = scale.read();
  torque = (reading - offset) * calibrationFactor;

  /* Time to do some maths */
  GyZ_Radians = -GyZ * (1/32.8) * (3.14 / 180); // Convert into radians / sec
  GyZ_RPM = -GyZ * 0.0051; // Convert into revs / sec

  power = GyZ_Radians * torque; // Get the power value in Watts
  Serial.println("");
  Serial.print("Torque: "); Serial.println(torque);
  Serial.print("Power: "); Serial.println(power);
  Serial.print("Omega: "); Serial.println(GyZ_Radians);
  
  long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;

    rpm_str = String(GyZ_RPM);
    rpm_str.toCharArray(rpm_msg, rpm_str.length() + 1); 
    torque_str = String(torque);
    torque_str.toCharArray(torque_msg, torque_str.length() + 1);
    power_str = String(power);
    power_str.toCharArray(power_msg, power_str.length() + 1);


    client.publish("rpm", rpm_msg);
    client.publish("torque", torque_msg);
    client.publish("power", power_msg);

    Serial.println("Message sent to MQTT");
  }

}


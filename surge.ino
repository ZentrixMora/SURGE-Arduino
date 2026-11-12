#include <WiFiEsp.h>
#include "config.h"

// --- Wi-Fi & Backend Configuration ---
char ssid[] = SSID_NAME;    
char pass[] = SSID_PASS; 
char server[] = SERVER_IP;  
int port = SERVER_PORT;                  
String path = "/api/sensor-data";
WiFiEspClient client;

// --- Hardware Pin Definitions ---
const int trigPins[] = {2, 4, 26, 8};
const int echoPins[] = {3, 5, 27, 9};

const int ledGreen = 10;
const int ledYellow = 11;
const int ledRed = 12;
const int buzzer = 13;

// --- Calibration Variables ---
const int dangerThreshold = 15; // Trigger alarm if water is closer than 15cm
const int maxDistance = 200;    // Maximum physical distance for sanity check

// --- Sensor Reading Function ---
long getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH);
  long distance = duration * 0.034 / 2;
  
  // Sanity Check: Discard physically impossible readings
  if (distance == 0 || distance > maxDistance) {
    return -1; 
  }
  return distance;
}

void setup() {
  Serial.begin(115200);      // USB Serial Monitor baud rate
  Serial1.begin(115200);     // Hardware Serial1 link to ESP-01S (Arduino TX->19 / RX->18)

  // 1. Initialize ESP module FIRST (prevents buffer timeout)
  WiFi.init(&Serial1);

  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("ESP-01S not responding! Check wiring and power.");
  } else {
    // Attempt Wi-Fi connection with a timeout (max 5 attempts)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 5) {
      Serial.print("Connecting to Wi-Fi...");
      WiFi.begin(ssid, pass);
      delay(3000);
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to Wi-Fi!");
      Serial.print("Assigned IP Address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nWi-Fi connection failed. Proceeding offline.");
    }
  }

  // 2. Initialize Sensor & Output Pins AFTER Wi-Fi setup
  for (int i = 0; i < 4; i++) {
    pinMode(trigPins[i], OUTPUT);
    pinMode(echoPins[i], INPUT);
  }
  pinMode(ledGreen, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(buzzer, OUTPUT);

  pinMode(1,OUTPUT);
}

void loop() {
  digitalWrite(1,HIGH);
  long distances[4];
  bool danger[4] = {false, false, false, false};
  int warningCount = 0;

  // 1. Sequential Pinging to Prevent Cross-Talk
  for (int i = 0; i < 4; i++) {
    distances[i] = getDistance(trigPins[i], echoPins[i]);
    
    if (distances[i] != -1 && distances[i] < dangerThreshold) {
      danger[i] = true;
      warningCount++;
    }
    
    // Mandatory 50ms delay between sensor pings
    delay(50); 
  }

  // 2. Multi-Sensor Voting Logic (The Consensus Rule)
  bool consensusAlarm = false;
  if ((danger[0] && danger[1]) || 
      (danger[1] && danger[2]) || 
      (danger[2] && danger[3])) {
    consensusAlarm = true;
  }

  // 3. Actuate Warning System Locally
  if (consensusAlarm) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, HIGH);
    digitalWrite(buzzer, HIGH);
  } else if (warningCount > 0) {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, HIGH);
    digitalWrite(ledRed, LOW);
    digitalWrite(buzzer, LOW);
  } else {
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
    digitalWrite(buzzer, LOW);
  }

  // 4. Construct JSON Payload String with Actual Sensor Data
  String jsonPayload = "{\"device_id\":\"SURGE-001\",\"readings\":[";
  jsonPayload += "{\"sensor_id\":1,\"distance_cm\":" + String(distances[0]) + "},";
  jsonPayload += "{\"sensor_id\":2,\"distance_cm\":" + String(distances[1]) + "},";
  jsonPayload += "{\"sensor_id\":3,\"distance_cm\":" + String(distances[2]) + "},";
  jsonPayload += "{\"sensor_id\":4,\"distance_cm\":" + String(distances[3]) + "}";
  jsonPayload += "]}";

  // 5. Send Data over HTTP POST (only if connected)
  if (WiFi.status() == WL_CONNECTED) {
    if (client.connect(server, port)) {
      Serial.println("Connected to Express server! Sending POST payload...");

      // Build complete HTTP POST request in a single string buffer
      String httpRequest = String("POST ") + path + " HTTP/1.1\r\n" +
                           "Host: " + server + ":" + port + "\r\n" +
                           "Content-Type: application/json\r\n" +
                           "Content-Length: " + jsonPayload.length() + "\r\n" +
                           "Connection: close\r\n\r\n" + 
                           jsonPayload + "\r\n";

      // Transmit request in one single call
      client.print(httpRequest);
      Serial.println("Payload sent successfully!");
    } else {
      Serial.println("Connection to Express server failed.");
    }
    
    // Brief pause to allow the module to finish flushing remaining bytes
    delay(500);
    client.stop();
  }

  // Wait before repeating the transmission cycle
  delay(200);
}
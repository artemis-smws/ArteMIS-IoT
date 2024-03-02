#include <WiFi.h>
#include <HTTPClient.h>
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRutils.h>

const int trigPin = 5; // Trigger pin
const int echoPin = 18; // Echo pin
const int irPin = 14; // IR sensor pin
const int minDistance = 0; // Minimum distance in inches
const int maxDistance = 30; // Maximum distance in inches
const float thresholdPercentage = 5.0; // Threshold percentage for capacity increase

// Define sound speed in cm/uS
#define SOUND_SPEED 0.034
#define CM_TO_INCH 0.393701

bool wifiEnabled = false; // Initially, WiFi is disabled
bool requestSent = false; // Flag to track if request has been sent
bool initialRequestSent = false; // Flag to track if initial request has been sent
bool thresholdMet = false; // Flag to track if threshold has been met
bool lowCapacityMet = false; // Flag to track if low capacity has been met
float lastDistanceInch = 0; // Variable to store the last measured distance
int lastCapacity = 0; // Variable to store the last measured capacity
unsigned long thresholdStartTime = 0; // Variable to store the time threshold was met
unsigned long lowCapacityStartTime = 0; // Variable to store the time low capacity was met
unsigned long previousMillis = 0; // Variable to store the last time WiFi was enabled
unsigned long frequencyCount = 0; // Variable to store the frequency count
unsigned long lastDay = 0; // Variable to store the last day
unsigned long lastObjectDetectionTime = 0; // Variable to store the time that object detected
const unsigned long interval = 5000; // Interval in milliseconds (5 seconds)
const unsigned long thresholdDuration = 3000; // Threshold duration in milliseconds (3 seconds)
const unsigned long lowCapacityDuration = 5000; // Low capacity duration in milliseconds (5 seconds)

// WiFi credentials
const char* ssid = "DAVID_BHOUSE_4G";
const char* password = "PLDTWIFIdavid28";

// Create an instance of the IR receiver
IRrecv irrecv(irPin);

void setup() {
  Serial.begin(9600); // Start serial communication
  pinMode(trigPin, OUTPUT); // Set the trigPin as an output
  pinMode(echoPin, INPUT); // Set the echoPin as an input
  irrecv.enableIRIn(); // Start the receiver
}

void loop() {
  unsigned long currentMillis = millis(); // Get the current time

  float distanceInch = measureDistance();
  int capacity = mapDistanceToCapacity(distanceInch);

  decode_results results;
  
  if (irrecv.decode(&results)) {
    // Increment object count when within the last 3 second atleast an object is detected
    if (currentMillis - lastObjectDetectionTime >= 3000) {
      frequencyCount++;
    }
    
    Serial.print("Object Count: ");
    Serial.println(frequencyCount);
    
    // Update the last object detection time
    lastObjectDetectionTime = currentMillis;

    // Enable the receiver to receive the next value
    irrecv.resume();
  }
  
  // Check if initial request has been sent
  if (!initialRequestSent) {
    if (!wifiEnabled) {
      connectToWiFi(); // Connect to WiFi if not already connected
      previousMillis = currentMillis; // Update the time WiFi was enabled
    }
    sendInitialAPIRequest(capacity); // Send initial API request when first active
    initialRequestSent = true;
    requestSent = true; // Set flag to true after request is sent
    lastCapacity = capacity; // Update last capacity
  }
  
  if (capacity > lastCapacity * (1 + thresholdPercentage / 100.0) && capacity > lastCapacity + 5) {
    // If capacity increased by threshold percentage and more than 5
    if (!wifiEnabled) {
      connectToWiFi(); // Connect to WiFi if not already connected
      previousMillis = currentMillis; // Update the time WiFi was enabled
    }
    
    if (wifiEnabled && !requestSent && !thresholdMet) {
      // If WiFi is enabled and request not sent and threshold not met, start threshold timer
      thresholdMet = true; // Set threshold met flag
      thresholdStartTime = currentMillis; // Record threshold start time
    }

    // Check if threshold has been met for the specified duration
    if (thresholdMet && (currentMillis - thresholdStartTime >= thresholdDuration)) {
      // If threshold met for specified duration, send API request
      Serial.println("Threshold met for 3 seconds, sending API request");
      sendAPIRequest(capacity); // Send the capacity as capacity
      requestSent = true; // Set flag to true after request is sent
      lastCapacity = capacity; // Update last capacity
      thresholdMet = false; // Reset threshold met flag
    }
  } else {
    // If capacity did not increase by threshold percentage, print distance and capacity only
    requestSent = false; // Reset requestSent flag
    thresholdMet = false; // Reset threshold met flag
  }

  // Check if capacity is below 5% for 5 seconds
  if (capacity <= 5) {
    if (!lowCapacityMet) {
      // If low capacity threshold just met, record start time
      lowCapacityMet = true;
      lowCapacityStartTime = currentMillis;
    } else if (currentMillis - lowCapacityStartTime >= lowCapacityDuration) {
      // If low capacity has been met for 5 seconds, send API request and reset frequency
      if (!wifiEnabled) {
        connectToWiFi(); // Connect to WiFi if not already connected
        previousMillis = currentMillis; // Update the time WiFi was enabled
      }
      if (wifiEnabled && !requestSent) {
        Serial.println("Capacity below 5% for 5 seconds, sending API request");
        frequencyCount = 0; // Reset frequency
        sendAPIRequest(capacity); // Send the capacity as capacity
        requestSent = true; // Set flag to true after request is sent
        lastCapacity = capacity; // Update last capacity]
        lowCapacityMet = false; // Reset low capacity met flag
      }
    }
  } else {
    // If capacity is above 5%, reset low capacity met flag
    Serial.print("Distance (inch): ");
    Serial.print(distanceInch);
    Serial.print(", Capacity: ");
    Serial.println(capacity);
    requestSent = false; // Reset requestSent flag
    lowCapacityMet = false;
  }

  // Check if 5 seconds have passed and capacity hasn't increased by 5%
  if (wifiEnabled && (currentMillis - previousMillis >= interval) && (capacity <= lastCapacity * (1 + thresholdPercentage / 100.0))) {
    Serial.println("No significant capacity increase, disabling WiFi");
    WiFi.disconnect(); // Disconnect WiFi
    wifiEnabled = false; // Set WiFi flag to false
    requestSent = false; // Reset requestSent flag
    thresholdMet = false; // Reset threshold met flag
  }

  // Check if it's a new day and reset the request count
  unsigned long currentDay = currentMillis / (24 * 60 * 60 * 1000);
  if (currentDay != lastDay) {
    frequencyCount = 0; // Reset frequency
    lastDay = currentDay;
  }

  delay(1000); // Delay for stability
}

float measureDistance() {
  // Trigger the ultrasonic sensor
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Measure the duration of the echo pulse
  long duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance in inches
  float distanceInch = duration * SOUND_SPEED * CM_TO_INCH / 2.0;
  
  return distanceInch;
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }
  
  Serial.println("Connected to WiFi");
  wifiEnabled = true; // Set WiFi flag to true when connected
}

void sendAPIRequest(int capacity) {
  HTTPClient http;
  http.begin("https://us-central1-artemis-b18ae.cloudfunctions.net/v1/bin/artemis-a1-residual"); // API endpoint
  http.addHeader("Content-Type", "application/json");
  
  // Construct JSON payload
  String jsonPayload = "{\"capacity\": " + String(capacity) + ", \"frequency\": " + String(frequencyCount) + ", \"type\": \"residual\"}";
  
  // Send POST request with JSON payload
  int httpResponseCode = http.POST(jsonPayload);
  
  // Check for successful response
  if (httpResponseCode > 0) {
    Serial.print("API Request Success. Response code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("API Request Failed. Error code: ");
    Serial.println(httpResponseCode);
  }
  
  http.end(); // Free resources
}

void sendInitialAPIRequest(int capacity) {
  // Only send the initial API request once
  if (!initialRequestSent) {
    Serial.println("Sending initial API request...");
    sendAPIRequest(capacity);
    initialRequestSent = true;
  }
}

int mapDistanceToCapacity(float distance) {
  // Map the distance to a capacity between 0% and 100%
  int capacity = map(distance, maxDistance, minDistance, 0, 100);
  // Ensure capacity is within 0 to 100
  capacity = constrain(capacity, 0, 100);
  return capacity;
}

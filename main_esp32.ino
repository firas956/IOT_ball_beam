#include <WiFi.h>
#include <AsyncTCP.h>          // Third-party library required for WebSockets/Server
#include <ESPAsyncWebServer.h> // Third-party library required for Async Server
#include <Wire.h>
#include <Adafruit_VL53L0X.h>
#include <ArduinoJson.h>       // Third-party library for JSON

// Include our Web Interface HTML strings
#include "webpage.h"

// ==========================================
// 1. WIFI SETTINGS
// ==========================================
const char* ssid = "YOUR_ROUTER_SSID";
const char* password = "YOUR_ROUTER_PASSWORD";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ==========================================
// 2. HARDWARE PINS & GLOBALS
// ==========================================
// Adjust Step & Dir pins for your ESP32
const int stepPin = 18; 
const int dirPin = 19;  
const int microsteps = 16; 

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

double setpoint = 150.0; 
double Kp = 5.5;
double Ki = 0.0;
double Kd = 1.25;

const double errorDeadband = 10; 
const int stepDeadband = 0;       

double previousError = 0;
double integral = 0;
unsigned long previousTime = 0;
unsigned long lastWsUpdate = 0;

int currentStep = 61 * microsteps;
double currentDistance = 0.0; 

// ==========================================
// FUNCTION: Move Stepper
// ==========================================
void moveStepper(int numOfSteps, bool direction, int speedDelay) {
  digitalWrite(dirPin, direction);
  for (int i = 0; i < numOfSteps; i++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(speedDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(speedDelay);
  }
}

// ==========================================
// FUNCTION: Check Login Cookie
// ==========================================
bool checkAuth(AsyncWebServerRequest *request) {
  if (request->hasHeader("Cookie")) {
    String cookie = request->header("Cookie");
    if (cookie.indexOf("session=authenticated") != -1) {
      return true;
    }
  }
  return false;
}

// ==========================================
// FUNCTION: Handle Incoming Websocket Data
// ==========================================
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("Dashboard connected!");
    // Send initial PID values to the fresh webpage
    StaticJsonDocument<200> doc;
    doc["type"] = "init";
    doc["sp"] = setpoint;
    doc["kp"] = Kp;
    doc["ki"] = Ki;
    doc["kd"] = Kd;
    String response;
    serializeJson(doc, response);
    client->text(response);
    
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0; // Null-terminate
      
      // Parse JSON from webpage updating PID
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      if (!error) {
        setpoint = doc["sp"];
        Kp = doc["kp"];
        Ki = doc["ki"];
        Kd = doc["kd"];
        Serial.println("Updated PID parameters from Web Interface.");
      }
    }
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  
  Serial.begin(115200); 
  Wire.begin(); // ESP32 Default SDA=D21, SCL=D22
  
  if(!lox.begin()){
    Serial.println("Failed to boot VL53L0X. Check wiring.");
  }

  // --- CONNECT TO WIFI ---
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP()); // Type this IP in your PC browser!

  // Default Stepper Alignment
  moveStepper(61 * microsteps, HIGH, 1250 / microsteps); 
  delay(3000); 

  // --- WEB ROUTES ---
  
  // 1. Load starting page (Redirect to Dashboard if already logged in)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (checkAuth(request)) {
      request->redirect("/dashboard");
    } else {
      request->send_P(200, "text/html", login_html);
    }
  });

  // 2. Handle Login Button Action
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest *request){
    String user = "";
    String pass = "";
    if (request->hasParam("username", true)) user = request->getParam("username", true)->value();
    if (request->hasParam("password", true)) pass = request->getParam("password", true)->value();
    
    // Check Hardcoded Credentials
    if (user == "admin" && pass == "myadmin") {
      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "OK");
      // Give permission cookie valid for this browser session
      response->addHeader("Set-Cookie", "session=authenticated; Path=/");
      request->send(response);
    } else {
      request->send(401, "text/plain", "Unauthorized");
    }
  });

  // 3. Load Main Dashboard
  server.on("/dashboard", HTTP_GET, [](AsyncWebServerRequest *request){
    if (checkAuth(request)) {
      request->send_P(200, "text/html", index_html);
    } else {
      request->redirect("/");
    }
  });

  // Start Websocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  
  // Start Server
  server.begin();
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  // Required to maintain websocket connections cleanly
  ws.cleanupClients(); 

  unsigned long currentTime = millis();
  double dt = (currentTime - previousTime) / 1000.0;

  // 1. SENSOR AND PID LOGIC
  if (dt > 0) {
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    if (measure.RangeStatus != 4) {
      currentDistance = measure.RangeMilliMeter; 
      
      double error = setpoint - currentDistance;
      if (abs(error) <= errorDeadband) { error = 0.0; }
      
      integral += error * dt;
      integral = constrain(integral, -1000, 1000); 
      
      double derivative = (error - previousError) / dt;

      double output = (Kp * error) + (Ki * integral) + (Kd * derivative);
      output = constrain(output, -61 * microsteps, 39 * microsteps);
      
      int targetStep = (61 * microsteps) + (int)output;
      int stepsToMove = targetStep - currentStep;
      
      if (abs(stepsToMove) > stepDeadband) {
        bool dir = (stepsToMove > 0) ? LOW : HIGH;
        moveStepper(abs(stepsToMove), dir, 280); 
        currentStep = targetStep;
      }
      previousError = error;
    }
    previousTime = currentTime;
  }

  // 2. SEND GRAPH DATA EVERY 200ms
  if (currentTime - lastWsUpdate > 200) {
    if (ws.count() > 0) { 
      StaticJsonDocument<100> doc;
      doc["type"] = "update";
      doc["dist"] = currentDistance;
      doc["sp"] = setpoint;
      
      String payload;
      serializeJson(doc, payload);
      ws.textAll(payload); 
    }
    lastWsUpdate = currentTime;
  }
}

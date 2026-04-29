#include <Wire.h>
#include <Adafruit_VL53L0X.h>

const int stepPin = 3;
const int dirPin = 2;
const int microsteps = 16; 

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

double setpoint = 150.0; 
double Kp = 5.5;
double Ki = 0;
double Kd = 1.25;

const double errorDeadband = 10; 
const int stepDeadband = 0;       

double previousError = 0;
double integral = 0;
unsigned long previousTime = 0;

int currentStep = 61 * microsteps;

// NOUVEAU : On mémorise la dernière distance valide lue
double currentDistance = 0.0; 

void setup() {
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
  
  Serial.begin(115200); 
  Wire.begin();
  lox.begin();

  moveStepper(61 * microsteps, HIGH, 1250 / microsteps); 
  delay(3000); 
}

void moveStepper(int numOfSteps, bool direction, int speedDelay) {
  digitalWrite(dirPin, direction);
  for (int i = 0; i < numOfSteps; i++) {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(speedDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(speedDelay);
  }
}

void loop() {
  // 1. LECTURE DE LABVIEW
  if (Serial.available() > 0) {
    setpoint = Serial.parseFloat();
    Kp = Serial.parseFloat();
    Ki = Serial.parseFloat();
    Kd = Serial.parseFloat();
    
    // Clear the rest of the buffer until the newline
    while(Serial.available() > 0 && Serial.read() != '\n'); 
  }

  // 2. CAPTEUR ET PID
  unsigned long currentTime = millis();
  double dt = (currentTime - previousTime) / 1000.0;

  // On exécute le PID uniquement si le temps a avancé
  if (dt > 0) {
    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);

    // On exécute la suite uniquement si le capteur capte bien la bille
    if (measure.RangeStatus != 4) {
      currentDistance = measure.RangeMilliMeter; // Mise à jour de la distance
      
      double error = setpoint - currentDistance;
      
      if (abs(error) <= errorDeadband) {
        error = 0.0;
      }
      
      integral += error * dt;
      
      // Optional: Anti-windup for the integral term to prevent it from growing too large
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

  // 3. ENVOI DES DONNÉES
  // These MUST remain uncommented so LabVIEW receives both numbers and a newline character
  Serial.print(currentDistance);
  Serial.print(" ");           
  Serial.println(currentStep); 
  
  // Petite pause pour ne pas inonder LabVIEW (qui lit toutes les 50ms)
  delay(20); 
}
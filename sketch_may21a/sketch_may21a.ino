// Cambia estos valores segun tu conexion real.
static const int SENSOR_PIN = 27;
static const bool SENSOR_MODE_QTR_RC = true;

// Pinout inferido del schematic para el TB6612FNG.
static const int MOTOR_IZQ_PWM_PIN = 23;
static const int MOTOR_IZQ_IN1_PIN = 22;
static const int MOTOR_IZQ_IN2_PIN = 21;
static const int MOTOR_DER_PWM_PIN = 19;
static const int MOTOR_DER_IN1_PIN = 18;
static const int MOTOR_DER_IN2_PIN = 5;

uint32_t readQtrRc(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  delayMicroseconds(10);

  pinMode(pin, INPUT);

  uint32_t start = micros();
  while (digitalRead(pin) == HIGH) {
    if ((micros() - start) > 3000) {
      break;
    }
  }

  return micros() - start;
}

uint32_t readInfrared() {
  if (SENSOR_MODE_QTR_RC) {
    return readQtrRc(SENSOR_PIN);
  }

  return digitalRead(SENSOR_PIN);
}

void setupMotors() {
  pinMode(MOTOR_IZQ_PWM_PIN, OUTPUT);
  pinMode(MOTOR_IZQ_IN1_PIN, OUTPUT);
  pinMode(MOTOR_IZQ_IN2_PIN, OUTPUT);
  pinMode(MOTOR_DER_PWM_PIN, OUTPUT);
  pinMode(MOTOR_DER_IN1_PIN, OUTPUT);
  pinMode(MOTOR_DER_IN2_PIN, OUTPUT);

  analogWrite(MOTOR_IZQ_PWM_PIN, 0);
  analogWrite(MOTOR_DER_PWM_PIN, 0);
}

void setMotor(int pwmPin, int in1Pin, int in2Pin, int speedValue) {
  int pwm = constrain(abs(speedValue), 0, 255);

  if (speedValue > 0) {
    digitalWrite(in1Pin, HIGH);
    digitalWrite(in2Pin, LOW);
  } else if (speedValue < 0) {
    digitalWrite(in1Pin, LOW);
    digitalWrite(in2Pin, HIGH);
  } else {
    digitalWrite(in1Pin, LOW);
    digitalWrite(in2Pin, LOW);
  }

  analogWrite(pwmPin, pwm);
}

void stopMotors() {
  setMotor(MOTOR_IZQ_PWM_PIN, MOTOR_IZQ_IN1_PIN, MOTOR_IZQ_IN2_PIN, 0);
  setMotor(MOTOR_DER_PWM_PIN, MOTOR_DER_IN1_PIN, MOTOR_DER_IN2_PIN, 0);
}

void testMotors() {
  Serial.println("Motor izquierdo adelante");
  setMotor(MOTOR_IZQ_PWM_PIN, MOTOR_IZQ_IN1_PIN, MOTOR_IZQ_IN2_PIN, 180);
  setMotor(MOTOR_DER_PWM_PIN, MOTOR_DER_IN1_PIN, MOTOR_DER_IN2_PIN, 0);
  delay(1500);

  stopMotors();
  delay(500);

  Serial.println("Motor derecho adelante");
  setMotor(MOTOR_IZQ_PWM_PIN, MOTOR_IZQ_IN1_PIN, MOTOR_IZQ_IN2_PIN, 0);
  setMotor(MOTOR_DER_PWM_PIN, MOTOR_DER_IN1_PIN, MOTOR_DER_IN2_PIN, 180);
  delay(1500);

  stopMotors();
  delay(500);

  Serial.println("Ambos motores atras");
  setMotor(MOTOR_IZQ_PWM_PIN, MOTOR_IZQ_IN1_PIN, MOTOR_IZQ_IN2_PIN, -180);
  setMotor(MOTOR_DER_PWM_PIN, MOTOR_DER_IN1_PIN, MOTOR_DER_IN2_PIN, -180);
  delay(1500);

  stopMotors();
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  if (!SENSOR_MODE_QTR_RC) {
    pinMode(SENSOR_PIN, INPUT);
  }

  setupMotors();

  Serial.println("Inicio test sensor IR y motores");
}

void loop() {
  uint32_t infraredValue = readInfrared();

  Serial.print("IR: ");
  Serial.println(infraredValue);

  testMotors();
}

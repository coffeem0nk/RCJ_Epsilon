#include "config.h"
#include "math.h"

camBlock ball, home, target;
uint32_t targetClear = 0;

float k;

bool ballSensor = 0;
bool left_btn = false, center_btn = false, right_btn = false;
bool left_led = false, center_led = false, right_led = false;

short speed = 0;
float dir = 0;
float heading = 0;
float yaw = 0;
float yaw_offset = 0;

void dmpDataReady() {
  mpuInterrupt = true;
}

template <typename T>
T sign(T value) {
  return T((value > 0) - (value < 0));
}

double constrainAngle(double x) {
  x = fmod(x + PI, 2 * PI);
  if (x < 0)
    x += 2 * PI;
  return x - PI;
}

float calcAngle(int x, int y) {
  return (-acos((x - CAM_CENTER_X) / sqrt(pow(y - CAM_CENTER_Y, 2) + pow(x - CAM_CENTER_X, 2))) * (1 - 2 * (y > CAM_CENTER_Y)));
}

float calcAngle(camBlock b) {
  int x = b.x;
  int y = b.y;
  return calcAngle(x, y);
}

float calcDist(int x, int y) {
  return sqrt(pow(x - CAM_CENTER_X, 2) + pow(y - CAM_CENTER_Y, 2));
}

float calcDist(camBlock b) {
  int x = b.x;
  int y = b.y;
  return calcDist(x, y);
}

bool checkTimer(uint32_t timer, int dt) {
  return (millis() - timer < dt);
}

void dribbler(short speed) {
  digitalWrite(DRIBBLER_DIR, (speed > 0));
  analogWrite(DRIBBLER_SPEED, abs(speed));
}

void updateBtns() {
  left_btn = digitalRead(LEFT_BTN);
  center_btn = digitalRead(CENTER_BTN);
  right_btn = digitalRead(RIGHT_BTN);
}

void updateBallSensor() {
  ballSensor = digitalRead(BALL_SENSOR);
}

void kick(bool state) {
  static uint32_t kickTimer = 0;
  if (state) {
    if (!checkTimer(kickTimer, 2000)) {
      kickTimer = millis();
    }
  }

  if (checkTimer(kickTimer, 3)) {
    //dribbler(-255);
  } else if (checkTimer(kickTimer, 100)) {
    digitalWrite(KICKER, HIGH);
  } else {
    digitalWrite(KICKER, LOW);
  }
}

void updateCam() {
  ball.found = false;
  home.found = false;
  target.found = false;

  pixy.ccc.getBlocks(false, CCC_SIG1 | CCC_SIG2 | CCC_SIG3);
  if (pixy.ccc.numBlocks) {
    for (int i = 0; i < pixy.ccc.numBlocks; ++i) {
      if (pixy.ccc.blocks[i].m_signature == 1) {
        ball.x = pixy.ccc.blocks[i].m_x;
        ball.y = pixy.ccc.blocks[i].m_y;
        ball.w = pixy.ccc.blocks[i].m_width;
        ball.h = pixy.ccc.blocks[i].m_height;

        ball.dir = calcAngle(ball.x, ball.y);
        ball.dist = calcDist(ball.x, ball.y);

        ball.found = true;
        ball.t = millis();
      }
      if (pixy.ccc.blocks[i].m_signature == HOME_GOAL) {
        home.x = pixy.ccc.blocks[i].m_x;
        home.y = pixy.ccc.blocks[i].m_y;
        home.w = pixy.ccc.blocks[i].m_width;
        home.h = pixy.ccc.blocks[i].m_height;

        home.dir = calcAngle(home.x, home.y);
        home.dist = calcDist(home.x, home.y);

        home.found = true;
        home.t = millis();
      }
      if (pixy.ccc.blocks[i].m_signature == ENEMY_GOAL) {
        target.x = pixy.ccc.blocks[i].m_x;
        target.y = pixy.ccc.blocks[i].m_y;
        target.w = pixy.ccc.blocks[i].m_width;
        target.h = pixy.ccc.blocks[i].m_height;

        target.dir = calcAngle(target.x, target.y);
        target.dist = calcDist(target.x, target.y);

        target.found = true;
        target.t = millis();
      }
    }
  }

  pixy.setLED(0, ball.found * 255, 0);

  ball.found = checkTimer(ball.t, 600);
  home.found = checkTimer(home.t, 300);
  target.found = checkTimer(target.t, 300);
}

void setLED(bool l = left_led, bool c = center_led, bool r = right_led) {
  digitalWrite(LEFT_LED, l);
  digitalWrite(CENTER_LED, c);
  digitalWrite(RIGHT_LED, r);
}

void setSpeed(byte port, short motor_speed) {
  if (motor_speed > 255)
    motor_speed = 255;
  else if (motor_speed < -255)
    motor_speed = -255;

  analogWrite(motors_pwm[port], abs(motor_speed));
  digitalWrite(motors_in1[port], (motor_speed >= 0));
  digitalWrite(motors_in2[port], !(motor_speed >= 0));
}

void move() {
  int u = -heading * k;
  if (!ballSensor) {
    u = min(50, max(-50, u));
  } else {
    u = min(100, max(-100, u));
  }
  setSpeed(MOTOR_A,  speed * cos((dir + 0.785398163397448)) + u);
  setSpeed(MOTOR_B,  -speed * cos((dir - 0.785398163397448)) + u);
  setSpeed(MOTOR_C,  -speed * cos((dir + 0.785398163397448)) + u);
  setSpeed(MOTOR_D,  speed * cos((dir - 0.785398163397448)) + u);
}


void updateGyro() {
  if (!dmpReady) return;

  while (!mpuInterrupt && fifoCount < packetSize) {
    if (mpuInterrupt && fifoCount < packetSize) {
      fifoCount = mpu.getFIFOCount();
    }
  }
  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();
  fifoCount = mpu.getFIFOCount();
  if ((mpuIntStatus & _BV(MPU6050_INTERRUPT_FIFO_OFLOW_BIT)) || fifoCount >= 1024) {
    mpu.resetFIFO();
    fifoCount = mpu.getFIFOCount();
    Serial.println(F("FIFO overflow!"));
  } else if (mpuIntStatus & _BV(MPU6050_INTERRUPT_DMP_INT_BIT)) {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

    if (left_btn && right_btn) {
      yaw_offset = ypr[0];
      center_led = true;
    }
    yaw = constrainAngle(ypr[0] - yaw_offset);
  }
}

void setup() {
  pinMode(LEFT_LED, OUTPUT);
  pinMode(CENTER_LED, OUTPUT);
  pinMode(RIGHT_LED, OUTPUT);
  setLED(0, 1, 0);

  pinMode(L_STANDBY, OUTPUT);
  digitalWrite(L_STANDBY, HIGH);
  pinMode(R_STANDBY, OUTPUT);
  digitalWrite(R_STANDBY, HIGH);
  for (int i = 0; i < 4; ++i) {
    pinMode(motors_pwm[i], OUTPUT);
    pinMode(motors_in1[i], OUTPUT);
    pinMode(motors_in2[i], OUTPUT);
  }

  pinMode(LEFT_BTN, INPUT_PULLUP);
  pinMode(CENTER_BTN, INPUT_PULLUP);
  pinMode(RIGHT_BTN, INPUT_PULLUP);

  Serial.begin(9600);

#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
  Wire.begin();
  // Wire.setClock(400000); // 400kHz I2C clock. Comment this line if having compilation difficulties
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
  Fastwire::setup(400, true);
#endif

  mpu.initialize();
  pinMode(INTERRUPT_PIN, INPUT);
  Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
  devStatus = mpu.dmpInitialize();
  mpu.setXGyroOffset(GYRO_OFFSET_XG);
  mpu.setYGyroOffset(GYRO_OFFSET_YG);
  mpu.setZGyroOffset(GYRO_OFFSET_ZG);
  mpu.setZAccelOffset(GYRO_OFFSET_ZA);

  if (devStatus == 0) {
    mpu.setDMPEnabled(true);
    Serial.print(digitalPinToInterrupt(INTERRUPT_PIN));
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    dmpReady = true;
    packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
    Serial.print(devStatus);
  }

  pixy.init();
  pixy.setLED(0, 0, 0);
  pinMode(DRIBBLER_SPEED, OUTPUT);
  pinMode(DRIBBLER_DIR, OUTPUT);
  pinMode(KICKER, OUTPUT);
  pinMode(BALL_SENSOR, INPUT);

  setLED(0, 0, 0);
}

void loop() {
  k = BASIC_K;
  left_led = false;
  center_led = false;
  right_led = false;
  kick(false);
  updateBtns();
  updateCam();
  updateBallSensor();
  updateGyro();

  speed = BASIC_SPEED;
  dir = ball.dir;
  heading = ball.dir;
  if (!ball.found && ballSensor) {
    heading = 0;
    speed = 0;
  }

  if (!ballSensor) {
    dribbler(BASIC_DRIBBLER_SPEED);

    if (ball.x > 190 && ball.x < 215 && ball.y > 100 && ball.y < 140 && !checkTimer(ball.t, 300)) {
      speed = 70;
      dir = target.dir;
      if (target.dist > 78) {
        heading = constrainAngle(PI + target.dir);
      } else {
        heading = target.dir;
        if (abs(heading) < 0.1) {
          if (!checkTimer(targetClear, 100))
            kick(true);
        } else {
          targetClear = millis();
        }
      }
    }
  } else {
    dribbler(SNOOZE_DRIBBLER_SPEED);
  }

  Serial.print("X: ");
  Serial.print(ball.x);
  Serial.print(" Y: ");
  Serial.println(ball.y);

  setLED();
  move();
}
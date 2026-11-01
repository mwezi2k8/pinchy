/*
  Pinchy - Receiver (Arduino Uno)
  Receives joystick packets from the Nano transmitter over UART
  and drives four MG90S servos.

  Packet from Tx (9600 baud):  <a1,a2,a3,a4>\r\n
    each value is -255..+255, 0 when the joystick is at rest (deadband done on Tx)

  Mapping (matches the block diagram):
    a1 = Joystick 1 X -> Servo 1 Base      -> D3
    a2 = Joystick 1 Y -> Servo 2 Shoulder  -> D5
    a3 = Joystick 2 X -> Servo 3 Elbow     -> D6
    a4 = Joystick 2 Y -> Servo 4 Gripper   -> D9

  Wiring:
    Tx cable RED   (Nano D1 TX) -> Uno D0 (RX)
    Tx cable GREEN (Tx GND)     -> Uno GND
    Each servo: RED -> its own LM7805 output, BROWN -> common Rx GND,
                ORANGE (signal) -> its Uno PWM pin above.

  NOTE: D0 is shared with the USB port. Disconnect the RED wire from D0
  while uploading, then reconnect it. The Serial Monitor can still show
  DEBUG output (Uno TX -> USB) while the Nano is connected to D0.

  Control modes:
    RATE_MODE 1 (recommended): joystick sets the SPEED of each joint.
      Release the stick and the joint stays where it is, so the arm
      holds its pose and the gripper stays closed on the cube.
    RATE_MODE 0: joystick sets the ANGLE directly (with smoothing).
      Releasing the stick sends the joint back to the middle of its range.

  Test / debug switches:
    SERVO_TEST 1 : ignores the Tx and slowly sweeps each servo, one at a
                   time, between its min and max. Use this to check each
                   servo on its own LM7805 branch and to find safe limits.
    DEBUG 1      : prints received values and servo angles at 9600 baud.
*/

#include <Servo.h>

// ---------- Switches ----------
#define SERVO_TEST 0
#define DEBUG      0
#define RATE_MODE  1

// ---------- Settings ----------
const long BAUD_RATE        = 9600;   // must match the Nano transmitter
const unsigned long UPDATE_MS  = 20;  // servo update period (50 Hz)
const unsigned long TIMEOUT_MS = 500; // no valid packet for this long -> hold position
const float MAX_SPEED_DEG_S = 90.0;   // RATE_MODE: joint speed at full stick deflection
const float SMOOTHING       = 0.15;   // ANGLE mode: 0..1, smaller = smoother/slower

// ---------- Servo configuration ----------
// Index:              Base  Shoulder  Elbow  Gripper
const uint8_t PIN[4]   = {  3,     5,      6,      9 };
const int MIN_ANG[4]   = {  0,    20,     20,     10 };  // calibrate on the real arm
const int MAX_ANG[4]   = {180,   160,    160,     80 };  // so no link hits a stop
const int START_ANG[4] = { 90,    90,     90,     45 };  // pose at power-up
const bool INVERT[4]   = {false, false, false, false};   // flip if a joint moves the wrong way

Servo servo[4];
float angle[4];        // current angle (float for smooth small steps)
int   lastWritten[4];  // last value sent to each servo

// ---------- Packet reception ----------
const byte BUF_LEN = 32;
char buf[BUF_LEN];
byte bufIdx = 0;
bool inPacket = false;

int cmd[4] = {0, 0, 0, 0};          // latest valid joystick values
unsigned long lastPacketMs = 0;
unsigned long goodPackets = 0, badPackets = 0;

// Parse "a1,a2,a3,a4" (without the < >). Returns true if valid.
bool parsePacket(char *s) {
  int v[4];
  char *p = s;
  for (int i = 0; i < 4; i++) {
    char *end;
    long n = strtol(p, &end, 10);
    if (end == p) return false;                 // no number found
    if (n < -255 || n > 255) return false;      // out of range -> corrupted
    v[i] = (int)n;
    if (i < 3) {
      if (*end != ',') return false;            // missing comma
      p = end + 1;
    } else if (*end != '\0') {
      return false;                             // junk after 4th value
    }
  }
  for (int i = 0; i < 4; i++) cmd[i] = v[i];
  return true;
}

// Non-blocking: read whatever bytes have arrived and assemble packets.
void readSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '<') {                 // start of packet (also resyncs after garbage)
      inPacket = true;
      bufIdx = 0;
    } else if (inPacket) {
      if (c == '>') {               // end of packet
        buf[bufIdx] = '\0';
        inPacket = false;
        if (parsePacket(buf)) { lastPacketMs = millis(); goodPackets++; }
        else badPackets++;
      } else if (bufIdx < BUF_LEN - 1) {
        buf[bufIdx++] = c;
      } else {                      // too long -> drop it
        inPacket = false;
        badPackets++;
      }
    }
    // characters outside < > (e.g. \r\n) are ignored
  }
}

// ---------- Servo control ----------
void writeServo(int i) {
  angle[i] = constrain(angle[i], MIN_ANG[i], MAX_ANG[i]);
  int a = (int)(angle[i] + 0.5);
  if (a != lastWritten[i]) {        // only write when the angle actually changes
    servo[i].write(a);
    lastWritten[i] = a;
  }
}

void updateServos(float dt) {
  // Link lost or never started: hold every joint where it is.
  if (lastPacketMs == 0 || millis() - lastPacketMs > TIMEOUT_MS) return;

  for (int i = 0; i < 4; i++) {
    int v = INVERT[i] ? -cmd[i] : cmd[i];
#if RATE_MODE
    angle[i] += (v / 255.0) * MAX_SPEED_DEG_S * dt;
#else
    float target = MIN_ANG[i] + (v + 255) * (MAX_ANG[i] - MIN_ANG[i]) / 510.0;
    angle[i] += (target - angle[i]) * SMOOTHING;
#endif
    writeServo(i);
  }
}

// ---------- Servo test (SERVO_TEST 1) ----------
void servoTest() {
  for (int i = 0; i < 4; i++) {
    Serial.print("Testing servo "); Serial.print(i + 1);
    Serial.print(" on D"); Serial.println(PIN[i]);
    for (int a = START_ANG[i]; a <= MAX_ANG[i]; a++) { servo[i].write(a); delay(20); }
    delay(400);
    for (int a = MAX_ANG[i]; a >= MIN_ANG[i]; a--)   { servo[i].write(a); delay(20); }
    delay(400);
    for (int a = MIN_ANG[i]; a <= START_ANG[i]; a++) { servo[i].write(a); delay(20); }
    delay(800);
  }
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(BAUD_RATE);
  for (int i = 0; i < 4; i++) {
    angle[i] = START_ANG[i];
    lastWritten[i] = START_ANG[i];
    servo[i].write(START_ANG[i]);   // set position before attach -> no wild jump
    servo[i].attach(PIN[i]);
    delay(200);                     // stagger start-up current draw
  }
#if DEBUG
  Serial.println("Rx ready");
#endif
}

void loop() {
#if SERVO_TEST
  servoTest();
  return;
#endif

  readSerial();

  static unsigned long lastUpdate = 0;
  unsigned long now = millis();
  if (now - lastUpdate >= UPDATE_MS) {
    float dt = (now - lastUpdate) / 1000.0;
    lastUpdate = now;
    updateServos(dt);
  }

#if DEBUG
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 200) {
    lastPrint = now;
    bool linkOk = lastPacketMs != 0 && now - lastPacketMs <= TIMEOUT_MS;
    Serial.print(linkOk ? "LINK OK  " : "NO LINK  ");
    Serial.print("cmd: ");
    for (int i = 0; i < 4; i++) { Serial.print(cmd[i]); Serial.print(i < 3 ? "," : "  "); }
    Serial.print("ang: ");
    for (int i = 0; i < 4; i++) { Serial.print(lastWritten[i]); Serial.print(i < 3 ? "," : "  "); }
    Serial.print("good/bad: "); Serial.print(goodPackets);
    Serial.print("/"); Serial.println(badPackets);
  }
#endif
}

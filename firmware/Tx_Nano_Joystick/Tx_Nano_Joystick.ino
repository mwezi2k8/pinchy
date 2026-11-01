/*
  Pinchy - Transmitter (Arduino Nano)
  Reads two HW504 joysticks and sends <a1,a2,a3,a4> to the Uno over UART.
  Each value is -255..+255, 0 when the stick is at rest.
*/

// ---------- Pins ----------
const int PIN_VRX1 = A0;   // Joystick 1, X axis -> base
const int PIN_VRY1 = A1;   // Joystick 1, Y axis -> shoulder
const int PIN_VRX2 = A2;   // Joystick 2, X axis -> elbow
const int PIN_VRY2 = A3;   // Joystick 2, Y axis -> gripper

// ---------- Settings ----------
const long BAUD_RATE  = 9600;  // must match the Uno receiver
const int  DEADBAND   = 30;    // |value| below this is sent as 0
const int  SEND_DELAY = 50;    // ms between packets (~20 packets/s)

// Convert analogRead() value (0..1023) to -255..+255
// and apply the deadband around the joystick centre.
int convert(int raw) {
  int value = map(raw, 0, 1023, -255, 255);
  if (abs(value) < DEADBAND) value = 0;   // joystick at rest sends 0
  return value;
}

void setup() {
  Serial.begin(BAUD_RATE);
  // Joystick push-switches (D2, D3) are not used in this design.
}

void loop() {
  int a1 = convert(analogRead(PIN_VRX1));
  int a2 = convert(analogRead(PIN_VRY1));
  int a3 = convert(analogRead(PIN_VRX2));
  int a4 = convert(analogRead(PIN_VRY2));

  Serial.print('<');
  Serial.print(a1); Serial.print(',');
  Serial.print(a2); Serial.print(',');
  Serial.print(a3); Serial.print(',');
  Serial.print(a4);
  Serial.println('>');

  delay(SEND_DELAY);
}

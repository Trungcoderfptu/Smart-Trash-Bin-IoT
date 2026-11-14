#include <Servo.h>
#include <LiquidCrystal_I2C.h>

// ==========================================
// 1. KHAI BAO CHAN
// ==========================================

const int trigLid = 2;
const int echoLid = 3;

const int trigTrash = 4;
const int echoTrash = 5;

const int buzzerPin = 7;
const int ledRed = 8;
const int servoPin = 9;

const int ledWhiteOut = 10;
const int ledWhiteIn = 11;

const int ldrPin = A1;

// ==========================================
// 2. KHOI TAO
// ==========================================

Servo lidServo;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ==========================================
// 3. CAU HINH
// ==========================================
int LID_CLOSED_ANGLE = 90;
int LID_OPEN_ANGLE = 180;

const int EMPTY_TRASH_DIST = 18;      // chieu cao thung rac max
const int FULL_TRASH_DIST = 2;        // chieu cao thung rac min
const int TRASH_EMPTY_TOLERANCE = 1;  // Sai so cho phep khi thung rong

const int detectPersonDist = 25;   // khoang cach nguoi den gan min
const int personReleaseDist = 30;  // khoang cach nguoi den gan max

const int PERSON_DETECT_COUNT = 2;
const int PERSON_CLEAR_COUNT = 4;

const int darkOn = 200;   //muc nhay cua cam bien anh sang min
const int darkOff = 250;  // muc nhay cua cam bien anh sang max

const int fullOn = 90;
const int fullOff = 85;

const unsigned long closeDelay = 2000;      // thoi gian dong khi khong co nguoi
const unsigned long unlockTimeout = 3000;   // Thoi gain thung rac khoa sau khi dong
const unsigned long reopenLockTime = 1000;  // khi khong co nguoi khoa chuc nang mo nap x mili seccon

const unsigned long sensorInterval = 80;
const unsigned long ldrInterval = 100;
const unsigned long buzzerInterval = 300;
const unsigned long lcdInterval = 150;
const unsigned long debugInterval = 1000;

const unsigned long personInvalidTimeout = 1200;

const unsigned long lcdReinitDelay = 300;
const unsigned long lcdReinitInterval = 10000;

// ==========================================
// 4. BIEN HE THONG
// ==========================================

bool isLidOpen = false;
bool personDetected = false;

bool waitingToClose = false;
bool allowOpen = true;

bool isTrashFull = false;
bool isDark = false;

int mode = 0;

int personCount = 0;
int personClearCount = 0;

int trashPercent = 0;

int ldrValue = 0;

int personDistance = 999;
int filteredPersonDistance = 999;

int trashDistance = 999;
int trashDistanceFiltered = EMPTY_TRASH_DIST;

int personSamples[3] = { 999, 999, 999 };

int sampleIndex = 0;
int sensorTurn = 0;

unsigned long personLeftTime = 0;
unsigned long lastLidClosedTime = 0;
unsigned long lastPersonValidTime = 0;

unsigned long lastSensorTime = 0;
unsigned long lastLdrTime = 0;
unsigned long lastBuzzerTime = 0;
unsigned long lastLcdTime = 0;
unsigned long lastDebugTime = 0;

bool buzzerState = false;
bool lcdNeedReinit = false;
unsigned long lcdReinitTime = 0;
unsigned long lastLcdReinit = 0;

// ==========================================
// 5. HAM DOC CAM BIEN KHOANG CACH SIEU AM
// ==========================================
int readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 25000);
  if (duration == 0) {
    return 999;
  }
  int distance = duration * 0.034 / 2;
  if (distance < 2 || distance > 400) {
    return 999;
  }
  return distance;
}

// ==========================================
// 6. HAM LOC NHIEU TRUNG VI 3 LAN DO
// ==========================================
int getMedianDistance(int a, int b, int c) {
  if (a > b) {
    int temp = a;
    a = b;
    b = temp;
  }
  if (b > c) {
    int temp = b;
    b = c;
    c = temp;
  }
  if (a > b) {
    int temp = a;
    a = b;
    b = temp;
  }
  return b;
}

// ==========================================
// 7. HAM KHOI TAO LAI LCD (PHUC HOI I2C)
// ==========================================
void reinitLCD() {
  lcd.init();
  lcd.backlight();
  lastLcdTime = 0;
}
void requestLcdReinit() {
  lcdNeedReinit = true;
  lcdReinitTime = millis();
}

// ==========================================
// 8. HAM MO NAP THUNG RAC
// ==========================================
void openLid() {

  if (isLidOpen || !allowOpen) {
    return;
  }
  lidServo.write(LID_OPEN_ANGLE);
  digitalWrite(ledWhiteIn, HIGH);
  isLidOpen = true;
  waitingToClose = false;
  requestLcdReinit();
  Serial.println("LID OPENED");
}

// ==========================================
// 8B. HAM RESET TRANG THAI NGUOI
// ==========================================
void resetPersonState() {
  personDetected = false;
  personCount = 0;
  personClearCount = 0;
}

// ==========================================
// 9. HAM DONG NAP THUNG RAC
// ==========================================
void closeLid() {
  if (!isLidOpen) {
    return;
  }
  lidServo.write(LID_CLOSED_ANGLE);
  digitalWrite(ledWhiteIn, LOW);
  isLidOpen = false;
  waitingToClose = false;
  allowOpen = false;
  lastLidClosedTime = millis();

  resetPersonState();

  personSamples[0] = 999;
  personSamples[1] = 999;
  personSamples[2] = 999;
  requestLcdReinit();
  Serial.println("LID CLOSED - LOCKED");
}

// ==========================================
// 10A. HAM XU LY KHI NAP DANG BI KHOA
// ==========================================
void processLockedState(int distance, unsigned long now) {
  if (distance < 999) {
    personDistance = distance;
  }
  bool clearNow = (distance >= 999) || (distance >= personReleaseDist);
  if (clearNow) {
    personClearCount++;
  } else {
    personClearCount = 0;
  }
  if (personClearCount >= PERSON_CLEAR_COUNT || now - lastLidClosedTime >= unlockTimeout) {
    allowOpen = true;
    resetPersonState();
    for (int i = 0; i < 3; i++) {
      personSamples[i] = distance;
    }
    filteredPersonDistance = distance;
    lastPersonValidTime = now;
    Serial.println("SENSOR CLEAR - OPEN ENABLED");
  }
}

// ==========================================
// 10B. HAM XU LY CHONG RUNG PHAT HIEN NGUOI
// ==========================================
void evaluatePersonPresence(int filteredDist) {
  if (filteredDist <= detectPersonDist) {
    personCount++;
    personClearCount = 0;
    if (personCount >= PERSON_DETECT_COUNT) {
      if (!personDetected) {
        Serial.println("PERSON DETECTED");
      }
      personDetected = true;
    }
  } else if (filteredDist >= personReleaseDist) {
    personClearCount++;
    personCount = 0;
    if (personClearCount >= PERSON_CLEAR_COUNT) {
      if (personDetected) {
        Serial.println("PERSON LEFT");
      }
      personDetected = false;
    }
  }
}

// ==========================================
// 10. HAM DOC CAM BIEN NGUOI
// ==========================================
void readPersonDistance() {
  int distance = readDistance(trigLid, echoLid);
  unsigned long now = millis();

  if (!allowOpen) {
    processLockedState(distance, now);
    return;
  }

  if (distance < 3 || distance >= 999) {
    if (now - lastPersonValidTime >= personInvalidTimeout) {
      resetPersonState();
    }
    return;
  }

  personDistance = distance;
  lastPersonValidTime = now;
  personSamples[sampleIndex] = distance;
  sampleIndex++;

  if (sampleIndex >= 3) {
    sampleIndex = 0;
  }

  for (int i = 0; i < 3; i++) {
    if (personSamples[i] == 999) {
      filteredPersonDistance = 999;
      return;
    }
  }

  filteredPersonDistance = getMedianDistance(
    personSamples[0],
    personSamples[1],
    personSamples[2]);

  evaluatePersonPresence(filteredPersonDistance);
}

// ==========================================
// 11. HAM DOC CAM BIEN DO MUC RAC
// ==========================================
void readTrashDistance() {
  int distance = readDistance(trigTrash, echoTrash);
  if (distance >= 999) {
    return;
  }
  trashDistance = distance;
  if (distance >= EMPTY_TRASH_DIST - TRASH_EMPTY_TOLERANCE) {
    trashDistanceFiltered = EMPTY_TRASH_DIST;
    trashPercent = 0;
  } else {
    distance = constrain(distance, FULL_TRASH_DIST, EMPTY_TRASH_DIST);
    trashDistanceFiltered =
      (trashDistanceFiltered + distance) / 2;
    trashPercent = map(
      trashDistanceFiltered,
      EMPTY_TRASH_DIST,
      FULL_TRASH_DIST,
      0,
      100);

    trashPercent = constrain(trashPercent, 0, 100);
  }
  if (!isTrashFull && trashPercent >= fullOn) {
    isTrashFull = true;
    Serial.println("TRASH FULL");
  }
  if (isTrashFull && trashPercent <= fullOff) {
    isTrashFull = false;
    Serial.println("TRASH NOT FULL");
  }
}

// ==========================================
// 12. HAM XU LY DONG MO NAP TU DONG
// ==========================================
void handleNormalMode(unsigned long now) {
  if (personDetected && allowOpen) {
    waitingToClose = false;
    if (!isLidOpen && now - lastLidClosedTime >= reopenLockTime) {
      openLid();
    }
    return;
  }
  if (isLidOpen) {
    if (!waitingToClose) {
      personLeftTime = now;
      waitingToClose = true;
      Serial.println("WAITING TO CLOSE");
    }
    if (now - personLeftTime >= closeDelay) {
      closeLid();
    }
  } else {
    waitingToClose = false;
    lidServo.write(LID_CLOSED_ANGLE);
  }
}

// ==========================================
//12b. CHINH GOC SERVO QUA SERIAL MONITOR
// ==========================================
void printAngles() {
  Serial.print("LID_CLOSED_ANGLE = ");
  Serial.print(LID_CLOSED_ANGLE);
  Serial.print(" | LID_OPEN_ANGLE = ");
  Serial.println(LID_OPEN_ANGLE);
}

void handleSerialTuning() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    bool changed = false;
    if (c == '+') {
      LID_CLOSED_ANGLE = constrain(LID_CLOSED_ANGLE + 2, 0, 180);
      changed = true;
    } else if (c == '-') {
      LID_CLOSED_ANGLE = constrain(LID_CLOSED_ANGLE - 2, 0, 180);
      changed = true;
    } else if (c == 'o') {
      LID_OPEN_ANGLE = constrain(LID_OPEN_ANGLE + 5, 0, 180);
      changed = true;
    } else if (c == 'l') {
      LID_OPEN_ANGLE = constrain(LID_OPEN_ANGLE - 5, 0, 180);
      changed = true;
    }
    if (changed) {
      lidServo.write(isLidOpen ? LID_OPEN_ANGLE : LID_CLOSED_ANGLE);
      printAngles();
    }
  }
}

// ==========================================
// 13. HAM DOC CAM BIEN ANH SANG LDR
// ==========================================
void readLDR() {
  ldrValue = analogRead(ldrPin);
  if (!isDark && ldrValue < darkOn) {
    isDark = true;
  }
  if (isDark && ldrValue > darkOff) {
    isDark = false;
  }
  digitalWrite(ledWhiteOut, isDark ? HIGH : LOW);
}

// ==========================================
// 14. HAM CAP NHAT CHE DO HOAT DONG
// ==========================================
void updateMode() {
  int oldMode = mode;
  if (isTrashFull) {
    mode = 1;
  } else {
    mode = 0;
  }
  if (oldMode != mode) {
    Serial.print("MODE CHANGED: ");
    Serial.println(mode);
  }
}

// ==========================================
// 15. HAM DIEU KHIEN COI VA DEN BAO DAY RAC
// ==========================================
void handleBuzzer(unsigned long now) {
  if (mode == 1) {
    if (now - lastBuzzerTime >= buzzerInterval) {
      lastBuzzerTime = now;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState);
      digitalWrite(ledRed, buzzerState);
    }
  } else {
    buzzerState = false;
    digitalWrite(buzzerPin, LOW);
    digitalWrite(ledRed, LOW);
  }
}

// ==========================================
// 16. HAM HIEN THI THONG TIN LEN LCD
// ==========================================
void updateLCD(unsigned long now) {
  if (lcdNeedReinit && now - lcdReinitTime >= lcdReinitDelay) {
    lcdNeedReinit = false;
    lastLcdReinit = now;
    reinitLCD();
  }
  if (now - lastLcdReinit >= lcdReinitInterval) {
    lastLcdReinit = now;
    reinitLCD();
  }
  if (now - lastLcdTime < lcdInterval) {
    return;
  }
  lastLcdTime = now;
  lcd.setCursor(0, 0);
  if (mode == 1) {
    lcd.print("FULL! R:");
  } else {
    lcd.print("NORM  R:");
  }
  lcd.print(trashPercent);
  lcd.print("%    ");

  lcd.setCursor(0, 1);
  if (isLidOpen) {
    lcd.print("Lid:OPN ");
  } else {
    lcd.print("Lid:CLS ");
  }
  if (!allowOpen) {
    lcd.print("Wait    ");
  } else if (personDetected) {
    lcd.print("Detect  ");
  } else {
    lcd.print("Ready   ");
  }
}

// ==========================================
// 17. HAM XUAT THONG TIN SERIAL DEBUG
// ==========================================
void debugSerial(unsigned long now) {
  if (now - lastDebugTime < debugInterval) {
    return;
  }
  lastDebugTime = now;
  Serial.print("Mode: ");
  Serial.print(mode);
  Serial.print(" | TrashDist: ");
  Serial.print(trashDistance);
  Serial.print(" | Trash%: ");
  Serial.print(trashPercent);
  Serial.print(" | PersonDist: ");
  Serial.print(personDistance);
  Serial.print(" | Filtered: ");
  Serial.print(filteredPersonDistance);
  Serial.print(" | Person: ");
  Serial.print(personDetected ? "YES" : "NO");
  Serial.print(" | AllowOpen: ");
  Serial.print(allowOpen ? "YES" : "NO");
  Serial.print(" | Lid: ");
  Serial.println(isLidOpen ? "OPEN" : "CLOSED");
}

// ==========================================
// 18. HAM KHOI TAO HE THONG (SETUP)
// ==========================================
void setup() {
  Serial.begin(9600);
  pinMode(trigLid, OUTPUT);
  pinMode(echoLid, INPUT);
  pinMode(trigTrash, OUTPUT);
  pinMode(echoTrash, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(ledWhiteOut, OUTPUT);
  pinMode(ledWhiteIn, OUTPUT);
  pinMode(ldrPin, INPUT);
  lidServo.attach(servoPin);
  lidServo.write(LID_CLOSED_ANGLE);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("SMART TRASH BIN ");
  lcd.setCursor(0, 1);
  lcd.print("STARTING...     ");
  digitalWrite(buzzerPin, LOW);
  digitalWrite(ledRed, LOW);
  digitalWrite(ledWhiteOut, LOW);
  digitalWrite(ledWhiteIn, LOW);
  lastPersonValidTime = millis();
  delay(1000);
  lcd.clear();
  lastLcdReinit = millis();
  Serial.println("SMART TRASH BIN STARTED");
  Serial.println("Chinh goc nap: '+'/'-' = goc dong, 'o'/'l' = goc mo");
  printAngles();
}

// ==========================================
// 19. HAM VONG LAP CHINH (LOOP)
// ==========================================
void loop() {
  unsigned long now = millis();
  handleSerialTuning();  // [SUA] chinh goc servo qua Serial
  if (now - lastSensorTime >= sensorInterval) {
    lastSensorTime = now;
    if (sensorTurn == 0) {
      readPersonDistance();
      sensorTurn = 1;
    } else {
      readTrashDistance();
      sensorTurn = 0;
    }
  }
  if (now - lastLdrTime >= ldrInterval) {
    lastLdrTime = now;
    readLDR();
  }
  updateMode();
  handleNormalMode(now);
  handleBuzzer(now);
  updateLCD(now);
  debugSerial(now);
}

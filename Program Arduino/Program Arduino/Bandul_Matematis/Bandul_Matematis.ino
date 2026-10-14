#include <LiquidCrystal.h>

struct Button {
  const int pin;
  int state;
  int lastState;
  unsigned long lastDebounceTime;
};
Button reset      = {2, HIGH, HIGH, 0};
Button mode       = {25, HIGH, HIGH, 0};
Button up         = {22, HIGH, HIGH, 0};
Button down       = {23, HIGH, HIGH, 0};
Button startStop  = {27, HIGH, HIGH, 0};

const unsigned long debounceDelay = 50;
// LCD pin: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(A7, A6, A3, A2, A1, A0);

// fungsi debounce button
bool isButtonPressed(Button &btn) {
  int reading = digitalRead(btn.pin);//membaca nilai pin dari button yang di tekan
  bool pressed = false;

  if (reading != btn.lastState){
    btn.lastDebounceTime = millis();
  }
  // jika sinyal sudah melebihi durasi debounce
  if ((millis() - btn.lastDebounceTime) > debounceDelay){
    if (reading != btn.state) {
      btn.state = reading;

      // kembalikan true saat tombol baru selesai ditekan
      if (btn.state == LOW) {
        pressed = true;
      }
    }
  }
  btn.lastState = reading;
  return pressed;// mengembalikan nilai bool dari pressed
}

//variable mode
const byte BM_Mode; //mode untuk bandul matematis
const byte AM_Mode;// mode untuk pesawat atwood(atwood machine)


// Pin pengatur
const byte photogatePin = 2;  // Gunakan pin interrupt INT4 (Arduino Mega)


// Variabel pencatatan waktu
volatile unsigned long times[50];  // Menyimpan waktu relatif dalam ms
volatile unsigned long startTime = 0;
volatile int timeIndex = 0;
volatile bool newData = false;


int scrollIndex = 0;
bool waitingToStart = true;   // Tambahkan flag awal
bool menuStatus = true;
bool atwoodStatus = false;
bool pendulumMode = false;

void mainMenu() { 

  }

void setup() {
  lcd.begin(20, 4);
  lcd.setCursor(8,1);
  lcd.print("Bandul");
  lcd.setCursor(6,2);
  lcd.print("Matematis");
  delay(6000);
  lcd.clear();
  lcd.setCursor(4,1);
  lcd.print("System Ready");
  delay(3000);
  lcd.clear();

  lcd.setCursor(2, 1);
  lcd.print("Mulai mengayun!");

  // Setup pin
  pinMode(photogatePin, INPUT);
  attachInterrupt(digitalPinToInterrupt(photogatePin), recordTime, CHANGE);
  
  //setup pin button
  pinMode(startStop.pin, INPUT_PULLUP);
  pinMode(up.pin, INPUT_PULLUP);
  pinMode(down.pin, INPUT_PULLUP);
  pinMode(reset.pin, INPUT_PULLUP);
  pinMode(mode.pin, INPUT_PULLUP);
}

void loop() {
  if (mainMenu == true) {
    mainMenu();

  }
  // Tampilkan "Mulai mengayun" hanya jika belum mulai
  if (waitingToStart && timeIndex == 0) {
    lcd.setCursor(2, 1);
    lcd.print("Mulai mengayun!");
  }

  if (newData) {
    newData = false;
    displayTimes();
  }

  // Scroll UP
  if (digitalRead(scrollUpPin) == LOW) {
    if (scrollIndex > 0) {
      scrollIndex--;
      displayTimes();
    }
    delay(200); // Debounce
  }

  // Scroll DOWN
  if (digitalRead(scrollDownPin) == LOW) {
    if (scrollIndex < timeIndex - 4) {
      scrollIndex++;
      displayTimes();
    }
    delay(200); // Debounce
  }

  // Reset data kapan saja jika tombol ditekan
  if (digitalRead(resetPin) == LOW) {
    resetSystem();
    delay(500);  // Debounce panjang untuk tombol reset
  }
}

void recordTime() {
  unsigned long currentMillis = millis();

  if (timeIndex == 0) {
    startTime = currentMillis;
    waitingToStart = false;  // Hilangkan pesan "Mulai mengayun"
    lcd.clear();
  }

  if (timeIndex < 50) {
    times[timeIndex] = currentMillis - startTime;
    timeIndex++;
    newData = true;
  }
}

void displayTimes() {
  lcd.clear();
  for (int i = 0; i < 4; i++) {
    int idx = scrollIndex + i;
    if (idx < timeIndex) {
      lcd.setCursor(0, i);
      lcd.print(idx + 1);
      lcd.print(": ");
      lcd.print(times[idx]);
      lcd.print(" ms");
    }
  }
}

void resetSystem() {
  // Reset semua variabel pencatatan
  for (int i = 0; i < 50; i++) {
    times[i] = 0;
  }

  timeIndex = 0;
  scrollIndex = 0;
  startTime = 0;
  newData = true;
  waitingToStart = true;  // Kembalikan status ke awal

  lcd.clear();
  lcd.setCursor(4, 1);
  lcd.print("Data Direset");
  delay(3000);
  lcd.clear();

  // Tampilkan kembali pesan "Mulai mengayun"
  lcd.setCursor(2, 1);
  lcd.print("Mulai mengayun!");
}


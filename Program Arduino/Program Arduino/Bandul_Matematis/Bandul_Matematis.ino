#include <LiquidCrystal.h>
#include <PinChangeInterrupt.h>

// LCD pin: RS, E, D4, D5, D6, D7
LiquidCrystal lcd(A7, A6, A3, A2, A1, A0);

// Pin pengatur
const byte photogatePin = 2;  // Gunakan pin interrupt INT4 (Arduino Mega)
const int scrollUpPin = 22;
const int scrollDownPin = 23;
const int resetPin = 29;      // Tombol reset

// Variabel pencatatan waktu
volatile unsigned long times[50];  // Menyimpan waktu relatif dalam ms
volatile unsigned long startTime = 0;
volatile int timeIndex = 0;
volatile bool newData = false;

int scrollIndex = 0;
bool waitingToStart = true;   // Tambahkan flag awal

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
  attachInterrupt(digitalPinToInterrupt(photogatePin), recordTime, FALLING);

  pinMode(scrollUpPin, INPUT_PULLUP);
  pinMode(scrollDownPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
}

void loop() {
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


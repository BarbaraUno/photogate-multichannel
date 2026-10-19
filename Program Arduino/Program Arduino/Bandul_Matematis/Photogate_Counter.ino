#include <LiquidCrystal.h>

// ===== LCD: RS, E, D4, D5, D6, D7 =====
LiquidCrystal lcd(A7, A6, A3, A2, A1, A0);

// ===== Pin =====
// Photogate tetap di pin 2 (INT4 pada Arduino Mega)
const byte photogatePin = 2;
// Tombol reset DIPINDAH ke pin 27, karena pin 2 dipakai photogate
const byte resetPin = 29;

// ===== Variabel counting =====
volatile unsigned long counter = 0;
volatile unsigned long lastPulseTime = 0;   // untuk filter noise (us)
const unsigned long minPulseGap = 1000;     // abaikan pulsa < 1 ms

unsigned long lastShownCount = 0xFFFFFFFF;  // paksa tampil pertama kali

// ===== Debounce tombol reset =====
const unsigned long debounceDelay = 50;
int resetState = HIGH;
int resetLastState = HIGH;
unsigned long resetLastDebounce = 0;

// ISR: dipanggil tiap photogate terhalang
void countPulse() {
  unsigned long now = micros();
  if (now - lastPulseTime >= minPulseGap) {
    counter++;
    lastPulseTime = now;
  }
}

bool isResetPressed() {
  int reading = digitalRead(resetPin);
  bool pressed = false;

  if (reading != resetLastState) {
    resetLastDebounce = millis();
  }
  if ((millis() - resetLastDebounce) > debounceDelay) {
    if (reading != resetState) {
      resetState = reading;
      if (resetState == LOW) pressed = true;
    }
  }
  resetLastState = reading;
  return pressed;
}

void showCount(unsigned long c) {
  lcd.setCursor(0, 2);
  lcd.print("                    ");  // hapus baris
  lcd.setCursor(0, 2);
  lcd.print("Jumlah: ");
  lcd.print(c);
}

void setup() {
  lcd.begin(20, 4);
  lcd.setCursor(3, 0);
  lcd.print("PHOTOGATE COUNTER");
  lcd.setCursor(0, 1);
  lcd.print("--------------------");
  lcd.setCursor(0, 3);
  lcd.print("Tekan reset: nol");

  pinMode(photogatePin, INPUT);
  pinMode(resetPin, INPUT_PULLUP);

  // RISING = hitung 1x per halangan.
  // Kalau hitungannya terbalik/tidak jalan, ganti ke FALLING.
  attachInterrupt(digitalPinToInterrupt(photogatePin), countPulse, RISING);
}

void loop() {
  // Salin counter dengan aman (variabel 32-bit, bukan atomic di AVR)
  noInterrupts();
  unsigned long c = counter;
  interrupts();

  if (c != lastShownCount) {
    showCount(c);
    lastShownCount = c;
  }

  if (isResetPressed()) {
    noInterrupts();
    counter = 0;
    interrupts();
  }
}

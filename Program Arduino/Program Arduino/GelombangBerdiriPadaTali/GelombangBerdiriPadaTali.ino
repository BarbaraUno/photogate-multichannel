#include <MD_AD9833.h>
#include <SPI.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Pin AD9833
#define FSYNC 9
MD_AD9833 gen(FSYNC);

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Rotary Encoder
const int pinCLK = 2;   // CLK encoder
const int pinDT  = 3;   // DT encoder
const int pinSW  = 4;   // Tombol push encoder
long frequency = 0;     // frekuensi awal
int lastCLKState;
int lastSWState;

void setup() {
  // Rotary encoder
  pinMode(pinCLK, INPUT);
  pinMode(pinDT, INPUT);
  pinMode(pinSW, INPUT_PULLUP);  // tombol encoder dengan pull-up internal
  lastCLKState = digitalRead(pinCLK);
  lastSWState = digitalRead(pinSW);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(1, 0);
  lcd.print("Freq Generator");  

  // AD9833
  gen.begin();
  gen.setMode(MD_AD9833::MODE_SINE);
  gen.setFrequency(MD_AD9833::CHAN_0, frequency);
}

void loop() {
  // Rotary encoder untuk ubah frekuensi
  int currentCLKState = digitalRead(pinCLK);
  if (currentCLKState != lastCLKState && currentCLKState == HIGH) {
    if (digitalRead(pinDT) != currentCLKState) {
      frequency += 1;  // putar kanan, tambah frekuensi 1 Hz
    } else {
      frequency -= 1;  // putar kiri, kurangi frekuensi 1 Hz
    }
    // Batasi rentang 0–50 Hz
    if (frequency < 0) frequency = 0;
    if (frequency > 50) frequency = 50;

    gen.setFrequency(MD_AD9833::CHAN_0, frequency);

    // Update LCD
    lcd.setCursor(0, 1);
    lcd.print("Freq:");
    lcd.setCursor(6, 1);
    lcd.print(frequency);
    lcd.print(" Hz");
  }
  lastCLKState = currentCLKState;

  // Tombol push encoder untuk reset frekuensi
  int currentSWState = digitalRead(pinSW);
  if (currentSWState == LOW && lastSWState == HIGH) {
    frequency = 0;  // reset ke 0 Hz
    gen.setFrequency(MD_AD9833::CHAN_0, frequency);

    lcd.setCursor(0, 1);
    lcd.print("Freq:");
    lcd.setCursor(6, 1);
    lcd.print(frequency);
    lcd.print(" Hz");
  }
  lastSWState = currentSWState;
}

#include <LiquidCrystal.h>

// ================= LCD: RS, E, D4, D5, D6, D7 =================
LiquidCrystal lcd(A7, A6, A3, A2, A1, A0);

// ================= Tombol =================
struct Button {
  const byte pin;
  int state;
  int lastState;
  unsigned long lastDebounceTime;
};
Button btnUp    = {22, HIGH, HIGH, 0};
Button btnDown  = {23, HIGH, HIGH, 0};
Button btnMode  = {25, HIGH, HIGH, 0};
Button btnStart = {27, HIGH, HIGH, 0};
Button btnReset = {29, HIGH, HIGH, 0};

const unsigned long debounceDelay = 50;

// true hanya sekali saat tombol baru selesai ditekan
bool isButtonPressed(Button &btn) {
  int reading = digitalRead(btn.pin);
  bool pressed = false;

  if (reading != btn.lastState) {
    btn.lastDebounceTime = millis();
  }
  if ((millis() - btn.lastDebounceTime) > debounceDelay) {
    if (reading != btn.state) {
      btn.state = reading;
      if (btn.state == LOW) pressed = true;
    }
  }
  btn.lastState = reading;
  return pressed;
}

// ================= Photogate =================
const byte photogatePin = 2;  // ch5, INT4 (interrupt hardware, tanpa library)

// ================= Parameter pengukuran =================
const byte TOTAL_AYUNAN = 12;                    // total ayunan yang direkam
const byte BUANG        = 2;                     // ayunan awal yang dibuang
const byte UKUR         = TOTAL_AYUNAN - BUANG;  // 10 ayunan yang dihitung
const byte TOTAL_TS     = 2 * TOTAL_AYUNAN + 1;  // 25 titik waktu (2 lintasan/ayunan + titik awal)

const unsigned long MIN_GAP_US = 50000UL;  // abaikan deteksi < 50 ms (filter noise)
const float OUTLIER_TOL = 0.20;            // T dianggap outlier jika beda > 20% dari median

// ================= Data (diisi oleh ISR) =================
volatile unsigned long ts[TOTAL_TS];  // waktu absolut micros() tiap lintasan
volatile byte count = 0;
volatile bool armed = false;
volatile bool finished = false;
volatile unsigned long lastEdge = 0;

// ================= Hasil olahan =================
float Tk[UKUR];       // periode tiap ayunan (detik)
bool  outlier[UKUR];  // penanda outlier
float Tmean = 0;      // rata-rata T dari data valid
float Tsd = 0;        // simpangan baku dari data valid
float Ttotal = 0;     // T dari waktu total / jumlah ayunan (pembanding)
byte  nValid = 0;

// ================= Status tampilan =================
enum State { IDLE, MEASURING, DONE };
State state = IDLE;
byte page = 0;        // 0 = ringkasan hasil, 1 = daftar T per ayunan
int scrollIndex = 0;
bool needRedraw = true;
byte lastCount = 255;

// ================= ISR photogate =================
// Hanya menyimpan waktu. Tidak ada LCD / delay / Serial di sini.
void isrPhotogate() {
  if (!armed) return;

  unsigned long now = micros();
  if (count > 0 && (now - lastEdge) < MIN_GAP_US) return;  // filter noise

  ts[count] = now;
  count++;
  lastEdge = now;

  if (count >= TOTAL_TS) {
    armed = false;
    finished = true;
  }
}

// ================= Pengolahan data =================
void computeResults() {
  unsigned long t[TOTAL_TS];
  noInterrupts();
  for (byte i = 0; i < TOTAL_TS; i++) t[i] = ts[i];
  interrupts();

  // T tiap ayunan = selisih lintasan searah (n dan n+2), ayunan awal dibuang
  for (byte j = 0; j < UKUR; j++) {
    byte k = BUANG + j;
    unsigned long dt = t[2 * k + 2] - t[2 * k];  // aman walau micros() overflow
    Tk[j] = dt / 1000000.0;
  }

  // Median untuk mendeteksi outlier
  float sorted[UKUR];
  for (byte i = 0; i < UKUR; i++) sorted[i] = Tk[i];
  for (byte i = 1; i < UKUR; i++) {
    float key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }
  float median = (sorted[UKUR / 2 - 1] + sorted[UKUR / 2]) / 2.0;

  // Rata-rata dan simpangan baku dari data valid saja
  float sum = 0;
  nValid = 0;
  for (byte i = 0; i < UKUR; i++) {
    outlier[i] = (fabs(Tk[i] - median) > OUTLIER_TOL * median);
    if (!outlier[i]) {
      sum += Tk[i];
      nValid++;
    }
  }
  Tmean = (nValid > 0) ? sum / nValid : 0;

  float sq = 0;
  for (byte i = 0; i < UKUR; i++) {
    if (!outlier[i]) sq += (Tk[i] - Tmean) * (Tk[i] - Tmean);
  }
  Tsd = (nValid > 1) ? sqrt(sq / (nValid - 1)) : 0;

  // T dari waktu total (hanya bergantung pada dua titik waktu)
  unsigned long total = t[2 * TOTAL_AYUNAN] - t[2 * BUANG];
  Ttotal = (total / (float)UKUR) / 1000000.0;
}

// ================= Tampilan =================
void drawScreen() {
  lcd.clear();

  if (state == IDLE) {
    lcd.setCursor(2, 0);
    lcd.print("BANDUL MATEMATIS");
    lcd.setCursor(0, 1);
    lcd.print("Photogate: ch5");
    lcd.setCursor(0, 2);
    lcd.print("Tekan START untuk");
    lcd.setCursor(0, 3);
    lcd.print("mulai mengukur");
  }
  else if (state == MEASURING) {
    byte c = count;
    byte swings = (c >= 1) ? (c - 1) / 2 : 0;
    lcd.setCursor(0, 0);
    lcd.print("MENGUKUR...");
    lcd.setCursor(0, 1);
    lcd.print("Ayunan: ");
    lcd.print(swings);
    lcd.print("/");
    lcd.print(TOTAL_AYUNAN);
    lcd.setCursor(0, 2);
    if (c == 0) lcd.print("Ayunkan bandul");
    else if (swings < BUANG) lcd.print("(dibuang)");
    else lcd.print("(diukur)");
    lcd.setCursor(0, 3);
    lcd.print("START: batal");
  }
  else {  // DONE
    if (page == 0) {
      lcd.setCursor(0, 0);
      lcd.print("T rata2: ");
      lcd.print(Tmean, 4);
      lcd.print(" s");
      lcd.setCursor(0, 1);
      lcd.print("SD     : ");
      lcd.print(Tsd, 4);
      lcd.print(" s");
      lcd.setCursor(0, 2);
      lcd.print("Valid  : ");
      lcd.print(nValid);
      lcd.print("/");
      lcd.print(UKUR);
      lcd.setCursor(0, 3);
      lcd.print("T total: ");
      lcd.print(Ttotal, 4);
      lcd.print(" s");
    } else {
      for (byte row = 0; row < 4; row++) {
        int idx = scrollIndex + row;
        if (idx < UKUR) {
          lcd.setCursor(0, row);
          if (idx + 1 < 10) lcd.print(" ");
          lcd.print(idx + 1);
          lcd.print(": ");
          lcd.print(Tk[idx], 4);
          lcd.print(" s");
          if (outlier[idx]) lcd.print(" *");
        }
      }
    }
  }
}

// ================= Kontrol =================
void startMeasurement() {
  noInterrupts();
  count = 0;
  finished = false;
  armed = true;
  interrupts();

  lastCount = 255;
  state = MEASURING;
  needRedraw = true;
}

void cancelMeasurement() {
  noInterrupts();
  armed = false;
  interrupts();

  state = IDLE;
  needRedraw = true;
}

void resetSystem() {
  noInterrupts();
  armed = false;
  finished = false;
  count = 0;
  interrupts();

  nValid = 0;
  page = 0;
  scrollIndex = 0;
  state = IDLE;

  lcd.clear();
  lcd.setCursor(4, 1);
  lcd.print("Data Direset");
  delay(1000);
  needRedraw = true;
}

// ================= Setup & Loop =================
void setup() {
  lcd.begin(20, 4);
  lcd.setCursor(8, 1);
  lcd.print("Bandul");
  lcd.setCursor(6, 2);
  lcd.print("Matematis");
  delay(2000);
  lcd.clear();

  pinMode(btnUp.pin, INPUT_PULLUP);
  pinMode(btnDown.pin, INPUT_PULLUP);
  pinMode(btnMode.pin, INPUT_PULLUP);
  pinMode(btnStart.pin, INPUT_PULLUP);
  pinMode(btnReset.pin, INPUT_PULLUP);

  pinMode(photogatePin, INPUT_PULLUP);
  // FALLING = satu deteksi tiap lintasan. Kalau tidak terdeteksi, ganti ke RISING.
  attachInterrupt(digitalPinToInterrupt(photogatePin), isrPhotogate, FALLING);
}

void loop() {
  // Semua tombol dibaca tiap putaran supaya debounce tetap akurat
  bool pReset = isButtonPressed(btnReset);
  bool pStart = isButtonPressed(btnStart);
  bool pMode  = isButtonPressed(btnMode);
  bool pUp    = isButtonPressed(btnUp);
  bool pDown  = isButtonPressed(btnDown);

  if (pReset) {
    resetSystem();
  }
  else if (pStart) {
    if (state == MEASURING) cancelMeasurement();
    else startMeasurement();          // dari IDLE atau DONE
  }
  else if (state == DONE) {
    if (pMode) {
      page = (page == 0) ? 1 : 0;
      scrollIndex = 0;
      needRedraw = true;
    }
    if (page == 1) {
      if (pUp && scrollIndex > 0) {
        scrollIndex--;
        needRedraw = true;
      }
      if (pDown && scrollIndex < UKUR - 4) {
        scrollIndex++;
        needRedraw = true;
      }
    }
  }

  // Pantau pengukuran yang sedang berjalan
  if (state == MEASURING) {
    byte c = count;        // 1 byte, pembacaannya atomic
    bool fin = finished;

    if (c != lastCount) {
      lastCount = c;
      needRedraw = true;
    }
    if (fin) {
      noInterrupts();
      finished = false;
      interrupts();
      computeResults();
      state = DONE;
      page = 0;
      scrollIndex = 0;
      needRedraw = true;
    }
  }

  if (needRedraw) {
    needRedraw = false;
    drawScreen();
  }
}
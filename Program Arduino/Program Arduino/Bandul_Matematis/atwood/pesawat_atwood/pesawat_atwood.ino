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

bool isButtonPressed(Button &btn) {
  int reading = digitalRead(btn.pin);
  bool pressed = false;
  if (reading != btn.lastState) btn.lastDebounceTime = millis();
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
const byte photogatePin = 2;   // pin interrupt hardware (INT4)
const byte photogateCh  = 5;   // ch5 = pin 2

// ================= PARAMETER KATROL (ISI SESUAI DESAIN) =================
const byte  JUMLAH_CELAH   = 6;    // jumlah jari-jari/celah pada katrol
const float RADIUS_TALI_MM = 23.0;  // jari-jari efektif tempat tali melilit (mm),
                                    // diukur sampai pusat tali, BUKAN sampai ujung jari-jari
// Kalibrasi (disarankan): turunkan massa sejauh D mm, lihat jumlah pulsa P,
// lalu isi nilai ini = D / (P-1). Kalau > 0, nilai ini dipakai menggantikan hitungan di atas.
const float KALIBRASI_MM_PER_PULSA = 0.0;

// ================= Parameter pengukuran =================
const unsigned int MAKS_PULSA = 150;                // maksimum pulsa yang direkam
const byte BUANG_AWAL = 1;                          // pulsa awal tidak ikut perhitungan
const byte MIN_PULSA  = BUANG_AWAL + 5;             // minimum agar hasil dihitung
const unsigned long MIN_GAP_US = 1000UL;            // abaikan pulsa < 1 ms (noise)
const unsigned long TIMEOUT_US = 1000000UL;         // 1 s tanpa pulsa = gerakan selesai

float arcM;   // jarak lintasan massa per pulsa (meter)

// ================= Data (diisi ISR) =================
volatile unsigned long ts[MAKS_PULSA];
volatile unsigned int count = 0;
volatile bool armed = false;
volatile bool finished = false;
volatile unsigned long lastEdge = 0;

// ================= Hasil olahan =================
float tf[MAKS_PULSA];     // waktu tiap pulsa relatif terhadap pulsa pertama (detik)
unsigned int nPulse = 0;
bool okFit = false;
float aFit = 0;           // percepatan dari fit x-t (m/s2)
float vEnd = 0;           // kecepatan terakhir (m/s)
float resMM = 0;          // galat residu fit (mm)
float aVT = 0;            // percepatan dari fit v-t (pembanding)
float bedaPct = 0;        // selisih aFit dan aVT (%)
float Ttot = 0;           // waktu total (s)
float jarakCm = 0;        // jarak total (cm)

// ================= Status tampilan =================
enum State { IDLE, MEASURING, DONE };
State state = IDLE;
byte page = 0;            // 0 = utama, 1 = kualitas data, 2 = daftar pulsa
int scrollIndex = 0;
bool needRedraw = true;
unsigned int lastCount = 0xFFFF;
unsigned long lastDraw = 0;

// ================= ISR =================
// Hanya menyimpan waktu. Tanpa LCD / delay / Serial.
void isrEncoder() {
  if (!armed) return;
  unsigned long now = micros();
  if (count > 0 && (now - lastEdge) < MIN_GAP_US) return;

  ts[count] = now;
  count++;
  lastEdge = now;

  if (count >= MAKS_PULSA) {
    armed = false;
    finished = true;
  }
}

// ================= Pengolahan data =================
float det3(float a, float b, float c, float d, float e, float f,
           float g, float h, float k) {
  return a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
}

bool computeResults() {
  unsigned int n = nPulse;

  // Waktu relatif terhadap pulsa pertama
  noInterrupts();
  unsigned long t0 = ts[0];
  for (unsigned int i = 0; i < n; i++) tf[i] = (float)(ts[i] - t0) / 1000000.0;
  interrupts();

  unsigned int i0 = BUANG_AWAL;
  unsigned int m = n - i0;
  if (m < 5) return false;

  // Titik tengah dan skala waktu (supaya perhitungan stabil)
  float tmean = 0;
  for (unsigned int i = i0; i < n; i++) tmean += tf[i];
  tmean /= m;
  float s = max(fabs(tf[i0] - tmean), fabs(tf[n - 1] - tmean));
  if (s <= 0) return false;

  // Fit kuadratik: x = c0 + c1*u + c2*u^2, u = (t - tmean)/s
  float S0 = m, S1 = 0, S2 = 0, S3 = 0, S4 = 0, Y0 = 0, Y1 = 0, Y2 = 0;
  for (unsigned int i = i0; i < n; i++) {
    float u = (tf[i] - tmean) / s;
    float u2 = u * u;
    float x = i * arcM;
    S1 += u;  S2 += u2;  S3 += u2 * u;  S4 += u2 * u2;
    Y0 += x;  Y1 += x * u;  Y2 += x * u2;
  }
  float D = det3(S0, S1, S2, S1, S2, S3, S2, S3, S4);
  if (fabs(D) < 1e-6) return false;
  float c0 = det3(Y0, S1, S2, Y1, S2, S3, Y2, S3, S4) / D;
  float c1 = det3(S0, Y0, S2, S1, Y1, S3, S2, Y2, S4) / D;
  float c2 = det3(S0, S1, Y0, S1, S2, Y1, S2, S3, Y2) / D;

  aFit = 2.0 * c2 / (s * s);
  float uLast = (tf[n - 1] - tmean) / s;
  vEnd = (c1 + 2.0 * c2 * uLast) / s;

  // Residu fit
  float sq = 0;
  for (unsigned int i = i0; i < n; i++) {
    float u = (tf[i] - tmean) / s;
    float r = i * arcM - (c0 + c1 * u + c2 * u * u);
    sq += r * r;
  }
  resMM = sqrt(sq / (m - 3)) * 1000.0;

  // Pembanding: regresi linear v terhadap t (v tiap selang = arc / dt)
  float sT = 0, sV = 0, sTT = 0, sTV = 0;
  int k = 0;
  for (unsigned int i = i0 + 1; i < n; i++) {
    float dt = tf[i] - tf[i - 1];
    if (dt <= 0) continue;
    float tm = (tf[i] + tf[i - 1]) / 2.0 - tmean;
    float v = arcM / dt;
    sT += tm;  sV += v;  sTT += tm * tm;  sTV += tm * v;
    k++;
  }
  if (k < 3) return false;
  float den = k * sTT - sT * sT;
  if (fabs(den) < 1e-9) return false;
  aVT = (k * sTV - sT * sV) / den;
  bedaPct = (fabs(aFit) > 1e-6) ? (aVT - aFit) / aFit * 100.0 : 0;

  Ttot = tf[n - 1];
  jarakCm = (n - 1) * arcM * 100.0;
  return true;
}

// ================= Tampilan =================
// Cetak satu baris 20 karakter, sisanya diisi spasi (tanpa lcd.clear -> tidak berkedip)
void line(byte row, const char *s) {
  lcd.setCursor(0, row);
  byte n = 0;
  while (s[n] && n < 20) { lcd.write(s[n]); n++; }
  while (n < 20) { lcd.write(' '); n++; }
}

void drawScreen() {
  char buf[26], f1[14], f2[14];
  lastDraw = millis();

  if (state == IDLE) {
    line(0, "ATWOOD - ENCODER");
    snprintf(buf, sizeof(buf), "Photogate: ch%d", photogateCh);
    line(1, buf);
    dtostrf(RADIUS_TALI_MM, 1, 1, f1);
    snprintf(buf, sizeof(buf), "Celah:%d R:%smm", JUMLAH_CELAH, f1);
    line(2, buf);
    line(3, "START untuk mulai");
  }
  else if (state == MEASURING) {
    noInterrupts();
    unsigned int c = count;
    interrupts();
    line(0, "MENGUKUR...");
    snprintf(buf, sizeof(buf), "Pulsa : %u", c);
    line(1, buf);
    dtostrf((c > 0 ? c - 1 : 0) * arcM * 100.0, 1, 1, f1);
    snprintf(buf, sizeof(buf), "Jarak : %s cm", f1);
    line(2, buf);
    line(3, c == 0 ? "Lepas massa..." : "START: selesai");
  }
  else {  // DONE
    if (!okFit) {
      line(0, "DATA KURANG");
      line(1, "Ulangi pengukuran");
      line(2, "");
      line(3, "");
      return;
    }
    if (page == 0) {
      dtostrf(aFit, 1, 3, f1);
      snprintf(buf, sizeof(buf), "a  : %s m/s2", f1);
      line(0, buf);
      dtostrf(vEnd, 1, 2, f1);
      snprintf(buf, sizeof(buf), "v akhir: %s m/s", f1);
      line(1, buf);
      snprintf(buf, sizeof(buf), "Pulsa:%u fit:%u", nPulse, nPulse - BUANG_AWAL);
      line(2, buf);
      dtostrf(jarakCm, 1, 1, f1);
      snprintf(buf, sizeof(buf), "Jarak : %s cm", f1);
      line(3, buf);
    }
    else if (page == 1) {
      dtostrf(resMM, 1, 2, f1);
      snprintf(buf, sizeof(buf), "Residu: %s mm", f1);
      line(0, buf);
      dtostrf(aVT, 1, 3, f1);
      snprintf(buf, sizeof(buf), "a(v-t): %s", f1);
      line(1, buf);
      dtostrf(bedaPct, 1, 1, f1);
      snprintf(buf, sizeof(buf), "Beda  : %s %%", f1);
      line(2, buf);
      dtostrf(Ttot, 1, 3, f1);
      snprintf(buf, sizeof(buf), "T tot : %s s", f1);
      line(3, buf);
    }
    else {
      for (byte row = 0; row < 4; row++) {
        unsigned int idx = scrollIndex + row;
        if (idx < nPulse) {
          dtostrf(tf[idx] * 1000.0, 1, 1, f1);
          if (idx == 0) {
            snprintf(buf, sizeof(buf), "%u t=%s v=-", idx + 1, f1);
          } else {
            float dt = tf[idx] - tf[idx - 1];
            dtostrf(dt > 0 ? arcM / dt : 0, 1, 2, f2);
            snprintf(buf, sizeof(buf), "%u t=%s v=%s", idx + 1, f1, f2);
          }
          line(row, buf);
        } else {
          line(row, "");
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
  lastEdge = 0;
  armed = true;
  interrupts();

  lastCount = 0xFFFF;
  state = MEASURING;
  needRedraw = true;
}

void finishMeasurement() {
  noInterrupts();
  armed = false;
  finished = false;
  unsigned int n = count;
  interrupts();

  if (n < MIN_PULSA) {          // data terlalu sedikit -> batal
    state = IDLE;
    needRedraw = true;
    return;
  }
  nPulse = n;
  okFit = computeResults();
  state = DONE;
  page = 0;
  scrollIndex = 0;
  needRedraw = true;
}

void resetSystem() {
  noInterrupts();
  armed = false;
  finished = false;
  count = 0;
  interrupts();

  nPulse = 0;
  okFit = false;
  page = 0;
  scrollIndex = 0;
  state = IDLE;

  line(0, "");
  line(1, "    Data Direset");
  line(2, "");
  line(3, "");
  delay(800);
  needRedraw = true;
}

// ================= Setup & Loop =================
void setup() {
  lcd.begin(20, 4);
  lcd.clear();
  line(1, "   Pesawat Atwood");
  line(2, "   Encoder Katrol");
  delay(2000);

  if (KALIBRASI_MM_PER_PULSA > 0) arcM = KALIBRASI_MM_PER_PULSA / 1000.0;
  else arcM = (2.0 * PI * RADIUS_TALI_MM / JUMLAH_CELAH) / 1000.0;

  pinMode(btnUp.pin, INPUT_PULLUP);
  pinMode(btnDown.pin, INPUT_PULLUP);
  pinMode(btnMode.pin, INPUT_PULLUP);
  pinMode(btnStart.pin, INPUT_PULLUP);
  pinMode(btnReset.pin, INPUT_PULLUP);

  pinMode(photogatePin, INPUT_PULLUP);
  // Satu jenis tepi saja -> jarak antar pulsa selalu sama.
  // Kalau tidak terdeteksi, coba ganti FALLING ke RISING.
  attachInterrupt(digitalPinToInterrupt(photogatePin), isrEncoder, FALLING);
}

void loop() {
  bool pReset = isButtonPressed(btnReset);
  bool pStart = isButtonPressed(btnStart);
  bool pMode  = isButtonPressed(btnMode);
  bool pUp    = isButtonPressed(btnUp);
  bool pDown  = isButtonPressed(btnDown);

  if (pReset) {
    resetSystem();
  }
  else if (pStart) {
    if (state == MEASURING) finishMeasurement();   // selesai manual
    else startMeasurement();                       // dari IDLE atau DONE
  }
  else if (state == DONE && okFit) {
    if (pMode) {
      page = (page + 1) % 3;
      scrollIndex = 0;
      needRedraw = true;
    }
    if (page == 2) {
      if (pUp && scrollIndex > 0) {
        scrollIndex--;
        needRedraw = true;
      }
      if (pDown && nPulse > 4 && scrollIndex < (int)nPulse - 4) {
        scrollIndex++;
        needRedraw = true;
      }
    }
  }

  // Pantau pengukuran
  if (state == MEASURING) {
    noInterrupts();
    unsigned int c = count;
    unsigned long le = lastEdge;
    unsigned long now = micros();
    bool fin = finished;
    interrupts();

    // Berhenti otomatis kalau tidak ada pulsa baru selama TIMEOUT_US
    if (!fin && c >= MIN_PULSA && (now - le) > TIMEOUT_US) fin = true;

    if (fin) {
      finishMeasurement();
    } else if (c != lastCount && (millis() - lastDraw) > 100) {
      lastCount = c;
      needRedraw = true;
    }
  }

  if (needRedraw) {
    needRedraw = false;
    drawScreen();
  }
}
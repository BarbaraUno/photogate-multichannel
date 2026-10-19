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

// ================= Photogate (dipakai bersama) =================
const byte photogatePin = 2;   // pin interrupt hardware (INT4)
const byte photogateCh  = 5;   // ch5 = pin 2

// ================= Status umum =================
enum App { APP_MENU, APP_BANDUL, APP_ATWOOD };
App app = APP_MENU;
byte menuSel = 0;              // 0 = Bandul, 1 = Atwood
bool menuRedraw = true;

// Kejadian tombol (diisi sekali tiap putaran loop)
bool evUp, evDown, evMode, evStart, evReset;

// Tahan MODE untuk kembali ke menu
const unsigned long HOLD_MENU_MS = 1500;
unsigned long modeHoldStart = 0;
bool modeHoldFired = false;

// Cetak satu baris 20 karakter, sisanya diisi spasi (tanpa lcd.clear -> tidak berkedip)
void line(byte row, const char *s) {
  lcd.setCursor(0, row);
  byte n = 0;
  while (s[n] && n < 20) { lcd.write(s[n]); n++; }
  while (n < 20) { lcd.write(' '); n++; }
}

// ======================================================================
//                         BANDUL MATEMATIS
// ======================================================================
const byte B_TOTAL_AYUNAN = 12;                      // total ayunan yang direkam
const byte B_BUANG        = 2;                       // ayunan awal yang dibuang
const byte B_UKUR         = B_TOTAL_AYUNAN - B_BUANG;  // 10 ayunan yang dihitung
const byte B_TOTAL_TS     = 2 * B_TOTAL_AYUNAN + 1;  // 25 titik waktu

const unsigned long B_MIN_GAP_US = 50000UL;  // abaikan deteksi < 50 ms
const float B_OUTLIER_TOL = 0.20;            // outlier jika beda > 20% dari median

volatile unsigned long b_ts[B_TOTAL_TS];
volatile byte b_count = 0;
volatile bool b_armed = false;
volatile bool b_finished = false;
volatile unsigned long b_lastEdge = 0;

float b_Tk[B_UKUR];
bool  b_outlier[B_UKUR];
float b_Tmean = 0;
float b_Tsd = 0;
float b_Ttotal = 0;
byte  b_nValid = 0;

enum BState { B_IDLE, B_MEASURING, B_DONE };
BState bState = B_IDLE;
byte bPage = 0;          // 0 = ringkasan, 1 = daftar T per ayunan
int bScroll = 0;
bool bRedraw = true;
byte bLastCount = 255;

// ISR: hanya menyimpan waktu
void isrBandul() {
  if (!b_armed) return;
  unsigned long now = micros();
  if (b_count > 0 && (now - b_lastEdge) < B_MIN_GAP_US) return;

  b_ts[b_count] = now;
  b_count++;
  b_lastEdge = now;

  if (b_count >= B_TOTAL_TS) {
    b_armed = false;
    b_finished = true;
  }
}

void bCompute() {
  unsigned long t[B_TOTAL_TS];
  noInterrupts();
  for (byte i = 0; i < B_TOTAL_TS; i++) t[i] = b_ts[i];
  interrupts();

  // T tiap ayunan = selisih lintasan searah (n dan n+2), ayunan awal dibuang
  for (byte j = 0; j < B_UKUR; j++) {
    byte k = B_BUANG + j;
    unsigned long dt = t[2 * k + 2] - t[2 * k];
    b_Tk[j] = dt / 1000000.0;
  }

  // Median untuk mendeteksi outlier
  float sorted[B_UKUR];
  for (byte i = 0; i < B_UKUR; i++) sorted[i] = b_Tk[i];
  for (byte i = 1; i < B_UKUR; i++) {
    float key = sorted[i];
    int j = i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }
  float median = (sorted[B_UKUR / 2 - 1] + sorted[B_UKUR / 2]) / 2.0;

  float sum = 0;
  b_nValid = 0;
  for (byte i = 0; i < B_UKUR; i++) {
    b_outlier[i] = (fabs(b_Tk[i] - median) > B_OUTLIER_TOL * median);
    if (!b_outlier[i]) {
      sum += b_Tk[i];
      b_nValid++;
    }
  }
  b_Tmean = (b_nValid > 0) ? sum / b_nValid : 0;

  float sq = 0;
  for (byte i = 0; i < B_UKUR; i++) {
    if (!b_outlier[i]) sq += (b_Tk[i] - b_Tmean) * (b_Tk[i] - b_Tmean);
  }
  b_Tsd = (b_nValid > 1) ? sqrt(sq / (b_nValid - 1)) : 0;

  unsigned long total = t[2 * B_TOTAL_AYUNAN] - t[2 * B_BUANG];
  b_Ttotal = (total / (float)B_UKUR) / 1000000.0;
}

void bDraw() {
  char buf[26], f1[14];

  if (bState == B_IDLE) {
    line(0, "  BANDUL MATEMATIS");
    snprintf(buf, sizeof(buf), "Photogate: ch%d", photogateCh);
    line(1, buf);
    line(2, "START: mulai ukur");
    line(3, "Tahan MODE: menu");
  }
  else if (bState == B_MEASURING) {
    byte c = b_count;
    byte swings = (c >= 1) ? (c - 1) / 2 : 0;
    line(0, "MENGUKUR...");
    snprintf(buf, sizeof(buf), "Ayunan: %u/%u", swings, B_TOTAL_AYUNAN);
    line(1, buf);
    if (c == 0) line(2, "Ayunkan bandul");
    else if (swings < B_BUANG) line(2, "(dibuang)");
    else line(2, "(diukur)");
    line(3, "START: batal");
  }
  else {  // B_DONE
    if (bPage == 0) {
      dtostrf(b_Tmean, 1, 4, f1);
      snprintf(buf, sizeof(buf), "T rata2: %s s", f1);
      line(0, buf);
      dtostrf(b_Tsd, 1, 4, f1);
      snprintf(buf, sizeof(buf), "SD     : %s s", f1);
      line(1, buf);
      snprintf(buf, sizeof(buf), "Valid  : %u/%u", b_nValid, B_UKUR);
      line(2, buf);
      dtostrf(b_Ttotal, 1, 4, f1);
      snprintf(buf, sizeof(buf), "T total: %s s", f1);
      line(3, buf);
    } else {
      for (byte row = 0; row < 4; row++) {
        int idx = bScroll + row;
        if (idx < B_UKUR) {
          dtostrf(b_Tk[idx], 1, 4, f1);
          snprintf(buf, sizeof(buf), "%2d: %s s%s", idx + 1, f1,
                   b_outlier[idx] ? " *" : "");
          line(row, buf);
        } else {
          line(row, "");
        }
      }
    }
  }
}

void bStart() {
  noInterrupts();
  b_count = 0;
  b_finished = false;
  b_armed = true;
  interrupts();
  bLastCount = 255;
  bState = B_MEASURING;
  bRedraw = true;
}

void bCancel() {
  noInterrupts();
  b_armed = false;
  interrupts();
  bState = B_IDLE;
  bRedraw = true;
}

// Reset data tanpa pesan (dipakai saat masuk ke mode)
void bResetData() {
  noInterrupts();
  b_armed = false;
  b_finished = false;
  b_count = 0;
  interrupts();
  b_nValid = 0;
  bPage = 0;
  bScroll = 0;
  bState = B_IDLE;
  bRedraw = true;
}

void bandulLoop() {
  if (evReset) {
    bResetData();
    line(0, "");
    line(1, "    Data Direset");
    line(2, "");
    line(3, "");
    delay(800);
  }
  else if (evStart) {
    if (bState == B_MEASURING) bCancel();
    else bStart();                      // dari IDLE atau DONE
  }
  else if (bState == B_DONE) {
    if (evMode) {
      bPage = (bPage == 0) ? 1 : 0;
      bScroll = 0;
      bRedraw = true;
    }
    if (bPage == 1) {
      if (evUp && bScroll > 0) {
        bScroll--;
        bRedraw = true;
      }
      if (evDown && bScroll < B_UKUR - 4) {
        bScroll++;
        bRedraw = true;
      }
    }
  }

  if (bState == B_MEASURING) {
    byte c = b_count;
    bool fin = b_finished;

    if (c != bLastCount) {
      bLastCount = c;
      bRedraw = true;
    }
    if (fin) {
      noInterrupts();
      b_finished = false;
      interrupts();
      bCompute();
      bState = B_DONE;
      bPage = 0;
      bScroll = 0;
      bRedraw = true;
    }
  }

  if (bRedraw) {
    bRedraw = false;
    bDraw();
  }
}

// ======================================================================
//                      PESAWAT ATWOOD (ENCODER KATROL)
// ======================================================================

// ===== PARAMETER KATROL (ISI SESUAI DESAIN) =====
const byte  JUMLAH_CELAH   = 6;     // jumlah jendela/jari-jari pada katrol
const float RADIUS_TALI_MM = 23.0;  // jari-jari efektif tempat tali melilit (mm)
// Kalibrasi (disarankan): turunkan massa sejauh D mm, lihat jumlah pulsa P,
// lalu isi nilai ini = D / (P-1). Kalau > 0, dipakai menggantikan hitungan di atas.
const float KALIBRASI_MM_PER_PULSA = 0.0;

// ===== Parameter pengukuran =====
const unsigned int MAKS_PULSA = 150;           // maksimum pulsa yang direkam
const byte BUANG_AWAL = 1;                     // pulsa awal tidak ikut perhitungan
const byte MIN_PULSA  = BUANG_AWAL + 5;        // minimum agar hasil dihitung
const unsigned long A_MIN_GAP_US = 1000UL;     // abaikan pulsa < 1 ms (noise)
const unsigned long TIMEOUT_US = 1000000UL;    // 1 s tanpa pulsa = gerakan selesai

float arcM;   // jarak lintasan massa per pulsa (meter)

// ===== Data (diisi ISR) =====
volatile unsigned long a_ts[MAKS_PULSA];
volatile unsigned int a_count = 0;
volatile bool a_armed = false;
volatile bool a_finished = false;
volatile unsigned long a_lastEdge = 0;

// ===== Hasil olahan =====
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

enum AState { A_IDLE, A_MEASURING, A_DONE };
AState aState = A_IDLE;
byte aPage = 0;           // 0 = utama, 1 = kualitas data, 2 = daftar pulsa
int aScroll = 0;
bool aRedraw = true;
unsigned int aLastCount = 0xFFFF;
unsigned long aLastDraw = 0;

// ISR: hanya menyimpan waktu
void isrAtwood() {
  if (!a_armed) return;
  unsigned long now = micros();
  if (a_count > 0 && (now - a_lastEdge) < A_MIN_GAP_US) return;

  a_ts[a_count] = now;
  a_count++;
  a_lastEdge = now;

  if (a_count >= MAKS_PULSA) {
    a_armed = false;
    a_finished = true;
  }
}

float det3(float a, float b, float c, float d, float e, float f,
           float g, float h, float k) {
  return a * (e * k - f * h) - b * (d * k - f * g) + c * (d * h - e * g);
}

bool aCompute() {
  unsigned int n = nPulse;

  noInterrupts();
  unsigned long t0 = a_ts[0];
  for (unsigned int i = 0; i < n; i++) tf[i] = (float)(a_ts[i] - t0) / 1000000.0;
  interrupts();

  unsigned int i0 = BUANG_AWAL;
  unsigned int m = n - i0;
  if (m < 5) return false;

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

  float sq = 0;
  for (unsigned int i = i0; i < n; i++) {
    float u = (tf[i] - tmean) / s;
    float r = i * arcM - (c0 + c1 * u + c2 * u * u);
    sq += r * r;
  }
  resMM = sqrt(sq / (m - 3)) * 1000.0;

  // Pembanding: regresi linear v terhadap t
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

void aDraw() {
  char buf[26], f1[14], f2[14];
  aLastDraw = millis();

  if (aState == A_IDLE) {
    snprintf(buf, sizeof(buf), "ATWOOD  (ch%d)", photogateCh);
    line(0, buf);
    dtostrf(RADIUS_TALI_MM, 1, 1, f1);
    snprintf(buf, sizeof(buf), "Celah:%d R:%smm", JUMLAH_CELAH, f1);
    line(1, buf);
    line(2, "START: mulai ukur");
    line(3, "Tahan MODE: menu");
  }
  else if (aState == A_MEASURING) {
    noInterrupts();
    unsigned int c = a_count;
    interrupts();
    line(0, "MENGUKUR...");
    snprintf(buf, sizeof(buf), "Pulsa : %u", c);
    line(1, buf);
    dtostrf((c > 0 ? c - 1 : 0) * arcM * 100.0, 1, 1, f1);
    snprintf(buf, sizeof(buf), "Jarak : %s cm", f1);
    line(2, buf);
    line(3, c == 0 ? "Lepas massa..." : "START: selesai");
  }
  else {  // A_DONE
    if (!okFit) {
      line(0, "DATA KURANG");
      line(1, "Ulangi pengukuran");
      line(2, "");
      line(3, "");
      return;
    }
    if (aPage == 0) {
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
    else if (aPage == 1) {
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
        unsigned int idx = aScroll + row;
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

void aStart() {
  noInterrupts();
  a_count = 0;
  a_finished = false;
  a_lastEdge = 0;
  a_armed = true;
  interrupts();

  aLastCount = 0xFFFF;
  aState = A_MEASURING;
  aRedraw = true;
}

void aFinish() {
  noInterrupts();
  a_armed = false;
  a_finished = false;
  unsigned int n = a_count;
  interrupts();

  if (n < MIN_PULSA) {          // data terlalu sedikit -> batal
    aState = A_IDLE;
    aRedraw = true;
    return;
  }
  nPulse = n;
  okFit = aCompute();
  aState = A_DONE;
  aPage = 0;
  aScroll = 0;
  aRedraw = true;
}

// Reset data tanpa pesan (dipakai saat masuk ke mode)
void aResetData() {
  noInterrupts();
  a_armed = false;
  a_finished = false;
  a_count = 0;
  interrupts();

  nPulse = 0;
  okFit = false;
  aPage = 0;
  aScroll = 0;
  aState = A_IDLE;
  aRedraw = true;
}

void atwoodLoop() {
  if (evReset) {
    aResetData();
    line(0, "");
    line(1, "    Data Direset");
    line(2, "");
    line(3, "");
    delay(800);
  }
  else if (evStart) {
    if (aState == A_MEASURING) aFinish();   // selesai manual
    else aStart();                          // dari IDLE atau DONE
  }
  else if (aState == A_DONE && okFit) {
    if (evMode) {
      aPage = (aPage + 1) % 3;
      aScroll = 0;
      aRedraw = true;
    }
    if (aPage == 2) {
      if (evUp && aScroll > 0) {
        aScroll--;
        aRedraw = true;
      }
      if (evDown && nPulse > 4 && aScroll < (int)nPulse - 4) {
        aScroll++;
        aRedraw = true;
      }
    }
  }

  if (aState == A_MEASURING) {
    noInterrupts();
    unsigned int c = a_count;
    unsigned long le = a_lastEdge;
    unsigned long now = micros();
    bool fin = a_finished;
    interrupts();

    // Berhenti otomatis kalau tidak ada pulsa baru selama TIMEOUT_US
    if (!fin && c >= MIN_PULSA && (now - le) > TIMEOUT_US) fin = true;

    if (fin) {
      aFinish();
    } else if (c != aLastCount && (millis() - aLastDraw) > 100) {
      aLastCount = c;
      aRedraw = true;
    }
  }

  if (aRedraw) {
    aRedraw = false;
    aDraw();
  }
}

// ======================================================================
//                         MENU & PENGALIH MODE
// ======================================================================
void drawMenu() {
  line(0, "=== MENU UTAMA ===");
  line(1, menuSel == 0 ? "> Bandul Matematis" : "  Bandul Matematis");
  line(2, menuSel == 1 ? "> Pesawat Atwood" : "  Pesawat Atwood");
  line(3, "UP/DN:pilih START:ok");
}

void enterApp(byte a) {
  detachInterrupt(digitalPinToInterrupt(photogatePin));
  app = a;
  if (a == APP_BANDUL) {
    bResetData();
    attachInterrupt(digitalPinToInterrupt(photogatePin), isrBandul, FALLING);
  } else if (a == APP_ATWOOD) {
    aResetData();
    attachInterrupt(digitalPinToInterrupt(photogatePin), isrAtwood, FALLING);
  }
}

void goMenu() {
  detachInterrupt(digitalPinToInterrupt(photogatePin));
  noInterrupts();
  b_armed = false;
  a_armed = false;
  interrupts();
  app = APP_MENU;
  menuRedraw = true;
}

void menuLoop() {
  if (evUp && menuSel > 0) {
    menuSel--;
    menuRedraw = true;
  }
  if (evDown && menuSel < 1) {
    menuSel++;
    menuRedraw = true;
  }
  if (evStart) {
    enterApp(menuSel == 0 ? APP_BANDUL : APP_ATWOOD);
    return;
  }
  if (menuRedraw) {
    menuRedraw = false;
    drawMenu();
  }
}

// ================= Setup & Loop =================
void setup() {
  lcd.begin(20, 4);
  lcd.clear();
  line(1, "     PHOTOGATE");
  line(2, "  Bandul & Atwood");
  delay(1500);

  if (KALIBRASI_MM_PER_PULSA > 0) arcM = KALIBRASI_MM_PER_PULSA / 1000.0;
  else arcM = (2.0 * PI * RADIUS_TALI_MM / JUMLAH_CELAH) / 1000.0;

  pinMode(btnUp.pin, INPUT_PULLUP);
  pinMode(btnDown.pin, INPUT_PULLUP);
  pinMode(btnMode.pin, INPUT_PULLUP);
  pinMode(btnStart.pin, INPUT_PULLUP);
  pinMode(btnReset.pin, INPUT_PULLUP);

  pinMode(photogatePin, INPUT_PULLUP);
  // Interrupt dipasang saat masuk ke tiap mode (lihat enterApp)
}

void loop() {
  // Semua tombol dibaca tiap putaran supaya debounce tetap akurat
  evReset = isButtonPressed(btnReset);
  evStart = isButtonPressed(btnStart);
  evMode  = isButtonPressed(btnMode);
  evUp    = isButtonPressed(btnUp);
  evDown  = isButtonPressed(btnDown);

  // Tahan MODE >= 1,5 detik = kembali ke menu utama
  if (btnMode.state == LOW) {
    if (modeHoldStart == 0) {
      modeHoldStart = millis();
    } else if (!modeHoldFired && (millis() - modeHoldStart) > HOLD_MENU_MS) {
      modeHoldFired = true;
      if (app != APP_MENU) goMenu();
    }
  } else {
    modeHoldStart = 0;
    modeHoldFired = false;
  }

  if (app == APP_MENU) menuLoop();
  else if (app == APP_BANDUL) bandulLoop();
  else atwoodLoop();
}
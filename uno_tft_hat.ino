/*========================================
 */

#include <SPI.h>
#include <SD.h>
#include <Time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ─── Display Configuration ───────────────────────────────
#define TFT_SCREEN_WIDTH  500  
#define TFT_SCREEN_HEIGHT 300

// TFT Hat pin assignments (typical for 2.7" hats)
#define TFT_CS    9   // TFT SPI chip select
#define TFT_RST   8    // TFT reset
#define TFT_DC    7    // TFT data/command

// Touch controller pins (XPT2046)
#define TOUCH_CS  A2   // Touch chip select (custom, not on SPI header)
#define TOUCH_Y  A0    // Touch Y-axis read
#define TOUCH_X  A1    // Touch X-axis read

// SD card
#define SD_CS      4   // SD card chip select

// ─── Timing Constants ────────────────────────────────────
#define PICTURE_DELAY   5000   // ms between images
#define TAP_DEBOUNCE    400    // ms between mode changes
#define TAP_SAMPLE_MS   3      // ms delay per ADC sample
#define TAP_SAMPLES     8      // samples to average

// ─── Touch Calibration ───────────────────────────────────
// XPT2046 typical ADC range varies by hat.
// Adjust these if touch feels misaligned at edges.
// Measured at screen corners: top-left ~ (yMin, xMin), bottom-right ~ (yMax, xMax)
#define TOUCH_X_MIN   150
#define TOUCH_X_MAX   900
#define TOUCH_Y_MIN   150
#define TOUCH_Y_MAX   900

#define TOUCH_MAP(x, inMin, inMax, outMin, outMax) \
  ((x) <= (inMin) ? (outMin) : ((x) >= (inMax) ? (outMax) : \
   (long)((x) - (inMin)) * (outMax - (outMin)) / ((inMax) - (inMin)) + (outMin)))

// ─── App Modes ───────────────────────────────────────────
enum Mode {
  MODE_DASHBOARD,
  MODE_CLOCK,
  MODE_PICTURE_FRAME,
  MODE_EYES,
  MODE_SPOTIFY        // placeholder — future
};

// ─── Globals ─────────────────────────────────────────────
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

Mode currentMode   = MODE_DASHBOARD;
unsigned long modeChangeTime = 0;
bool         touchConsumed = false;

// Clock redraw cache
int  lastClockHour    = 0;
int  lastClockMinute  = 0;

// Picture frame state
int picIndex        = 0;
int prevPicIndex    = -1;    // track previous index to detect changes
int totalPictures   = 0;
unsigned long picLastChange = 0;

// Eyes animation state
int  eyeY, eyeX;          // center positions
int  lookX = 0;           // gaze offset (-120..120)
bool eyeBlink   = false;
bool eyeClosing = false;
unsigned long lookChangeTime = 0;
unsigned long blinkTime    = 0;
unsigned long blinkHold    = 0;
const int    BLINK_DURATION   = 150;   // ms eyelids stay shut
const int    MIN_BLINK_GAP    = 1500;  // min ms between blinks
const int    MAX_BLINK_GAP    = 4500;  // max ms between blinks
const int    LOOK_DURATION    = 2000;  // ms holding a gaze direction

// Check if a filename has a .bmp extension (case-insensitive)
static bool isBmpFile(const char* name) {
  int len = strlen(name);
  if (len < 4) return false;
  int ext = len - 4;
  char c0 = name[ext], c1 = name[ext + 1], c2 = name[ext + 2], c3 = name[ext + 3];
  return ((c0 == 'b' || c0 == 'B') &&
          (c1 == 'm' || c1 == 'M') &&
          (c2 == 'p' || c2 == 'P') &&
          (c3 == '.' || c3 == ','));
}

// ─── Pin Modes (run once in setup) ──────────────────────
void initTouchPins() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);  // deselect touch controller
}

// ─── Touch Reading ──────────────────────────────────────
bool readTouch(int &x, int &y) {
  long sumX = 0, sumY = 0;
  int validCount = 0;

  for (int i = 0; i < TAP_SAMPLES; i++) {
    digitalWrite(TOUCH_CS, LOW);

    delay(TAP_SAMPLE_MS);
    int touchY = analogRead(TOUCH_Y);  // Y-axis reading
    int touchX = analogRead(TOUCH_X);  // X-axis reading

    delay(TAP_SAMPLE_MS);
    digitalWrite(TOUCH_CS, HIGH);

    // XPT2046 returns 0-1023 on ADC
    // Valid touch: reasonable pressure range (avoids noise + saturation)
    if (touchY > TOUCH_X_MIN + 20 && touchY < TOUCH_X_MAX - 20 &&
        touchX > TOUCH_Y_MIN + 20 && touchX < TOUCH_Y_MAX - 20) {
      sumX += touchX;
      sumY += touchY;
      validCount++;
    }
  }

  if (validCount < TAP_SAMPLES / 2) return false;

  x = (int)(sumX / validCount);
  y = (int)(sumY / validCount);
  return true;
}

// ─── Dashboard Drawing ──────────────────────────────────
void drawDashboard() {
  tft.fillScreen(ST77XX_BLACK);

  // Title
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(90, 8);
  tft.println("DASHBOARD");

  tft.drawLine(10, 40, TFT_SCREEN_WIDTH - 10, 40, ST77XX_GRAY);

  // 2x2 grid layout — item dimensions
  const int cellW = 130, cellH = 105;
  const int gapX  = 40,  gapY  = 45;
  const int ox   = 25,  oy   = 50;  // top-left origin of grid

  struct {
    Mode   mode;
    uint16_t color;
    const char *label;
  } items[] = {
    { MODE_CLOCK,     ST77XX_YELLOW, "CLOCK" },
    { MODE_PICTURE_FRAME, ST77XX_CYAN, "PICTURES" },
    { MODE_EYES,      ST77XX_GREEN,  "EYES" },
    { MODE_SPOTIFY,   ST77XX_GREEN,  "SPOTIFY" }
  };

  for (int i = 0; i < 4; i++) {
    int col = i % 2;
    int row = i / 2;
    int cx = ox + col * (cellW + gapX);
    int cy = oy + row * (cellH + gapY);

    // Rounded card
    tft.fillRoundRect(cx, cy, cellW, cellH, 10, ST77XX_DARKGREY);
    tft.drawRoundRect(cx, cy, cellW, cellH, 10, items[i].color);

    // Draw custom icon per item
    int icx = cx + cellW / 2;
    int icy = cy + 40;
    switch (items[i].mode) {
      case MODE_CLOCK:
        drawClockIcon(icx, icy, items[i].color);
        break;
      case MODE_PICTURE_FRAME:
        drawPictureIcon(icx, icy, items[i].color);
        break;
      case MODE_EYES:
        drawEyesIcon(icx, icy, items[i].color);
        break;
      case MODE_SPOTIFY:
        drawSpotifyIcon(icx, icy, items[i].color);
        break;
    }

    // Label
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(cx + 10, cy + 78);
    tft.println(items[i].label);

    // Spotify TODO note
    if (i == 3) {
      tft.setTextColor(ST77XX_GRAY);
      tft.setTextSize(0);
      tft.setCursor(cx + 30, cy + 92);
      tft.println("TODO");
    }
  }
}

// ─── Dashboard Icon Drawing (graphics primitives) ───────

void drawClockIcon(int x, int y, uint16_t color) {
  // Clock face
  tft.drawCircle(x, y, 20, color);
  // Hour hand
  tft.drawLine(x, y, x + 6, y - 10, color);
  // Minute hand
  tft.drawLine(x, y, x + 12, y + 4, color);
  // Center dot
  tft.fillCircle(x, y, 2, color);
}

void drawPictureIcon(int x, int y, uint16_t color) {
  // Picture frame rectangle
  tft.drawRect(x - 18, y - 14, 36, 28, color);
  // Sun
  tft.fillCircle(x - 6, y - 4, 4, color);
  // Mountains
  tft.drawLine(x - 16, y + 14, x - 4, y - 2, color);
  tft.drawLine(x - 4, y - 2, x + 6, y + 14, color);
  tft.drawLine(x + 2, y + 14, x + 12, y + 4, color);
}

void drawEyesIcon(int x, int y, uint16_t color) {
  // Left eye
  tft.drawCircle(x - 12, y, 10, color);
  tft.fillCircle(x - 12, y, 3, color);
  // Right eye
  tft.drawCircle(x + 12, y, 10, color);
  tft.fillCircle(x + 12, y, 3, color);
}

void drawSpotifyIcon(int x, int y, uint16_t color) {
  // Speaker/disc circle
  tft.drawCircle(x, y, 18, color);
  // Inner circle
  tft.drawCircle(x, y, 10, color);
  // Line through
  tft.drawLine(x - 6, y, x + 6, y, color);
}

// ─── Tap Detection (dashboard only) ─────────────────────
bool detectDashboardTap() {
  const int cellW = 130, cellH = 105;
  const int gapX  = 40,  gapY  = 45;
  const int ox   = 25,  oy   = 50;

  int tx, ty;
  if (!readTouch(tx, ty)) return false;

  // Map XPT2046 ADC → screen pixel coordinates using calibration
  int sx = TOUCH_MAP(tx, TOUCH_X_MIN, TOUCH_X_MAX, 0, TFT_SCREEN_WIDTH);
  int sy = TOUCH_MAP(ty, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, TFT_SCREEN_HEIGHT);

  for (int i = 0; i < 4; i++) {
    int col = i % 2, row = i / 2;
    int cx = ox + col * (cellW + gapX);
    int cy = oy + row * (cellH + gapY);
    if (sx >= cx && sx <= cx + cellW && sy >= cy && sy <= cy + cellH)
      return true;
  }
  return false;
}

// ─── Clock Mode ─────────────────────────────────────────
void drawClock() {
  int h = hourFormat12();
  int m = minute();
  bool timeChanged = (h != lastClockHour) || (m != lastClockMinute);
  lastClockHour = h;
  lastClockMinute = m;

  if (timeChanged) {
    // Full redraw when time changes
    tft.fillScreen(ST77XX_DARKGREY);

    // Header bar
    tft.fillRect(0, 0, TFT_SCREEN_WIDTH, 32, ST77XX_BLUE);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setCursor(10, 5);
    tft.println("  DIGITAL CLOCK");

    // Date
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(10, TFT_SCREEN_HEIGHT - 25);
    tft.print("  ");
    tft.print(day());
    tft.print('/');
    tft.print(month());
    tft.print('/');
    tft.println(year());

    // Separator (use TFT_SCREEN_WIDTH instead of hardcoded 300)
    tft.drawLine(20, 36, TFT_SCREEN_WIDTH, 36, ST77XX_GRAY);

    drawDigitalTime(100, 75);
  } else {
    // Only redraw the blinking dots
    drawBlinkingDots(100, 75);
  }
}

void drawBlinkingDots(int x, int y) {
  if (second() % 2 == 0) {
    tft.fillCircle(x + 130, y + 35, 6, ST77XX_WHITE);
  } else {
    tft.fillCircle(x + 130, y + 35, 6, ST77XX_DARKGREY);
  }
}

// Draw large digital time (12-hour format)
void drawDigitalTime(int x, int y) {
  // AM/PM indicator
  const char *ampm = (hourFormat12() == 12 && hour() >= 12) ? "PM" : "AM";

  // Time string: HH:MM
  char buf[12];
  snprintf(buf, sizeof(buf), "%02d:%02d", hourFormat12(), minute());

  // Outline text for visibility
  tft.setTextSize(5);
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(x + 2, y + 2);
  tft.println(buf);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(x, y);
  tft.println(buf);

  // AM/PM
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(x + 150, y);
  tft.println(ampm);

  // Separator dots (blinking)
  if (second() % 2 == 0) {
    tft.fillCircle(x + 130, y + 35, 6, ST77XX_WHITE);
  } else {
    tft.fillCircle(x + 130, y + 35, 6, ST77XX_DARKGREY);
  }
}

// ─── Picture Frame Mode ─────────────────────────────────
void drawPictureFrame() {
  tft.fillScreen(ST77XX_BLACK);

  // Header bar
  tft.fillRect(0, 0, TFT_SCREEN_WIDTH, 28, ST77XX_BLUE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(8, 3);
  tft.print("PICTURE FRAME  (");
  tft.print(totalPictures);
  tft.println(" images)");

  tft.drawLine(0, 28, TFT_SCREEN_WIDTH, 28, ST77XX_GRAY);

  // Only re-render image when index actually changed
  if (picIndex != prevPicIndex) {
    prevPicIndex = picIndex;
    drawBmpOnScreen(picIndex);

    // Update counter text (clear old, draw new)
    tft.setTextColor(ST77XX_BLACK);
    tft.setTextSize(1);
    tft.fillRect(80, TFT_SCREEN_HEIGHT - 16, 160, 16, ST77XX_BLACK);
    tft.setTextColor(ST77XX_GRAY);
    char txt[16];
    snprintf(txt, sizeof(txt), "Photo %d/%d", picIndex + 1, totalPictures);
    tft.setCursor(80, TFT_SCREEN_HEIGHT - 16);
    tft.print(txt);
  } else {
    // Still draw the counter even if image didn't change
    tft.setTextColor(ST77XX_BLACK);
    tft.setTextSize(1);
    tft.fillRect(80, TFT_SCREEN_HEIGHT - 16, 160, 16, ST77XX_BLACK);
    tft.setTextColor(ST77XX_GRAY);
    char txt[16];
    snprintf(txt, sizeof(txt), "Photo %d/%d", picIndex + 1, totalPictures);
    tft.setCursor(80, TFT_SCREEN_HEIGHT - 16);
    tft.print(txt);
  }
}

// Draw a BMP image centered/scaled to fit the screen
// Only supports 24-bit uncompressed BMP files
bool drawBmpOnScreen(int index) {
  // ── Find the Nth BMP file on SD card (open only one handle) ──
  File root = SD.open("/");
  if (!root) return false;

  int bmpCount = 0;
  File entry = root.openNextFile();
  while (entry) {
    // Check for .bmp extension (case-insensitive, first 4 chars)
    const char* name = entry.name();
    int len = strlen(name);
    if (len >= 4) {
      int ext = len - 4;
      if ((name[ext] == 'b' || name[ext] == 'B') &&
          (name[ext+1] == 'm' || name[ext+1] == 'M') &&
          (name[ext+2] == 'p' || name[ext+2] == 'P') &&
          (name[ext+3] == '.' || name[ext+3] == ',')) {
        bmpCount++;
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  if (bmpCount == 0) return false;

  // Handle index wrapping
  if (index >= bmpCount) index = 0;
  if (index < 0) index = bmpCount - 1;

  // Open only the target BMP file
  bmpCount = 0;
  root = SD.open("/");
  if (!root) return false;
  entry = root.openNextFile();
  while (entry) {
    const char* name = entry.name();
    int len = strlen(name);
    bool isBmp = false;
    if (len >= 4) {
      int ext = len - 4;
      if ((name[ext] == 'b' || name[ext] == 'B') &&
          (name[ext+1] == 'm' || name[ext+1] == 'M') &&
          (name[ext+2] == 'p' || name[ext+2] == 'P')) {
        // Check the dot - could be at position len-4 or earlier
        if (name[ext+3] == '.') {
          isBmp = true;
        } else {
          // 8.3 format: check for dot separator
          for (int i = 0; i < ext; i++) {
            if (name[i] == '.') { isBmp = true; break; }
          }
        }
      }
    }
    if (isBmp && bmpCount == index) {
      entry.close();
      File bmp = SD.open(name);
      if (!bmp) return false;

      // ── Read BMP headers ──────────────────────────────

      // File type (bytes 0-1)
      uint8_t bfType[2];
      bmp.read(bfType, 2);
      if (bfType[0] != 'B' || bfType[1] != 'M') {
        bmp.close();
        return false;
      }

      // Bitmap offset (bytes 10-13)
      bmp.seek(10);
      uint32_t bfOffBits;
      bmp.read((uint8_t*)&bfOffBits, 4);

      // Image dimensions (bytes 18-21 for width, 22-25 for height)
      bmp.seek(bfOffBits);
      int32_t imgWidth, imgHeight;
      bmp.read((uint8_t*)&imgWidth, 4);
      bmp.read((uint8_t*)&imgHeight, 4);
      if (imgHeight < 0) imgHeight = -imgHeight;  // handle top-down BMPs

      // Color depth (bytes 28-29 relative to bitmap data)
      bmp.seek(bfOffBits + 14);
      uint16_t bpp;
      bmp.read((uint8_t*)&bpp, 2);
      if (bpp != 24) {
        bmp.close();
        return false;  // only 24-bit BMP supported
      }

      // Compression (bytes 30-33 relative to bitmap data), must be 0 = BI_RGB
      uint32_t compression;
      bmp.read((uint8_t*)&compression, 4);
      if (compression != 0) {
        bmp.close();
        return false;
      }

      // Row padding (each row padded to 4-byte boundary)
      int rowSize = ((imgWidth * 3 + 3) / 4) * 4;

      // ── Calculate display area ────────────────────────

      // Scale to fit within screen bounds (centered)
      int dispW = min(imgWidth, TFT_SCREEN_WIDTH);
      int dispH = min(imgHeight, TFT_SCREEN_HEIGHT - 28);

      int offsetX = (TFT_SCREEN_WIDTH - dispW) / 2;
      int offsetY = 28 + (TFT_SCREEN_HEIGHT - 28 - dispH) / 2;

      // ── Render pixels (expand each source pixel) ──────

      tft.setSwapBytes(true);

      // BMP stores rows bottom-to-top
      for (int py = imgHeight - 1; py >= 0; py--) {
        // Map source row to display row
        int displayY = offsetY + py * dispH / imgHeight;
        if (displayY >= offsetY + dispH) continue;

        // Read entire row into buffer (including padding)
        uint8_t rowBuf[3 * 4096];  // max 4096 pixels * 3 bytes
        int bufSize = rowSize;
        if (bufSize > sizeof(rowBuf)) bufSize = sizeof(rowBuf);
        bmp.seek(bfOffBits + py * rowSize);
        bmp.read(rowBuf, bufSize);

        // Expand each source pixel across its display columns
        for (int dx = 0; dx < dispW; dx++) {
          int srcPx = dx * imgWidth / dispW;
          // Bounds check in case srcPx exceeds image width
          if (srcPx >= imgWidth) srcPx = imgWidth - 1;

          uint8_t b = rowBuf[srcPx * 3];
          uint8_t g = rowBuf[srcPx * 3 + 1];
          uint8_t r = rowBuf[srcPx * 3 + 2];

          // Convert to 16-bit RGB565
          uint16_t pixel = ((uint16_t)(r >> 3) << 11) |
                           ((uint16_t)(g >> 2) << 5)  |
                           ((uint16_t)(b >> 3));

          // Expand pixel to cover all display columns it maps to
          int nextDx = (dx + 1) * imgWidth / dispW;
          if (nextDx >= dispW) nextDx = dispW;
          tft.fillRect(offsetX + dx, displayY, nextDx - dx, 1, pixel);
        }
      }

      bmp.close();
      return true;
    }
    bmpCount++;
    entry.close();
    entry = root.openNextFile();
  }
  root.close();
  return false;  // index out of range
}

// ─── Eyes Mode ──────────────────────────────────────────
void setupEyes() {
  // Center positions (symmetric)
  eyeY  = TFT_SCREEN_HEIGHT / 2 + 10;
  eyeX  = TFT_SCREEN_WIDTH / 2;

  // Starting gaze (center)
  lookX = 0;
  nextLook();
  nextBlinkTime();
  nextLookTime();
}

void nextLook() {
  lookX = random(-120, 120);  // -120 to +120 range
}

void nextBlinkTime() {
  blinkTime = millis() + random(MIN_BLINK_GAP, MAX_BLINK_GAP);
}

void nextLookTime() {
  lookChangeTime = millis() + random(1000, LOOK_DURATION);
}

void drawEyes() {
  tft.fillScreen(ST77XX_WHITE);

  // Header
  tft.fillRect(0, 0, TFT_SCREEN_WIDTH, 28, ST77XX_CYAN);
  tft.setTextColor(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 3);
  tft.println("  YOUR COMPANION");
  tft.drawLine(0, 28, TFT_SCREEN_WIDTH, 28, ST77XX_GRAY);

  // Update iris position
  unsigned long now = millis();

  // Blink logic
  if (!eyeBlink && !eyeClosing && now >= blinkTime) {
    eyeBlink   = true;
    eyeClosing = true;
    blinkHold  = now;
  }

  if (eyeBlink && eyeClosing && now >= blinkHold + BLINK_DURATION) {
    eyeClosing = false;
  }

  if (eyeBlink && !eyeClosing) {
    eyeBlink = false;
    nextBlinkTime();
  }

  // Change gaze direction
  if (now >= lookChangeTime) {
    nextLook();
    nextLookTime();
  }

  // Calculate iris (pupil) positions based on current gaze direction
  const int IRIS_R = 22;
  const int PUPIL_R = 11;

  int irisLX = eyeX - 50 + (lookX * 55 / 127);  // left eye iris X
  int irisRX = eyeX + 50 + (lookX * 55 / 127);  // right eye iris X

  // Eye radius
  const int EYE_R = 38;

  // Draw left eye
  drawOneEye(eyeX - 50, eyeY, EYE_R, IRIS_R, PUPIL_R, irisLX, eyeY);
  // Draw right eye
  drawOneEye(eyeX + 50, eyeY, EYE_R, IRIS_R, PUPIL_R, irisRX, eyeY);

  // Subtle mouth (happy curve)
  tft.drawRoundRect(eyeX - 20, eyeY + 70, 40, 20, 10, ST77XX_DARKGREY);
  tft.fillRoundRect(eyeX - 18, eyeY + 72, 36, 16, 8, ST77XX_WHITE);
}

void drawOneEye(int cx, int cy, int eyeR, int irisR, int pupilR, int irisDrawX, int irisDrawY) {
  // Outer white (sclera) — slightly oval for eye shape
  tft.fillCircle(cx, cy, eyeR, ST77XX_WHITE);
  tft.drawCircle(cx, cy, eyeR, ST77XX_BLACK);

  if (eyeClosing) {
    // Draw closed eyelid (horizontal line with curve)
    tft.fillRoundRect(cx - eyeR - 2, cy - 3, (eyeR + 2) * 2, 6, 3, ST77XX_BLACK);
    return;
  }

  // Iris
  tft.fillCircle(irisDrawX, irisDrawY, irisR, ST77XX_DARKGREY);
  tft.drawCircle(irisDrawX, irisDrawY, irisR, ST77XX_BLACK);

  // Pupil (very dark)
  tft.fillCircle(irisDrawX, irisDrawY, pupilR, ST77XX_BLACK);

  // Highlight / shine (two small white dots)
  tft.fillCircle(irisDrawX - 5, irisDrawY - 7, 4, ST77XX_WHITE);
  tft.fillCircle(irisDrawX + 4, irisDrawY + 3, 2, ST77XX_WHITE);
}

// ─── Spotify Mode (placeholder) ─────────────────────────
void drawSpotify() {
  tft.fillScreen(ST77XX_BLACK);

  // Header
  tft.fillRect(0, 0, TFT_SCREEN_WIDTH, 28, ST77XX_GREEN);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 3);
  tft.println("  SPOTIFY — TODO");
  tft.drawLine(0, 28, TFT_SCREEN_WIDTH, 28, ST77XX_GRAY);

  // Center text
  tft.setTextColor(ST77XX_GREEN);
  tft.setTextSize(4);
  tft.setCursor(80, 100);
  tft.println("SPOTIFY");

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(90, 170);
  tft.println("Coming Soon!");
}

// ─── Main Draw Switch ───────────────────────────────────
void drawMode() {
  unsigned long now = millis();

  switch (currentMode) {
    case MODE_DASHBOARD:
      drawDashboard();
      modeChangeTime = now;
      break;

    case MODE_CLOCK:
      drawClock();
      break;

    case MODE_PICTURE_FRAME:
      // Auto-advance images — only redraw the image when index changes
      if (now - picLastChange >= PICTURE_DELAY && totalPictures > 0) {
        int newPic = (picIndex + 1) % totalPictures;
        picIndex = newPic;
        picLastChange = now;
        prevPicIndex = picIndex - 1;  // force drawPictureFrame to re-render
      }
      break;

    case MODE_EYES:
      drawEyes();
      break;

    case MODE_SPOTIFY:
      drawSpotify();
      break;
  }
}

// ─── Mode Change with Tap ───────────────────────────────
void handleTaps() {
  if (touchConsumed) return;

  int tx, ty;
  if (!readTouch(tx, ty)) return;

  // Map XPT2046 ADC → screen pixel coordinates using calibration
  int sx = TOUCH_MAP(tx, TOUCH_X_MIN, TOUCH_X_MAX, 0, TFT_SCREEN_WIDTH);
  int sy = TOUCH_MAP(ty, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, TFT_SCREEN_HEIGHT);

  switch (currentMode) {
    case MODE_DASHBOARD:
      if (detectDashboardTap()) {
        // Determine which item was tapped
        const int cellW = 130, cellH = 105;
        const int gapX  = 40, gapY  = 45;
        const int ox   = 25,  oy   = 50;

        for (int i = 0; i < 4; i++) {
          int col = i % 2, row = i / 2;
          int cx = ox + col * (cellW + gapX);
          int cy = oy + row * (cellH + gapY);
          if (sx >= cx && sx <= cx + cellW && sy >= cy && sy <= cy + cellH) {
            currentMode = (Mode)i;
            if (i == MODE_PICTURE_FRAME) {
              // Count pictures on SD card
              totalPictures = 0;
              picIndex = 0;
              File root = SD.open("/");
              if (root) {
                File entry = root.openNextFile();
                while (entry) {
                  if (isBmpFile(entry.name())) {
                    totalPictures++;
                  }
                  entry.close();
                  entry = root.openNextFile();
                }
                root.close();
              }
              picLastChange = millis();
            }
            if (i == MODE_EYES) setupEyes();
            modeChangeTime = millis();
            touchConsumed = true;
            return;
          }
        }
      }
      break;

    case MODE_CLOCK:
    case MODE_EYES:
      // Any tap returns to dashboard
      touchConsumed = true;
      currentMode = MODE_DASHBOARD;
      modeChangeTime = millis();
      break;

    case MODE_PICTURE_FRAME:
      // Any tap returns to dashboard
      touchConsumed = true;
      currentMode = MODE_DASHBOARD;
      modeChangeTime = millis();
      break;

    case MODE_SPOTIFY:
      touchConsumed = true;
      currentMode = MODE_DASHBOARD;
      modeChangeTime = millis();
      break;
  }
}

// ─── Arduino Setup ──────────────────────────────────────
void setup() {
  Serial.begin(9600);
  Serial.println("TFT Hat Dashboard starting...");

  // Initialize display
  tft.init(TFT_SCREEN_WIDTH, TFT_SCREEN_HEIGHT);
  tft.setRotation(3);           // portrait, cable on right side
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(30, 120);
  tft.println("Initializing...");

  // Initialize SD card
  Serial.print("Initializing SD card... ");
  if (!SD.begin(SD_CS)) {
    Serial.println("FAILED!");
    tft.fillScreen(ST77XX_RED);
    tft.setTextSize(1);
    tft.setCursor(20, 100);
    tft.println("SD card init failed!");
    tft.setCursor(20, 130);
    tft.println("Check wiring & card");
    while (1) delay(1000);
  }
  Serial.println("OK");

  // Count pictures
  totalPictures = 0;
  File root = SD.open("/");
  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      if (isBmpFile(entry.name())) {
        totalPictures++;
        Serial.print("  Found: ");
        Serial.println(entry.name());
      }
      entry.close();
      entry = root.openNextFile();
    }
    root.close();
  }
  Serial.print("Total pictures: ");
  Serial.println(totalPictures);

  // Initialize touch pins
  initTouchPins();

  // Start on dashboard
  modeChangeTime = millis();
}

// ─── Arduino Loop ───────────────────────────────────────
void loop() {
  drawMode();

  // Handle touch input (every mode can exit via touch)
  handleTaps();

  // Reset touch consumed flag
  touchConsumed = false;

  // Debounce — prevent rapid mode switching
  if (millis() - modeChangeTime < TAP_DEBOUNCE) {
    delay(TAP_DEBOUNCE - (millis() - modeChangeTime));
  }
}

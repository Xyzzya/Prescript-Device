#include <TFT_eSPI.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Audio.h>
#include "../index.h"

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
Audio audio(1);

// Audio pins
#define I2S_DOUT  5
#define I2S_BCLK  4
#define I2S_LRC   7

// Button pins
#define BTN_RANDOM_PIN 20
#define BTN_PRESCRIPT_PIN 21
#define BTN_CLEAR_PIN 2

// Animation constants
const int ANIMATION_DURATION_MS = 1000;
const int ANIM_FPS = 60;
const int ANIM_FRAME_MS = 1000 / ANIM_FPS;
const char SCRAMBLE_CHARS[] = "0123456789!█▒░ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// State variables
uint32_t rng_state;
bool isAnimating = false;
String targetText = "";
String currentText = "";
unsigned long animationStartTime = 0;
unsigned long lastAnimFrame = 0;
bool audioUnlocked = false;
bool isPlayingScramble = false;

// ==================== RNG FUNCTIONS ====================
void seedRNG(String seedStr) {
  uint32_t h = 1779033703 ^ seedStr.length();
  for (unsigned int i = 0; i < seedStr.length(); i++) {
    h = h ^ seedStr[i];
    h = h * 3432918353;
    h = (h << 13) | (h >> 19);
  }
  h = h ^ (h >> 16);
  h = h * 2246822507;
  h = h ^ (h >> 13);
  h = h * 3266489909;
  h = h ^ (h >> 16);
  rng_state = h + 0x6D2B79F5;
}

uint32_t nextRandom() {
  uint32_t t = rng_state;
  t = t ^ (t >> 15);
  t = t * (t | 1);
  t = t ^ (t + (t ^ (t >> 7)) * (t | 61));
  rng_state = t + 0x6D2B79F5;
  return (t ^ (t >> 14));
}

int randInt(int maxExclusive) {
  return (maxExclusive <= 0) ? 0 : nextRandom() % maxExclusive;
}

String pick(const String arr[], int size) {
  return arr[randInt(size)];
}

String ordinalSuffix(int n) {
  int mod100 = n % 100;
  if (mod100 >= 11 && mod100 <= 13) return "th";
  switch (n % 10) {
    case 1: return "st";
    case 2: return "nd";
    case 3: return "rd";
    default: return "th";
  }
}

String capFirst(String s) {
  if (s.length() == 0) return s;
  s[0] = toupper(s[0]);
  return s;
}

// ==================== AUDIO FUNCTIONS ====================
void startScrambleSound() {
  if (!audioUnlocked) return;

  Serial.println("Starting scramble.mp3");
  isPlayingScramble = true;

  if (SPIFFS.exists("/scramble.mp3")) {
    audio.connecttoFS(SPIFFS, "/scramble.mp3");
  } else {
    Serial.println("Warning: /scramble.mp3 not found");
    isPlayingScramble = false;
  }
}

void playBeepSound() {
  if (!audioUnlocked) return;

  Serial.println("Playing beep.mp3");

  if (SPIFFS.exists("/beep.mp3")) {
    audio.connecttoFS(SPIFFS, "/beep.mp3");
  } else {
    Serial.println("Warning: /beep.mp3 not found");
  }
}

void stopScrambleSound() {
  Serial.println("Stopping scramble sound");
  isPlayingScramble = false;
  audio.stopSong();
}

// ==================== TEXT RENDERING ====================
void drawCenteredWrappedText(String text, int maxWidth, uint16_t color, bool glow = false, bool glitch = false) {
  if (text.length() == 0) return;

  int textSize = 1;
  if (text.length() <= 10) {
    textSize = maxWidth / (text.length() * 6);
    if (textSize > 8) textSize = 8;
    if (textSize < 2) textSize = 2;
  } else if (text.length() <= 40) {
    textSize = 2;
  }
  tft.setTextSize(textSize);

  int lineHeight = tft.fontHeight() + 4;
  const int MAX_LINES = 20;
  String lines[MAX_LINES];
  int lineCount = 0;

  String currentLine = "";
  String word = "";

  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text[i];

    if (c == ' ' || c == '\n' || i == text.length() - 1) {
      if (i == text.length() - 1 && c != ' ' && c != '\n') word += c;

      int wordWidth = tft.textWidth(word);
      int lineWidth = tft.textWidth(currentLine);
      int spaceWidth = tft.textWidth(" ");

      if (lineWidth > 0 && lineWidth + spaceWidth + wordWidth > maxWidth) {
        if (lineCount < MAX_LINES) lines[lineCount++] = currentLine;
        currentLine = word;
      } else {
        if (currentLine.length() > 0) currentLine += " ";
        currentLine += word;
      }

      if (c == '\n') {
        if (lineCount < MAX_LINES) lines[lineCount++] = currentLine;
        currentLine = "";
      }
      word = "";
    } else {
      word += c;
    }
  }
  if (currentLine.length() > 0 && lineCount < MAX_LINES) {
    lines[lineCount++] = currentLine;
  }

  int totalHeight = lineCount * lineHeight;
  int startY = (tft.height() - totalHeight) / 2;

  for (int i = 0; i < lineCount; i++) {
    audio.loop(); // Keep audio DMA filled during heavy drawing loop
    int lineWidth = tft.textWidth(lines[i]);
    int startX = (tft.width() - lineWidth) / 2;

    if (glow) {
      uint16_t glowColor = tft.color565(255, 80, 30);
      tft.setTextColor(glowColor);
      tft.setCursor(startX - 1, startY + i * lineHeight); tft.print(lines[i]);
      tft.setCursor(startX + 1, startY + i * lineHeight); tft.print(lines[i]);
      tft.setCursor(startX, startY + i * lineHeight - 1); tft.print(lines[i]);
      tft.setCursor(startX, startY + i * lineHeight + 1); tft.print(lines[i]);
    }

    tft.setTextColor(color);
    tft.setCursor(startX, startY + i * lineHeight);
    tft.print(lines[i]);

    if (glitch && randInt(10) > 4) {
      int numBlocks = randInt(2) + 1;
      for (int b = 0; b < numBlocks; b++) {
        int blockW = randInt(40) + 8;
        int maxBlockW = (startX + lineWidth) - startX;
        if (maxBlockW <= 0) maxBlockW = 1;
        int blockX = startX + randInt(maxBlockW);
        if (blockX + blockW > startX + lineWidth) blockW = (startX + lineWidth) - blockX;

        if (blockW > 0) {
          uint16_t bcolor = (randInt(4) == 0) ? tft.color565(100, 150, 255) : TFT_GOLD;
          if (randInt(2) == 0) {
            tft.fillRect(blockX, startY + i * lineHeight, blockW, lineHeight - 4, bcolor);
          } else {
            for (int yl = 0; yl < lineHeight - 4; yl += 2) {
              tft.drawFastHLine(blockX, startY + i * lineHeight + yl, blockW, bcolor);
            }
          }
        }
      }
    }
  }
}

void drawColoredBitmapCenter(const uint16_t* bitmap, int w, int h) {
  int x = (tft.width() - w) / 2;
  int y = (tft.height() - h) / 2;
  tft.pushImage(x, y, w, h, bitmap);
}

// ==================== ANIMATION ====================
char getRandomScrambleChar() {
  return SCRAMBLE_CHARS[randInt(strlen(SCRAMBLE_CHARS))];
}

void updateScrambleAnimation() {
  if (!isAnimating) return;

  unsigned long now = millis();
  if (now - lastAnimFrame < ANIM_FRAME_MS) return;
  lastAnimFrame = now;

  // Calculate progress based on elapsed time (0.0 to 1.0)
  float progress = min(1.0f, (float)(now - animationStartTime) / ANIMATION_DURATION_MS);
  int revealedChars = (int)(progress * targetText.length());

  // Build current frame
  String newText = "";
  for (unsigned int i = 0; i < targetText.length(); i++) {
    if (i < (unsigned int)revealedChars) {
      newText += targetText[i];
    } else {
      newText += (targetText[i] == ' ') ? ' ' : getRandomScrambleChar();
    }
  }

  currentText = newText;

  // Draw frame
  tft.fillScreen(TFT_BLACK);
  drawCenteredWrappedText(currentText, tft.width() - 10, TFT_WHITE, true, true);

  // Check if animation complete
  if (progress >= 1.0f) {
    Serial.println("Animation complete");
    isAnimating = false;
    currentText = targetText;
    tft.fillScreen(TFT_BLACK);
    drawCenteredWrappedText(targetText, tft.width() - 10, TFT_WHITE, true, false);

    stopScrambleSound();

    // Start playing beeps
    playBeepSound();
  }
}

void startScrambleAnimation(String finalText) {
  targetText = finalText;
  currentText = "";
  isAnimating = true;
  animationStartTime = millis();
  lastAnimFrame = millis() - ANIM_FRAME_MS - 1;

  updateScrambleAnimation();
  startScrambleSound();
}

// ==================== PRESCRIPT GENERATOR ====================
String generatePrescript() {
  int isRandom = randInt(150);
  if (isRandom == 4) {
    const String prescripts[] = {
      "Pack a lunchbox and consume it on top of a trash can in the streets at 1 PM today.",
      "Bake dacquoise while the hour hand rests between 7 and 8, and eat it while watching a movie.",
      "Pet quadrupedal animals five times.",
      "At the railing on the roof of a building, shout out the name of the person you dislike.",
      "On the morning after receiving the Prescript, drink three cups of water as soon as you get up.",
      "When hungry, consume a cheeseburger with added onion.",
      "Fold thirty-nine paper cranes and throw them from the rooftop.",
      "When your eyes meet another person's, nod at them.",
      "Order something online you don't need.",
      "Go to sleep. You must dream about a bird locked in a cage.",
      "Visit the Index and receive your personalized reward.",
      "Repeat yesterday's prescript.",
      "Purchase something you will never use.",
      "If it is raining, open your eyes and have a race with someone who looks lost."
    };
    return pick(prescripts, 14);
  }

  const String locations[] = {
    "beach", "store", "house", "apartment complex", "ocean", "hotel", "motel",
    "restaurant", "theater", "library", "school", "factory", "alleyway",
    "empty field", "graveyard", "basement", "alley", "parking lot", "office",
    "workshop", "backstreet", "train platform", "lighthouse"
  };

  const String colors[] = {
    "blue ", "black ", "red ", "brown ", "yellow ", "pink ", "white ",
    "grey ", "green ", "purple ", "orange ", "dark green ", "light blue ",
  };

  const String personTypes[] = {
    "stranger", "friend", "person", "neighbor", "visitor", "passerby",
    "adult", "child", "elder", "teacher", "student", "someone", "figure",
    "family member", "local", "outsider", "witness"
  };

  const String personIds[] = {
    "ugly", "beautiful", "tall", "short", "kind", "mean", "friendly",
    "quiet", "loud", "nervous", "calm", "young", "old", "pale", "tired", ""
  };

  const String clothing[] = {
    "hat", "coat", "jacket", "shirt", "dress", "pants", "shoes", "gloves", "scarf", "mask"
  };

  const String activities[] = {
    "talk about your fears", "shake hands", "walk around", "sit down",
    "stand still", "look around", "wait quietly", "play a game",
    "have a conversation", "share a secret", "ask a question", "watch the sunset",
    "drink coffee", "sleep with them", "follow them", "ignore them"
  };

  const String starters[] = {
    "", "", "", "", "", "pause briefly, ", "wait a moment, ", "look around, ",
    "take a breath and ", "stand still and ", "look away and ", "then "
  };

  const String taskBegins[] = {
    "", "", "", "", "", "After waking up, ", "At noon, ", "Tonight, ",
    "Within the next hour, ", "When you leave your home, ", "If it is raining, ",
    "At midnight, ", "Before sunset, "
  };

  String script = pick(taskBegins, 13);
  String finalStarter = pick(starters, 12);
  if (finalStarter != "") script += finalStarter;

  int taskType = randInt(4);
  int number1 = randInt(100) + 1;
  String end1 = ordinalSuffix(number1);

  if (taskType == 0) {
    String personId = pick(personIds, 16);
    script += "find the " + String(number1) + end1 + " ";
    if (personId != "") script += personId + " ";
    script += pick(personTypes, 17) + " you see and " + pick(activities, 16) + ".";
  } else if (taskType == 1) {
    script += "go to a " + pick(locations, 23) + " and " + pick(activities, 16) + ".";
  } else if (taskType == 2) {
    String color = pick(colors, 14);
    script += "find someone wearing a " + color + pick(clothing, 10) + " and " + pick(activities, 16) + ".";
  } else {
    script += "approach the " + pick(personIds, 16) + " " + pick(personTypes, 17) +
              " at a " + pick(locations, 23) + " and " + pick(activities, 16) + ".";
  }

  if (randInt(10) < 3) {
    const String followups[] = {
      " Then go home.", " Do not look back.", " Return immediately after.",
      " Remember their face.", " Forget this interaction."
    };
    script += pick(followups, 5);
  }

  return capFirst(script);
}

String generateRandom8Chars() {
  const String charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789@#$%&()[]{}|,./<>";
  String result = "";
  for (int i = 0; i < 8; i++) {
    result += charset[esp_random() % charset.length()];
  }
  return result;
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nESP32-S3 Prescript Generator Starting...");

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS initialization failed!");
  } else {
    Serial.println("SPIFFS initialized");
  }

  Serial.println("Initializing display...");
  SPI.begin(12, -1, 11, -1);
  tft.init();
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  pinMode(BTN_RANDOM_PIN, INPUT_PULLUP);
  pinMode(BTN_PRESCRIPT_PIN, INPUT_PULLUP);
  pinMode(BTN_CLEAR_PIN, INPUT_PULLUP);

  prefs.begin("prescript", false);

  Serial.println("Initializing audio...");
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(21);
  audio.forceMono(true);

  tft.fillScreen(TFT_BLACK);
  drawColoredBitmapCenter(index_new, INDEX_NEW_WIDTH, INDEX_NEW_HEIGHT);

  Serial.println("Prescript Generator Ready");
}

// ==================== MAIN LOOP ====================
void loop() {
  static bool btnRandomPressed = false;
  static bool btnPrescriptPressed = false;
  static bool btnClearPressed = false;
  static unsigned long lastDebounce = 0;
  const unsigned long debounceDelay = 200;

  audio.loop();

  if (isPlayingScramble && isAnimating) {
    if (!audio.isRunning()) {
      if (SPIFFS.exists("/scramble.mp3")) {
        audio.connecttoFS(SPIFFS, "/scramble.mp3");
      }
    }
  }

  if (isAnimating) {
    updateScrambleAnimation();
    return;
  }

  bool stateRandom = digitalRead(BTN_RANDOM_PIN) == LOW;
  bool statePrescript = digitalRead(BTN_PRESCRIPT_PIN) == LOW;
  bool stateClear = digitalRead(BTN_CLEAR_PIN) == LOW;
  unsigned long currentTime = millis();

  if (statePrescript && !btnPrescriptPressed && (currentTime - lastDebounce > debounceDelay)) {
    btnPrescriptPressed = true;
    lastDebounce = currentTime;
    audioUnlocked = true;

    Serial.println("Generating prescript...");
    rng_state = esp_random();
    startScrambleAnimation(generatePrescript());
  } else if (!statePrescript) {
    btnPrescriptPressed = false;
  }

  if (stateRandom && !btnRandomPressed && (currentTime - lastDebounce > debounceDelay)) {
    btnRandomPressed = true;
    lastDebounce = currentTime;
    audioUnlocked = true;

    Serial.println("Generating random characters...");
    rng_state = esp_random();
    startScrambleAnimation(generateRandom8Chars());
  } else if (!stateRandom) {
    btnRandomPressed = false;
  }

  if (stateClear && !btnClearPressed && (currentTime - lastDebounce > debounceDelay)) {
    btnClearPressed = true;
    lastDebounce = currentTime;
    audioUnlocked = true;

    Serial.println("Clear button pressed!");
    rng_state = esp_random();
    startScrambleAnimation("_CLEAR._");
  } else if (!stateClear) {
    btnClearPressed = false;
  }
}

// ==================== AUDIO CALLBACKS ====================
void audio_info(const char *info) {
  Serial.print("audio_info: ");
  Serial.println(info);
}

void audio_eof_mp3(const char *info) {
  // Unused now as we use audio.isRunning()
}

void audio_eof_stream(const char *info) {
  // Unused
}
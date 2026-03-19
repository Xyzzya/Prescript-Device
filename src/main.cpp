#include <TFT_eSPI.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Audio.h>

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
Audio audio;

// ==================== BMP RENDERING ====================
// These read 16- and 32-bit types from the SD card file.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.
uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void drawBmp(const char *filename, int16_t x, int16_t y) {
  if ((x >= tft.width()) || (y >= tft.height())) return;

  fs::File bmpFile = SPIFFS.open(filename, "r");
  if (!bmpFile) {
    Serial.print("File not found: "); Serial.println(filename);
    return;
  }

  if (read16(bmpFile) == 0x4D42) { // BMP signature
    read32(bmpFile);
    read32(bmpFile);
    uint32_t seekOffset = read32(bmpFile);
    read32(bmpFile);
    int32_t w = read32(bmpFile);
    int32_t h = read32(bmpFile);

    if ((read16(bmpFile) == 1) && (read16(bmpFile) == 24) && (read32(bmpFile) == 0)) { // 24-bit, uncompressed
      y += h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      bmpFile.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3];

      for (int row = 0; row < h; row++) {
        bmpFile.read(lineBuffer, sizeof(lineBuffer));
        uint8_t* bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        for (int col = 0; col < w; col++) {
          uint8_t b = *bptr++;
          uint8_t g = *bptr++;
          uint8_t r = *bptr++;
          *tptr++ = tft.color565(r, g, b);
        }
        tft.pushImage(x, y--, w, 1, (uint16_t*)lineBuffer);
        bmpFile.seek(padding, fs::SeekCur);
      }
      tft.setSwapBytes(oldSwapBytes);
    } else {
      Serial.println("BMP format not supported (must be 24-bit uncompressed)");
    }
  } else {
    Serial.println("File is not a BMP");
  }
  bmpFile.close();
}

void drawBmpCenter(const char *filename) {
  fs::File bmpFile = SPIFFS.open(filename, "r");
  if (!bmpFile) {
    Serial.print("drawBmpCenter: File not found: ");
    Serial.println(filename);
    return;
  }

  uint16_t sig = read16(bmpFile);
  if (sig == 0x4D42) {
    read32(bmpFile);
    read32(bmpFile);
    read32(bmpFile);
    read32(bmpFile);
    int32_t w = read32(bmpFile);
    int32_t h = read32(bmpFile);
    bmpFile.close();

    Serial.printf("drawBmpCenter: BMP %s is %dx%d\n", filename, w, h);

    int16_t x = (tft.width() - w) / 2;
    int16_t y = (tft.height() - h) / 2;
    drawBmp(filename, x, y);
  } else {
    Serial.printf("drawBmpCenter: Not a BMP, signature is %04X\n", sig);
    bmpFile.close();
  }
}

// Audio pins
#define I2S_DOUT  5
#define I2S_BCLK  4
#define I2S_LRC   7

// Button pins
#define BTN_RANDOM_PIN 21
#define BTN_PRESCRIPT_PIN 40
#define BTN_CLEAR_PIN 20

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

  audio.connecttoFS(SPIFFS, "/scramble.mp3");

  // Pre-fill the I2S DMA buffer by pumping the loop heavily 
  // before we let the heavy TFT drawing routines block the CPU.
  for (int i = 0; i < 50; i++) {
    audio.loop();
  }
}

void playBeepSound() {
  if (!audioUnlocked) return;

  Serial.println("Playing beep.mp3");

  audio.connecttoFS(SPIFFS, "/beep.mp3");
}

void stopScrambleSound() {
  Serial.println("Stopping scramble sound");
  isPlayingScramble = false;
  audio.stopSong();
}

// ==================== TEXT RENDERING ====================
void drawCenteredWrappedText(String text, int maxWidth, uint16_t color, bool glow = false, bool glitch = false) {
  if (text.length() == 0) return;

  int textSize = 2;
  if (text.length() <= 10) {
    textSize = maxWidth / (text.length() * 6);
    if (textSize > 8) textSize = 8;
    if (textSize < 2) textSize = 2;
  }
  
  tft.setTextSize(textSize);
  int lineHeight = tft.fontHeight() + 4;
  const int MAX_LINES = 20;
  String lines[MAX_LINES];
  int lineCount = 0;

  auto wrapText = [&]() {
    lineCount = 0;
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
  };

  wrapText();

  if (textSize > 1 && lineCount * lineHeight > tft.height()) {
    textSize = 1;
    tft.setTextSize(textSize);
    lineHeight = tft.fontHeight() + 4;
    wrapText();
  }

  int totalHeight = lineCount * lineHeight;
  int startY = (tft.height() - totalHeight) / 2;

  for (int i = 0; i < lineCount; i++) {
    audio.loop(); // Keep audio DMA filled during heavy drawing loop
    int lineWidth = tft.textWidth(lines[i]);
    int startX = (tft.width() - lineWidth) / 2;

    if (glow) {
      uint16_t glowColor = tft.color565(30, 80, 255);
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

  startScrambleSound();
  updateScrambleAnimation();
}

// ==================== PRESCRIPT GENERATOR ====================
String getDirection() {
  const char* arr[] = {"north", "northeast", "east", "southeast", "south", "southwest", "west", "northwest"};
  return arr[randInt(8)];
}

String getDay() {
  const char* arr[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
  return arr[randInt(7)];
}

String getColor() {
  const char* arr[] = {"red", "blue", "yellow", "green", "purple", "orange", "white", "cyan", "pink", "light blue", "golden", "light green", "light purple", "light orange", "light pink"};
  return arr[randInt(15)];
}

String getFood() {
  int r = randInt(13);
  if (r == 12) return getColor() + "-colored foods";
  const char* arr[] = {"pasta", "spaghetti", "pizza", "a risotto", "sushi", "bread", "a baguette", "curry", "a HamHamPangPang sandwich", "a cheeseburger", "plain white rice", "fried rice", ""};
  return arr[r];
}

String getVerb() {
  int r = randInt(19);
  if (r == 9) return "give " + getColor() + " flowers to ";
  const char* arr[] = {
    "help ", "hug ", "trade a precious item with ", "gouge out the left eye of ",
    "gouge out the right eye of ", "exchange vows with ", "engage in a fistfight with ",
    "rip out the spine of ", "crush the skull of ", "",
    "take a walk with ", "hold hands with ", "buy dinner to ", "eat a meal with ",
    "head out on an extended vacation with ", "ask what is the name of ",
    "dance with ", "send a love letter to ", "send a handwritten confession letter to "
  };
  return arr[r];
}

String getNumberStr() {
  const char* arr[] = {"first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth", "eleventh", "twelfth", "thirteenth"};
  return arr[randInt(13)];
}

String getThingToBe() {
  const char* arr[] = {
    "animal", "dog", "cat", "child", "office worker", "Fixer", "saint", "priest",
    "writer", "artist", "twin sibling", "oldest sibling", "youngest sibling",
    "Syndicate member", "Thumb member", "Middle member", "Index member", "Ring member",
    "Thumb Soldato", "Thumb Capo", "Index Proselyte", "Index Messenger",
    "Middle Little Sibling", "Middle Young Sibling", "Ring Student", "Ring Docent",
    "person", "quadruped animal", "insect", "ant", "person walking a dog", "Index Proxy"
  };
  return arr[randInt(32)];
}

String getRelationship() {
  int r = randInt(28);
  if (r == 5) return "the " + getNumberStr() + " " + getThingToBe() + " you see";
  if (r == 6) return "the " + getNumberStr() + " " + getThingToBe() + " you meet";
  if (r == 10) return "every " + String(randInt(20) + 1) + " year old in your neighborhood";
  if (r == 14) return "your " + getNumberStr() + " neighbor down the road";
  if (r == 27) return "the first person you see wearing " + getColor();

  const char* arr[] = {
    "your next-door neighbor", "your nearest coworker", "your nearest Index member",
    "a total stranger", "your best friend", "", "", "your dreams",
    "the one you couldn't kill", "someone who wronged you", "",
    "the face in the mirror", "your mother", "your father", "",
    "the person you hate the most", "the person you love the most",
    "residents that live in the same building as you", "your next-door neighbors",
    "the leader of the closest Syndicate", "one you would consider a friend",
    "one you would consider a friend, but they do not", "someone that considers you a friend, but you do not",
    "your doctor", "a Color Fixer", "a Grade 5 Fixer", "a Grade 8 Fixer", ""
  };
  return arr[r];
}

String getLocation1() {
  int r = randInt(11);
  if (r == 9) return getRelationship() + "'s house";
  const char* arr[] = {
    "hotel", "motel", "alleyway", "office building", "Syndicate den", "bakery",
    "convenience store", "murky alleyway", "your house", "", "your workplace"
  };
  return arr[r];
}

String getLocation2() {
  int r = randInt(6);
  if (r == 0) return "District " + String(randInt(25) + 1);
  if (r == 1) return "nearest " + getLocation1() + " to you";
  if (r == 2) return "nearest " + getLocation1() + " " + getDirection() + " of you";
  const char* arr[] = { "", "", "", "on top of a trash can", "on a rooftop", "on the rooftop of the tallest building you can find in your District" };
  return arr[r];
}

String getRandomTimestamp() {
  const char* arr[] = {" hours", " days", " weeks", " months"};
  return String(randInt(12) + 1) + arr[randInt(4)];
}

String getSingle() {
  int r = randInt(79);
  if (r == 47) return "Open and close a " + getDirection() + "-facing window " + String(randInt(20) + 1) + " times, then close the curtains for the rest of the week.";
  if (r == 50) return "Gut a fish and place one ounce of the meat on " + getRelationship() + "'s pillow, along with the bones. Consume the rest raw without exception.";
  if (r == 52) return "Immediately locate and play the nearest physical copy of a video game. If you cannot complete it within 24 hours, dye your hair " + getColor() + ".";
  if (r == 55) return "Solve " + String(randInt(20) + 1) + " nonogram puzzles in a row.";
  if (r == 58) return "Make out an image of something in the trees, then fear it for " + getRandomTimestamp() + ".";
  if (r == 63) return "Do " + String(randInt(20) + 1) + " cartwheels immediately.";
  if (r == 64) return "Eat " + String(randInt(20) + 1) + " peas. Be sure to peel each one and separate the two halves.";
  if (r == 67) return "For the next twelve and three-forths of an hour, only walk with your feet faced " + getDirection() + ". They are not allowed to be offset from this direction by up to five degrees. When the time ends, repeat the Prescript, although walking only using your heels.";
  if (r == 71) return "Listen to the sound of the waves for " + String(randInt(12) + 1) + " hours.";
  if (r == 76) return "Count the teeth of the next " + String(randInt(20) + 1) + " people you meet. If any have fewer than 32, replace the missing ones with your own teeth.";
  if (r == 78) return "Tell " + getRelationship() + " about the future. Ensure it comes to pass.";

  const char* arr[] = {
    "Play rock paper scissors with the third person you meet and play rock. If you win, pull out 59 strands of their hair. Then, apply seafood-cream pasta sauce with mealworms fed on styrofoam to it three times, and eat it with a fork.",
    "Put 3 needles in your neighbor's birthday cake.",
    "Make risotto out of sewer water, and feed it to the person next door.",
    "Rip out the spine of someone who wronged the Index.",
    "Kill the painting you've drawn.",
    "Move an unicorn plushie to a park.",
    "See green from a white wall.",
    "Stand on any three-way intersection at 3:38 tomorrow, look to the east, and wave seven times.",
    "Exchange the left leg of the fourteenth person you come across today with the right leg of the twenty-sixth person you run into.",
    "When you see a person on a three-way intersection waving their hand seven times, follow them to their house.",
    "Kill all the doors of the ceiling.",
    "Consume your vocal cords, boiled for 14 seconds in simmering salt water, as part of your dinnertime meal.",
    "Climb to the rooftop of a building 3 stories or higher and wave your hand while looking down for 1 minute.",
    "Enter as the third customer of the day to a restaurant that sells chickens, and leave last.",
    "Start a game of hide-and-seek with at least three people, then return home as the \"it.\"",
    "Read six books, then visit the District that appears in the last book read. No time limit.",
    "Kill the liar within the alley within alleys.",
    "Pack a lunchbox and consume it on top of a trash can in the streets of District 11 at 1 PM today.",
    "Bake dacquoise while the hour hand rests between 7 and 8, and eat it while watching a movie.",
    "Initiate a game of Never Have I Ever with the first five people you encounter. When one folds a finger, break it.",
    "Neatly clip the nails of the sixty-second person you come across.",
    "Pet quadrupedal animals five times.",
    "Spin a wheel and throw a cake at the person determined by the result.",
    "Consume eight crabs stored at room temperature and ripe persimmon at once.",
    "At the railing on the roof of a building, shout out the name of the person you dislike, then jump off. The height of the building does not matter.",
    "After a meal, discard all dishes that were used to serve it.",
    "On the morning after receiving the Prescript, drink three cups of water as soon as you get up.",
    "Race against residents that live in the same building as you to District 7. Measure the distance every twenty-three minutes and disqualify the one farthest away from the destination.",
    "Within three days, knit a scarf with a butterfly pattern.",
    "Dial any number. Give a New Year’s greetings and words of blessing to whoever receives the call.",
    "When hungry, consume a Cheeki’s cheeseburger with added onion.",
    "Fold 39 paper cranes and throw them from the rooftop.",
    "At work, cut the ear of the first person to fulminate against you.",
    "When your eyes meet another person’s, nod at them.",
    "Return to your home this instant. You may leave once a dog barks in front of your house one time.",
    "Wear light green clothing and take 10 steps in a triangle-shaped alley.",
    "When penetrating the lungs with a stiletto, lay vertical the end, insert up to the wick.",
    "Destroy the sound, crush flat the thought.",
    "Sleep for a total of 800 hours per day.",
    "Drink a liter of milk. Warm up before you go play.",
    "Only read, write, or pull the trigger with your right hand.",
    "In 30 minutes, find a groom or bride - bonus if brunette. In 90 hours, spill their insides. Paint the room picture sque.",
    "In 400000 meters, turn right.",
    "Do not go home until you finish reading the value of e.",
    "Grind upon dawn's rail.",
    "Shove an entire orange inside a grapefruit, then hit it with a hammer repeatedly while meowing at 9:34 on a Friday night.",
    "Untie every shoelace in your household. You may re-tie them after you are next greeted by name.",
    "", 
    "Walk to the nearest intersection and head south for 17 blocks. Within 13 days, descend to the lowest floor of the building you arrive at. If you meet a man there, shake his hand.",
    "Start drawing, and do nothing else until you're done. Print it out and eat it.",
    "", 
    "Drink eight full cups of water within a twenty four hour period.",
    "", 
    "Walk up to the front door of a random house on your street. If they have a door camera, perform a magic trick while staring into the camera, then leave. Do not make a sound.",
    "The next time you lose in a card game, eat every card in the winner's hand. If they attempt to stop you, also eat their hand.",
    "", 
    "Go out for a walk with your left shoe untied, and when someone notices, tell them that their right shoe is untied.",
    "At the crossroads, do not turn left.",
    "", 
    "Take a Proxy out to a nice restaurant of their choosing. If you both order the same thing, leave without paying.",
    "Recite the value of e.",
    "At daybreak, noon, and sunset, go outside and bask in the sunlight, take a picture with the sun, and wave at the sun.",
    "Fill a blender with even amounts of ketchup, hummus and molasses. Blend for 7 and a half minutes, and chug the result straight from the appliance.",
    "Purchase tickets for a flight that is over 3 hours long and ensure you will sit next to two strangers in the middle seat. Quietly smack your lips every 5-10 minutes for the duration of the flight.",
    "", 
    "", 
    "Alter your body. Mirror the Fixer lost to the Library.",
    "Show this Prescript to your nearest Proxy. They will understand what it means.",
    "", 
    "Remember to aim for the heart.",
    "Read out the entirety of 'Prayer For Loving Sorrow'.",
    "Watch a Tortoise Spiral reach its end.",
    "Wordlessly hug someone who looks like they need it the least.",
    "Invent a new gender-neutral alternative to niece/nephew within the next ten minutes. Consider this Prescript failed if the person nearest in proximity to you doesn't like it.",
    "", 
    "Draw the muse that lives in your head.",
    "Spin around counterclockwise until you see triple, then stab the fake illusions until they bleed shadows.",
    "On the third day of the next month, walk past 3 people who are holding hands, tap their shoulders 3 times and offer a handshake. If they don't comply, you must start over.",
    "Doodle something in the feedback page of this website.",
    "While carrying a pile of books, crash into someone.",
    "Challenge every right-handed person who is not an Index member that you encounter to a staring contest, until you lose. Give meaning to the person who beat you.",
    "", 
    "Next time you are in a lifethreatening situation, take a 20 minute break. You must act as if nothing fazes you for the duration of the break.",
    "Go to a graveyard and lay across the {num} grave you see. Before 30 minutes have passed, make sure to water your dreams.",
    "Let her voice reach you. Do as she says.",
    "Let her voice reach you. Deny her will.",
    "Bring your daughter back home.",
    "" 
  };
  
  String res = String(arr[r]);
  if (res == "") return "Recite the value of e.";
  res.replace("{num}", getNumberStr());
  return res;
}

String getTimeFirstSlot() {
  int r = randInt(28);
  if (r == 3) return "In " + getRandomTimestamp() + ", ";
  if (r == 5) return "For the next " + getRandomTimestamp() + ", ";
  if (r == 10) return "Within " + getRandomTimestamp() + ", ";
  if (r == 12) return "Next time you cross paths with " + getRelationship() + ", ";
  if (r == 13) return "When your eyes meet with " + getRelationship() + ", ";
  if (r == 16) return "Within " + String(randInt(20) + 1) + " hours, ";
  if (r == 17) return "Within " + getRandomTimestamp() + ", on a " + getDay() + ", ";
  if (r == 27) return "Next " + getDay() + ", ";

  const char* arr[] = {
    "Tomorrow, ", "This evening, ", "Next week, ", "", "As soon as possible, ", "",
    "On the morning after receiving the Prescript, ",
    "On the afternoon after receiving the Prescript, ",
    "On the evening after receiving the Prescript, ",
    "At midnight, ", "", "During the Night in the Backstreets, ", "", "",
    "When you are next given a Prescript, ", "Next time you encounter someone, ",
    "", "", "Until someone stops you, ", "The next time you are in a life threatening situation, ",
    "Before the next snowfall, ", "Before the next time it rains, ", "The next time you fall in love, ",
    "During your next meal, ", "After you spot a rainbow, ", "When at work, ", "When in a classroom, ", ""
  };
  return arr[r];
}

String getActivitySecondSlot() {
  int r = randInt(74);
  if (r == 2) return "jump from a roof. It must be at least " + String(randInt(20) + 1) + " meters tall.";
  if (r == 7) return "help " + getRelationship() + " cross the street.";
  if (r >= 8 && r <= 13) return getVerb() + getRelationship() + ".";
  if (r == 16) return "give " + getRelationship() + " a manicure.";
  if (r == 17) return "wave to " + getRelationship() + " " + String(randInt(20) + 1) + " times  a day.";
  if (r == 18) return "nod to " + getRelationship() + " " + String(randInt(20) + 1) + " times.";
  if (r == 19) return "fight with " + getRelationship() + " to the death.";
  if (r == 20) return "bake cookies and give them to " + getRelationship() + ".";
  if (r == 21) return "take " + String(randInt(20) + 1) + " steps in an alley.";
  if (r == 22) return "head to (a) " + getLocation1() + " with " + getRelationship() + ".";
  if (r == 23) return "run for " + String(randInt(20) + 1) + " hours facing " + getDirection() + ". If anyone stands in your way, cut them down.";
  if (r == 24) return "walk for " + String(randInt(20) + 1) + " hours facing " + getDirection() + ". If anyone stands in your way, cut them down.";
  if (r == 25) return "apply seafood-cream pasta sauce with mealworms fed on styrofoam to " + getFood() + " three times. Eat it with a fork.";
  if (r == 27) return "tell " + getRelationship() + " your darkest secret.";
  if (r == 31) return "board a WARP train with " + getRelationship() + ".";
  if (r == 33) return "head " + getDirection() + " for " + String(randInt(20) + 1) + " blocks.";
  if (r == 36) return "steal copper wiring from the house of " + getRelationship() + ".";
  if (r == 47) return "look out your window and throw your least valuable possession towards the head of " + getRelationship() + ". If it hits, drink a cup of water. If it misses, your next Prescript shall be done twice.";
  if (r == 50) return "play tag with " + getRelationship() + ", and make sure you win.";
  if (r == 51) return "play tag with " + getRelationship() + ". You do not need to win.";
  if (r == 57) return "for " + String(randInt(20) + 1) + " minutes, read a book without using any devices.";
  if (r == 65) return "find " + getRelationship() + ", and speak your mind to them.";
  if (r == 68) return "send a message to " + getRelationship() + " asking about their day.";
  if (r == 69) return "invite " + getRelationship() + " inside your home.";
  if (r == 72) return "make someone up in your head, and pretend you are them in your next " + String(randInt(20) + 1) + " interactions. They must be very different from the you you are.";

  String arr[] = {
    "eat bitter.", "jump from a roof. The height does not matter.", "",
    "fetch a cup of water.", "drink a cup of sewage.", "grab a coffee with a friend.",
    "shoot yourself.", "", "", "", "", "", "", "", "immigrate to a different district.",
    "look at yourself in the mirror.", "", "", "", "", "", "", "", "", "", "",
    "spill your blood into the nearest toilet.", "",
    "buy tickets to a WARP train ride. They must be Economic Class.",
    "buy tickets to a WARP train ride. They must be First Class.",
    "board the next WARP train.", "",
    "wish a happy new year to a couple walking a dog.", "", "start a bug band.",
    "run into traffic.", "",
    "immediately go and knock on your neighbor's door. If they answer, exchange Prescripts with them, then follow their Prescript to the letter.",
    "destroy fate.", "convince someone else to fail this Prescript.",
    "recite the value of e.", "sleep in opposite sides of the bed and in different positions.",
    "leave all the houselights on.", "go to sleep, and do not wake until the following morning.",
    "seek that which will fill your heart.", "apply for a job.", "rip the blinds.",
    "sit on a comfortable chair and sleep. When you wake up, act like you have been awake the whole time.",
    "", "if you are in your bedroom, state your first thought out loud. Otherwise, jump around the first thing you see on toes.",
    "get the name of the next of kin from the next opponent you defeat. If they do not have one, behead them.",
    "", "", "weep from joy, sorrow, and fear.", "give yourself a secret name.",
    "drink from a puddle in the street.", "pet the nearest dog, even if it means breaking into somewhere.",
    "brew a cup of oolong tea.", "brew a cup of black tea.", "",
    "spend 23 hours as usual. In the 24th, engage in something you've never done before.",
    "tap the shoulder of the person in front or behind you, then tell them that the person next to you said they like them.",
    "insult a member of the Thumb, and blame it on someone else.",
    "insult a member of the Middle, and blame it on someone else.",
    "learn how to crochet with your fingers.", "paint the sky as you see it in the moment.",
    "do the macarena.", "do a backflip.", "", "speak only in numbers until you are told to stop.",
    "hand this Prescript to a Proxy. They will understand what it means.", "", "",
    "take a bubble bath.", "search for a District that doesn't exist.", "", "cross something off your bucket list."
  };
  return arr[r];
}

String getActivityFirstSlot() {
  int r = randInt(48);
  if (r == 7) return "Read a book with a " + getColor() + " cover, ";
  if (r == 8) return "Rip off the arms of " + getRelationship() + ", ";
  if (r == 9) return "Rip off the legs of " + getRelationship() + ", ";
  if (r == 10) return "Look " + getDirection() + " for " + getRandomTimestamp() + ", without ever moving your head away, ";
  if (r == 19) return "Find " + getRelationship() + ", ";
  if (r == 20) return "Invite " + getRelationship() + " to your home, ";
  if (r == 22) return "Walk into the nearest bookstore, pick the " + getNumberStr() + " book you see, read it cover to cover, ";
  if (r == 23) return "Bring the head of " + getRelationship() + " to " + getRelationship() + ", ";
  if (r == 24) return "Eat only " + getColor() + " colored foods for " + getRandomTimestamp() + ", ";
  if (r == 25) return "Eat nothing but " + getFood() + " for " + getRandomTimestamp() + ", ";
  if (r == 27) return "Convince " + getRelationship() + " to " + getActivitySecondSlot() + " Let it sink in, ";
  if (r == 28) return "Order " + getFood() + " and give it to " + getRelationship() + ", ";
  if (r == 29) return "Go to the nearest fast food restaurant, order the " + getNumberStr() + " item your eyes fall on, ";
  if (r == 31) return "During the Night in the Backstreets, kill " + String(randInt(20) + 1) + " or more Sweepers, ";
  if (r == 33) return "Record a bird for " + String(randInt(20) + 1) + " minutes, ";
  if (r == 35) return "Break the " + getNumberStr() + " clock you see, ";
  if (r == 42) return "Write a song for " + getRelationship() + ", ";
  if (r == 46) return "List " + String(randInt(20) + 1) + " positive things about yourself in a napkin, ";
  if (r == 47) return "Read the entirety of the " + getNumberStr() + " random article you draw on Wikipedia, ";

  const char* arr[] = {
    "Fix your posture, ", "Forget your own reflection, ", "Take a selfie, ",
    "Turn off your nearest computer, ", "Take a shower, ", "Ask for a different prescript, ",
    "Get banned from your most used social media, ", "", "", "", "",
    "Burn the last gift you received, ", "Pack a lunchbox, ", "Pack for a short trip, ",
    "Pack for a long trip, ", "Flip a coin until you get heads, ", "Flip a coin until you get tails, ",
    "Walk to the nearest intersection, ", "Reap what you've sowed, ", "", "",
    "Order a rice dish, count all of the individual grains, ", "", "", "", "",
    "Buy a food magazine and underline every instance of the word \"pepper\", ", "", "", "",
    "Find yourself in the creases of the couch, ", "", "Picture yourself in your favorite Association's uniform, ",
    "", "Ask someone about their 'ideal', ", "", "Go to the closest school, ",
    "Clap without a sound, ", "Name five things you can't see, ",
    "Run late to an important event with a piece of toast in your mouth, ",
    "Express an opinion on social media you disagree with, ",
    "Do a ten-pull on your latest played gacha game, ", "",
    "Order something different from the usual in your most frequented restaurant, ",
    "Brew a cup of green tea, ", "Dance to the melody in your head, ", "", "",
    "Play a game you have not opened in years, "
  };
  return arr[r];
}

String getMarker() {
  const char* arr[] = {
    "then ", "and immediately afterwards, ", "but before you do that, ",
    "and once you do, ", "pretend nothing happened, and ", "and ",
    "let it sink in, and ", "but if you can't, "
  };
  return arr[randInt(8)];
}

String getPostscript() {
  int r = randInt(24);
  if (r == 1) return " You must be wearing " + getColor() + ".";
  if (r == 10) return " Afterwards, " + getActivitySecondSlot();
  if (r == 11) return " Once you're done, " + getActivitySecondSlot();
  if (r == 13) return " Before you do that, " + getActivitySecondSlot();
  if (r == 14) return " Time limit: " + String(randInt(12) + 1) + getRandomTimestamp() + ".";
  if (r == 23) return " You can bring " + getRelationship() + " along.";

  const char* arr[] = {
    " No time limit.", "", " Remember to wear glasses when you do.", " Leave no witnesses.",
    " Return home as soon as you can.", " Close the door immediately.",
    " Be sure to keep an eye behind you.", " Take a break when you're done.",
    " The next time you receive a Prescript, remember you have to disobey it.",
    " You must hold your breath for the duration.", "", "", " You must not blink.", "", "",
    " Don't let yourself be caught.", " Shower immediately afterwards.",
    " Once this Prescript is done, you may consider yourself a Proselyte of the Index.",
    " Once this Prescript is done, you will be promoted to Proxy. Congratulations.",
    " Once this Prescript is done, you will be promoted to Messenger. Congratulations.",
    " Time limit: before you next spot three crows on the same electric wire.",
    " Ask yourself if it was worth it.", " Invite a friend to do the same.", ""
  };
  return arr[r];
}

String generatePrescript() {
  int structure = randInt(5) + 1;
  String finalPrescript = "";
  
  switch(structure) {
    case 1:
      finalPrescript = getSingle();
      break;
    case 2:
      finalPrescript = getTimeFirstSlot() + getActivitySecondSlot();
      break;
    case 3:
      finalPrescript = getActivityFirstSlot() + getMarker() + getActivitySecondSlot();
      break;
    case 4:
      finalPrescript = getTimeFirstSlot() + getActivitySecondSlot() + getPostscript();
      break;
    case 5:
      finalPrescript = getActivityFirstSlot() + getMarker() + getActivitySecondSlot() + getPostscript();
      break;
  }
  return capFirst(finalPrescript);
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
  delay(100);
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(50);
  digitalWrite(TFT_RST, LOW);
  delay(200);
  digitalWrite(TFT_RST, HIGH);
  delay(200);
  tft.init();
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
  drawBmpCenter("/index.bmp");

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

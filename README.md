# Prescript Device

The prescript device/pager from the hit game Limbus Company. 

## 🛠 Components Used

- **Microcontroller:** ESP32-S3 DevKitC-1 N16R8 (I recommend this specific model since it has better cores and has larger flash and RAM)
- **Display:** 1.77" TFT LCD (ST7735 Driver, 128x160 resolution)
- **Audio:** I2S DAC/Amplifier (e.g., MAX98357A) + 8Ω Speaker
- **Buttons:** 3x Momentary Push Buttons
- **Storage:** Internal SPIFFS for MP3 assets (`beep.mp3`, `scramble.mp3`)

## 🔌 Wiring Diagram

### TFT Display (ST7735 1.77" 160x128) (Recommended as the index logo is only 108x128, although you could upload an image yourself) 
| TFT Pin | ESP32-S3 Pin | Function |
| :--- | :--- | :--- |
| VCC | 3.3V / 5V | Power |
| GND | GND | Ground |
| CS | GPIO 10 | Chip Select |
| RESET | GPIO 9 | Reset |
| DC/A0 | GPIO 8 | Data/Command |
| SDA/MOSI| GPIO 11 | SPI Data |
| SCK | GPIO 12 | SPI Clock |
| LED/BL | GPIO 13 | Backlight Control |

### Audio (I2S DAC)
| DAC Pin | ESP32-S3 Pin | Function |
| :--- | :--- | :--- |
| VIN | 5V | Power |
| GND | GND | Ground |
| DIN | GPIO 5 | I2S Data Out |
| BCLK | GPIO 4 | Bit Clock |
| LRC | GPIO 7 | Word Select (Left/Right Clock) |

### Buttons
| Button | ESP32-S3 Pin | Mode |
| :--- | :--- | :--- |
| **Prescript** | GPIO 21 | Input PULLUP (GND) |
| **Random** | GPIO 20 | Input PULLUP (GND) |
| **Clear** | GPIO 2 | Input PULLUP (GND) |

---

## 🚀 How to Flash & Install (Beginner Guide)

If you are new to ESP32 development, follow these steps to get your Prescript Device running:

### 1. Prerequisites
- **Visual Studio Code (VS Code):** [Download and install here](https://code.visualstudio.com/).
- **PlatformIO Extension:** 
  1. Open VS Code.
  2. Click the **Extensions** icon on the left sidebar (or press `Ctrl+Shift+X`).
  3. Search for "PlatformIO IDE" and click **Install**.
  4. Wait for the installation to finish (an alien head icon will appear on the left).

### 2. Setup the Project
1. Download or clone this repository to your computer.
2. In VS Code, click the **PlatformIO (Alien Head)** icon.
3. Select **Pick a folder** or **Open Project** and navigate to this project's folder.

### 3. Flash the Device
Connect your ESP32-S3 to your computer via USB and follow these two critical steps:

#### **Step A: Upload Audio Files (SPIFFS)**
*The sounds won't work without this!*
1. Click the **PlatformIO icon** on the left.
2. Under **Project Tasks**, look for `esp32-s3-devkitc1-n16r8`.
3. Expand **Platform** and click **Upload Filesystem Image**.
4. This flashes the `data/` folder (about 35KB) to your device.

#### **Step B: Upload the Code**
1. Look at the bottom status bar of VS Code.
2. Click the **→ (Right Arrow)** icon to "Upload".
3. This compiles the code (~1.6MB) and sends it to the ESP32.

### 4. Verify
- Click the **Plug (Serial Monitor)** icon at the bottom.
- Set the speed to **115200** if prompted.
- You should see "Prescript Generator Ready" in the console!

## 🎮 Usage

- **Prescript Button:** Generates a randomized "Instruction" with a scramble animation and sound.
- **Random Button:** Generates an 8-character randomized string.
- **Clear Button:** Triggers '_CLEAR._'

## 📁 Project Structure
- `src/main.cpp`: Main application logic, RNG, and display/audio drivers.
- `data/`: Contains `scramble.mp3` and `beep.mp3` for the SPIFFS image.
- `platformio.ini`: Hardware configuration and library dependencies.
- `index.h`: Contains the bitmap data for the initial splash screen.




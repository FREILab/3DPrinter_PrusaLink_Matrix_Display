/**********************************************************************
 * A sketch to display to read out the state of a Prusa Link         *
 * instance and display it onto a matrix LED display.                *
 * *
 * Necessary acces data has to be provided to the secret.h           *
 * 08/2025 (Adapted for Prusa Link)                                  *
 * By Marius Tetard for FREILab Freiburg e.V. https://freilab.de     *
 * *
 * This software is open source and licensed under the MIT License.  *
 * See the LICENSE file or visit https://opensource.org/licenses/MIT *
 * for more details.                                                 *
**********************************************************************/


#include <WiFi.h>
#include <WiFiClient.h>
#include "secret.h"
#include "PrusaLinkAPI.h"          // For connection to Prusa Link
#include <Adafruit_Protomatter.h>  // For RGB matrix
#include <esp_task_wdt.h>          // For watchdog timer


#define HEIGHT 32   // Matrix height (pixels) - SET TO 64 FOR 64x64 MATRIX!
#define WIDTH 64    // Matrix width (pixels)
#define MAX_FPS 45  // Maximum redraw rate, frames/second

// PIN Configuration for the ESP32-S3 and the RGB Matrix
uint8_t rgbPins[] = { 42, 41, 40, 38, 39, 37 };
uint8_t addrPins[] = { 45, 36, 48, 35, 21 };
uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin = 14;

#if HEIGHT == 16
#define NUM_ADDR_PINS 3
#elif HEIGHT == 32
#define NUM_ADDR_PINS 4
#elif HEIGHT == 64
#define NUM_ADDR_PINS 5
#endif

Adafruit_Protomatter matrix(
  WIDTH, 4, 1, rgbPins, NUM_ADDR_PINS, addrPins,
  clockPin, latchPin, oePin, true);

int16_t textX;  // Current text position (X)
int16_t textY;  // Current text position (Y)
char str[64];   // Buffer to text

// Constants for Wi-Fi reconnection
const unsigned long CHECK_INTERVAL = 1000;  // Check Wi-Fi every 1 second
unsigned long previousMillis = 0;

// Setup Prusa Link
WiFiClient client;
// Initialize Prusa Link API. Port 80 is the default for Prusa Link HTTP.
PrusaLinkApi prusaLink(client, CONFIG_IP, CONFIG_PORT, SECRET_PRUSA_API_KEY);



unsigned long wifiLostSince = 0;
bool wifiWasOffline = false;

unsigned long prusaLinkLostSince = 0;
bool prusaLinkWasOffline = false;


const int tempGood_T0 = 50; // below this temperature Nozzle is considered cool
const int tempGood_Bed = 50; // below this temperature Bed is considered cool

// Watchdog timeout (3 seconds)
/* info on core mask:
  .idle_core_mask = (1 << portNUM_PROCESSORS) - 1
  ESP32 has two cores -> 0001
  shift two times to the left -> 0100
  subtract one -> 0011
*/
esp_task_wdt_config_t twdt_config
{
  timeout_ms:     3000U, // Increased to 3 seconds
  idle_core_mask: 0b011,
  trigger_panic:  true
};

void setup() {
  // Start Serial Interface
  Serial.begin(115200);
  delay(500);
  Serial.println("[System] Starting up...");

  // enable debug
  // prusaLink._debug = true;

  // Start LED Matrix
  ProtomatterStatus status = matrix.begin();
  Serial.print("[Matrix] Protomatter begin() status: ");
  Serial.println((int)status);

  // Show initial WiFi offline screen
  displayWiFiOffline();

  // connect to WiFi
  connectToWiFi();

  // Initialize the watchdog timer
  esp_task_wdt_init(&twdt_config); // Enable panic reset
  esp_task_wdt_add(NULL); // Add current task to watchdog
}

void loop() {
  unsigned long currentMillis = millis();
  // Check the Wi-Fi connection and API status every second
  if (currentMillis - previousMillis >= CHECK_INTERVAL) {
    previousMillis = currentMillis;

    // Feed the watchdog BEFORE potential long-running operations to prevent a reset
    esp_task_wdt_reset();

    // === Check Wi-Fi connection ===
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] Wi-Fi lost. Reconnecting...");
      if (!wifiWasOffline) {
        wifiLostSince = currentMillis;
        wifiWasOffline = true;
      } else if (currentMillis - wifiLostSince >= 3000) {
        displayWiFiOffline();
        reconnectWiFi();
      }
      return; // No API calls without WiFi
    } else {
      wifiWasOffline = false;
    }

    Serial.println("[API] Checking Prusa Link...");
    
    // === Check Prusa Link connection and status ===
    if (prusaLink.getPrinterStatus()) {
      prusaLinkWasOffline = false;

      // Feed the watchdog again after the first successful API call
      esp_task_wdt_reset();

      // State: Printer is printing
      if (prusaLink.printerStats.printerStatePrinting) {
        if (prusaLink.getJobInfo()) {
          float progress = prusaLink.jobInfo.progressCompletion / 100.0f;

          displayPrinterPrinting(
            prusaLink.jobInfo.progressPrintTimeLeft,
            progress,
            prusaLink.printerStats.printerTool0TempActual,
            prusaLink.printerStats.printerBedTempActual);
        } else {
          Serial.println("[PrusaLink] JobInfo-API offline (but Status is available)");
        }
      }
      // State: Printer is ready/idle
      else if (prusaLink.printerStats.printerStateReady) {
        displayPrinterReady(prusaLink.printerStats.printerTool0TempActual, prusaLink.printerStats.printerBedTempActual);
      }
      // Other states (paused, finished, busy, etc.) can be handled here if needed
      else {
        // For now, treat other states like "Ready"
        displayPrinterReady(prusaLink.printerStats.printerTool0TempActual, prusaLink.printerStats.printerBedTempActual);
        Serial.print("[PrusaLink] Printer in unhandled state: ");
        Serial.println(prusaLink.printerStats.printerState);
      }

    } else {
      Serial.println("[PrusaLink] API offline");
      // Optional Debugging lines
      Serial.print("[PrusaLink] HTTP Status Code: ");
      Serial.println(prusaLink.httpStatusCode);
      Serial.print("[PrusaLink] HTTP Error Body: ");
      Serial.println(prusaLink.httpErrorBody);

      if (!prusaLinkWasOffline) {
        prusaLinkLostSince = currentMillis;
        prusaLinkWasOffline = true;
      } else if (currentMillis - prusaLinkLostSince >= 3000) {
        displayPrusaLinkOffline();
      }
    }
  }

  // Feed the watchdog one last time to be safe
  esp_task_wdt_reset();
}

void connectToWiFi() {
  Serial.println("[WiFi] Connecting to Wi-Fi...");
  WiFi.begin(SECRET_SSID, SECRET_PASS);

  int attempts = 0;
  Serial.print("[WiFi] ");
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { // Increased attempts
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected.");
    Serial.println("[WiFi] IP Address: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WiFi] Failed to connect.");
  }
}

void reconnectWiFi() {
  WiFi.disconnect();
  delay(100);
  WiFi.reconnect();

  int attempts = 0;
  Serial.print("[WiFi] Reconnecting...");
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Reconnected.");
  } else {
    Serial.println("\n[WiFi] Reconnection attempt failed.");
  }
}


void displayPrinterPrinting(int seconds, float progress, int temp_T0, int temp_Bed) {
  int h = seconds / 3600;
  int min = (seconds % 3600) / 60;

  // necessary variables
  int h_ones, h_tens, m_tens, m_ones;

  // Extract digits
  h_tens = h / 10;
  h_ones = h % 10;
  m_tens = min / 10;
  m_ones = min % 10;

  // Fill background black
  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  // draw border
  matrix.drawRect(0, 0, 64, 32, matrix.color565(0, 255, 0));  // green

  // Text "Time"
  matrix.setTextColor(0xFFFF);  // white
  matrix.setCursor(2, 3);
  matrix.print("Time");

  // Minutes Text
  matrix.setCursor(57, 3);
  matrix.print("m");

  // Minutes ones
  matrix.setTextColor(matrix.color565(0, 255, 255));  // bright blue
  matrix.setCursor(51, 3);
  matrix.print(m_ones);

  // Minutes tens (if h>0 or m_tens>0)
  if ((h > 0) || (m_tens > 0)) {
    matrix.setCursor(45, 3);
    matrix.print(m_tens);
  }

  // Hours Text (if h>0)
  if (h > 0) {
    matrix.setTextColor(0xFFFF);  // white
    matrix.setCursor(38, 3);
    matrix.print("h");
  }

  // Hours ones (if h>0)
  if (h > 0) {
    matrix.setTextColor(matrix.color565(0, 255, 255));  // bright blue
    matrix.setCursor(32, 3);
    matrix.print(h_ones);
  }

  // Hours tens (if h>9)
  if (h > 9) {
    matrix.setCursor(26, 3);
    matrix.print(h_tens);
  }

  // Draw Progressbar outline
  matrix.drawRect(2, 12, 60, 6, matrix.color565(128, 128, 128));  // gray

  // draw the progress bar length depending on the progress
  int bar_max_progress = scaleFloatToInteger(progress);
  matrix.fillRect(3, 13, bar_max_progress - 3, 4, matrix.color565(0, 255, 0)); // green filled rect

  // Line separating Progress and Temperatures
  matrix.drawRect(1, 20, 62, 1, matrix.color565(255, 255, 255));  // white

  // Display T0 (Nozzle)
  if(temp_T0 < 10) textX = 16;
  else if (temp_T0 < 100) textX = 10;
  else textX = 4;
  textY = 23;
  if (temp_T0 >= tempGood_T0) matrix.setTextColor(matrix.color565(255, 0, 0));  // red
  else matrix.setTextColor(matrix.color565(0, 255, 0));  // green
  matrix.setCursor(textX, textY);
  matrix.print(temp_T0);

  // display "°C" for T0
  matrix.setTextColor(matrix.color565(255, 255, 255));  // white
  matrix.setCursor(26, 23);
  matrix.print("C");
  matrix.drawCircle(24, 23, 1, matrix.color565(255, 255, 255)); // white

  // Display Slash
  matrix.setCursor(33, 23);
  matrix.print("|");

  // Display T_Bed
  if(temp_Bed < 10) textX = 46;
  else if (temp_Bed < 100) textX = 40;
  else textX = 34; // For 3-digit bed temps
  textY = 23;
  if (temp_Bed >= tempGood_Bed) matrix.setTextColor(matrix.color565(255, 0, 0));  // red
  else matrix.setTextColor(matrix.color565(0, 255, 0));  // green
  matrix.setCursor(textX, textY);
  matrix.print(temp_Bed);

  // display "°C" for Bed
  matrix.setTextColor(matrix.color565(255, 255, 255));  // white
  matrix.setCursor(56, 23);
  matrix.print("C");
  matrix.drawCircle(54, 23, 1, matrix.color565(255, 255, 255)); // white

  // Update Display
  matrix.show();
}

void displayPrinterReady(int temp_T0, int temp_Bed) {

  // Fill background black
  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);

  // draw border
  matrix.drawRect(0, 0, 64, 32, matrix.color565(255, 255, 0));  // yellow

  // Text "Ready"
  matrix.setTextColor(0xFFFF);  // white
  matrix.setCursor(2, 3);
  matrix.print("Ready");

  // Draw Progressbar outline (empty)
  matrix.drawRect(2, 12, 60, 6, matrix.color565(128, 128, 128));  // gray

  // Line separating Progress and Temperatures
  matrix.drawRect(1, 20, 62, 1, matrix.color565(255, 255, 255));  // white

  // Display T0 (Nozzle)
  if(temp_T0 < 10) textX = 16;
  else if (temp_T0 < 100) textX = 10;
  else textX = 4;
  textY = 23;
  if (temp_T0 >= tempGood_T0) matrix.setTextColor(matrix.color565(255, 0, 0));  // red
  else matrix.setTextColor(matrix.color565(0, 255, 0));  // green
  matrix.setCursor(textX, textY);
  matrix.print(temp_T0);

  // display "°C" for T0
  matrix.setTextColor(matrix.color565(255, 255, 255));  // white
  matrix.setCursor(26, 23);
  matrix.print("C");
  matrix.drawCircle(24, 23, 1, matrix.color565(255, 255, 255)); // white

  // Display Slash
  matrix.setCursor(33, 23);
  matrix.print("|");

  // Display T_Bed
  if(temp_Bed < 10) textX = 46;
  else if (temp_Bed < 100) textX = 40;
  else textX = 34;
  textY = 23;
  if (temp_Bed >= tempGood_Bed) matrix.setTextColor(matrix.color565(255, 0, 0));  // red
  else matrix.setTextColor(matrix.color565(0, 255, 0));  // green
  matrix.setCursor(textX, textY);
  matrix.print(temp_Bed);

  // display "°C" for Bed
  matrix.setTextColor(matrix.color565(255, 255, 255));  // white
  matrix.setCursor(56, 23);
  matrix.print("C");
  matrix.drawCircle(54, 23, 1, matrix.color565(255, 255, 255)); // white

  // Update Display
  matrix.show();
}

void displayPrusaLinkOffline() {
  // Fill background black
  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  // draw border
  matrix.drawRect(0, 0, 64, 32, matrix.color565(255, 0, 0));  // red

  // Text
  matrix.setTextColor(0xFFFF);  // white
  matrix.setCursor(2, 3);
  matrix.print("Prusa Link");

  matrix.setCursor(2, 14);
  matrix.print("offline");

  // Update Display
  matrix.show();
}

void displayWiFiOffline() {
  // Fill background black
  matrix.fillScreen(0);
  matrix.setTextWrap(false);
  matrix.setTextSize(1);
  // draw border
  matrix.drawRect(0, 0, 64, 32, matrix.color565(255, 0, 0));  // red

  // Text
  matrix.setTextColor(0xFFFF);  // white
  matrix.setCursor(2, 3);
  matrix.print("WiFi");

  matrix.setCursor(2, 14);
  matrix.print("offline");

  // Update Display
  matrix.show();
}

// Scale a float between 0 to 1 to a int between 3 and 61
int scaleFloatToInteger(float value) {
  // Ensure the input value stays within the expected range
  value = constrain(value, 0.0, 1.0);

  // Map the float to the integer range [3, 61]
  int scaledValue = round(value * (61 - 3) + 3);

  return scaledValue;
}

// debug: print prusa link API data
void printPrusaLinkDebug() {
  if (prusaLink.getPrinterStatus()) {
    Serial.println();
    Serial.println("---------States---------");
    Serial.print("Printer Current State: ");
    Serial.println(prusaLink.printerStats.printerState);
    Serial.print("Printer State - Printing:  ");
    Serial.println(prusaLink.printerStats.printerStatePrinting);
    Serial.print("Printer State - Paused:  ");
    Serial.println(prusaLink.printerStats.printerStatePaused);
    Serial.print("Printer State - Ready:  ");
    Serial.println(prusaLink.printerStats.printerStateReady);
    Serial.println("------------------------");
    Serial.println();
    Serial.println("------Termperatures-----");
    Serial.print("Printer Temp - Nozzle (°C):  ");
    Serial.println(prusaLink.printerStats.printerTool0TempActual);
    Serial.print("Printer Temp - Bed (°C):  ");
    Serial.println(prusaLink.printerStats.printerBedTempActual);
    Serial.println("------------------------");
  }
   if (prusaLink.getJobInfo()) {
    Serial.println();
    Serial.println("----------Job-----------");
    Serial.print("File Name: ");
    Serial.println(prusaLink.jobInfo.jobFileName);
    Serial.print("Progress: ");
    Serial.println(prusaLink.jobInfo.progressCompletion);
    Serial.print("Time Left (s): ");
    Serial.println(prusaLink.jobInfo.progressPrintTimeLeft);
    Serial.println("------------------------");
  }
}
#define S2 2        // GPIO2
#define S3 15       // GPIO15
#define sensorOut 4 // GPIO4

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

// ========== GANTI DENGAN WIFI ANDA ==========
const char* ssid = "DUCE";
const char* password = "belongsajah";
// ============================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define EEPROM_SIZE 512
#define RESET_BUTTON 5  // GPIO5

// Telegram BOT Token
#define BOT_TOKEN "8583951904:AAHf0DUVwaVjjCJtQ_tADF7iJIJ5MeJv7rc"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

/* Variables */
int Red = 0;
int Green = 0;
int Blue = 0;
int Frequency = 0;
bool statusUang = 0;
bool msg = 0;
int Uang = 0;
int lastUang = -1;
String chatID = "";

// Debouncing variables
unsigned long lastDetectionTime = 0;
const unsigned long detectionCooldown = 3000; // 3 detik cooldown antar deteksi

unsigned long lastBotCheck = 0;
const unsigned long botCheckDelay = 1000;

// Helper functions for EEPROM
void writeIntToEEPROM(int address, int value) {
  EEPROM.write(address, (value >> 24) & 0xFF);
  EEPROM.write(address + 1, (value >> 16) & 0xFF);
  EEPROM.write(address + 2, (value >> 8) & 0xFF);
  EEPROM.write(address + 3, value & 0xFF);
  EEPROM.commit();
}

int readIntFromEEPROM(int address) {
  return ((long)EEPROM.read(address) << 24) |
         ((long)EEPROM.read(address + 1) << 16) |
         ((long)EEPROM.read(address + 2) << 8) |
         (long)EEPROM.read(address + 3);
}

String readStringFromEEPROM(int address, int maxLength) {
  String data = "";
  char c;
  for (int i = 0; i < maxLength; i++) {
    c = EEPROM.read(address + i);
    if (c == '\0' || c == 255) break;
    data += c;
  }
  return data;
}

void writeStringToEEPROM(int address, String data) {
  for (unsigned int i = 0; i < data.length(); i++) {
    EEPROM.write(address + i, data[i]);
  }
  EEPROM.write(address + data.length(), '\0');
  EEPROM.commit();
}

void connectWiFi() {
  Serial.println();
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);
  
  display.clearDisplay();
  displayCenteredText("Connecting", 20, 1);
  displayCenteredText("WiFi...", 35, 1);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    display.clearDisplay();
    displayCenteredText("WiFi OK!", 20, 1);
    displayCenteredText(WiFi.localIP().toString(), 35, 1);
    display.display();
    delay(2000);
  } else {
    Serial.println("");
    Serial.println("Gagal terhubung ke WiFi!");
    Serial.println("Periksa SSID dan Password!");
    
    display.clearDisplay();
    displayCenteredText("WiFi", 10, 1);
    displayCenteredText("GAGAL!", 25, 2);
    displayCenteredText("Cek setting", 50, 1);
    display.display();
    
    while(1) {
      delay(1000);
    }
  }
}

void setup() {
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(sensorOut, INPUT);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  Serial.begin(115200);
  delay(10);

  // Initialize OLED display first
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 gagal diinisialisasi"));
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  Uang = readIntFromEEPROM(0);
  chatID = readStringFromEEPROM(100, 50);

  // Validate Uang value
  if (Uang < 0 || Uang > 100000000) {
    Uang = 0;
    writeIntToEEPROM(0, Uang);
  }

  if (chatID == "" || chatID.length() < 5) {
    Serial.println("Chat ID belum diset");
    chatID = "";
  } else {
    Serial.print("Chat ID tersimpan: ");
    Serial.println(chatID);
  }

  // Connect to WiFi
  connectWiFi();

  // Configure secure client for Telegram
  secured_client.setCACert(TELEGRAM_CERTIFICATE_ROOT);

  // Request Chat ID if not set
  if (chatID == "") {
    requestChatID();
  } else {
    // Test connection
    bot.sendMessage(chatID, "🔄 Celengan Pintar aktif!\n💰 Saldo: Rp " + String(Uang));
  }

  displaySaldo();
  Serial.println("Setup selesai!");
  Serial.println("\n=== MODE KALIBRASI ===");
  Serial.println("Masukkan uang satu per satu");
  Serial.println("Catat nilai R, G, B untuk setiap nominal");
  Serial.println("Format: R:XX G:XX B:XX");
  Serial.println("======================\n");
}

void loop() {
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi terputus! Reconnecting...");
    connectWiFi();
  }

  // Check reset button (hold 3 seconds)
  if (digitalRead(RESET_BUTTON) == LOW) {
    delay(50);
    if (digitalRead(RESET_BUTTON) == LOW) {
      unsigned long pressStart = millis();
      bool resetDisplayed = false;
      
      while (digitalRead(RESET_BUTTON) == LOW) {
        if (millis() - pressStart > 3000 && !resetDisplayed) {
          display.clearDisplay();
          displayCenteredText("Mereset", 20, 2);
          displayCenteredText("Saldo...", 45, 1);
          display.display();
          resetDisplayed = true;
        }
        delay(10);
      }
      
      if (resetDisplayed) {
        resetSaldoEEPROM();
        displaySaldo();
      }
    }
  }

  // Update display if saldo changed
  if (Uang != lastUang) {
    displaySaldo();
    lastUang = Uang;
  }

  // Read color sensor multiple times for stability
  int r1 = getRed();
  delay(10);
  int r2 = getRed();
  delay(10);
  int r3 = getRed();
  Red = (r1 + r2 + r3) / 3;

  int g1 = getGreen();
  delay(10);
  int g2 = getGreen();
  delay(10);
  int g3 = getGreen();
  Green = (g1 + g2 + g3) / 3;

  int b1 = getBlue();
  delay(10);
  int b2 = getBlue();
  delay(10);
  int b3 = getBlue();
  Blue = (b1 + b2 + b3) / 3;

  // Debug sensor untuk kalibrasi
  Serial.printf("R:%d G:%d B:%d ", Red, Green, Blue);
  
  // Deteksi jika ada objek (RGB rendah = ada uang)
  bool objectDetected = (Red < 150 && Green < 150 && Blue < 150);
  
  if (objectDetected) {
    Serial.print(" >>> OBJEK TERDETEKSI!");
  }
  Serial.println();

  // Cek cooldown untuk menghindari deteksi ganda
  bool canDetect = (millis() - lastDetectionTime > detectionCooldown);

  // ========== DETEKSI NOMINAL ==========
  // PENTING: Ganti nilai min/max sesuai hasil kalibrasi Anda!
  // Format: detectNominal(nominal, RedMin, RedMax, GreenMin, GreenMax, BlueMin, BlueMax)
  
  if (objectDetected && canDetect) {
    int detectedNominal = 0;
    
    // Rp 100.000 - Cek nominal terbesar dulu
    if (detectNominal(100000, 19, 23, 31, 35, 24, 28)) {
      detectedNominal = 100000;
    }
    // Rp 50.000
    else if (detectNominal(50000, 30, 55, 35, 70, 30, 60)) {
      detectedNominal = 50000;
    }
    // Rp 20.000
    else if (detectNominal(20000, 60, 90, 50, 100, 101, 127)) {
      detectedNominal = 20000;
    }
    // Rp 10.000 - PENTING: GANTI NILAI INI!
    else if (detectNominal(10000, 55, 85, 65, 100, 50, 90)) {
      detectedNominal = 10000;
    }
    // Rp 5.000 - PENTING: GANTI NILAI INI!
    else if (detectNominal(5000, 60, 90, 70, 105, 55, 90)) {
      detectedNominal = 5000;
    }
    // Rp 2.000 - PENTING: GANTI NILAI INI!
    else if (detectNominal(2000, 70, 100, 80, 120, 65, 100)) {
      detectedNominal = 2000;
    }
    
    // Jika nominal terdeteksi, proses
    if (detectedNominal > 0) {
      processNominal(detectedNominal);
      lastDetectionTime = millis();
    } else {
      Serial.println("⚠️ Objek terdeteksi tapi nominal tidak cocok!");
      Serial.println("   Cek nilai kalibrasi Anda di code!");
    }
  }
  
  // Reset status jika tidak ada objek
  if (!objectDetected) {
    statusUang = 0;
    msg = 0;
  }

  // Check Telegram messages
  if (millis() - lastBotCheck > botCheckDelay) {
    lastBotCheck = millis();
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    if (numNewMessages > 0) {
      for (int i = 0; i < numNewMessages; i++) {
        handleTelegramMessage(i);
      }
    }
  }

  // Send notification
  if (statusUang == 1 && msg == 0) {
    if (chatID != "") {
      String message = "💰 Uang masuk!\nSaldo: Rp " + String(Uang);
      bot.sendMessage(chatID, message);
    }
    msg = 1;
  }

  delay(100);
}

void processNominal(int nominal) {
  Serial.println("\n=============================");
  Serial.printf("✅ NOMINAL TERDETEKSI: Rp %d\n", nominal);
  Serial.println("=============================\n");
  
  Uang += nominal;
  writeIntToEEPROM(0, Uang);
  showReceivedAmount(nominal);
  statusUang = 1;
  
  Serial.printf("Total Saldo: Rp %d\n\n", Uang);
}

bool detectNominal(int nominal, int rMin, int rMax, int gMin, int gMax, int bMin, int bMax) {
  return (Red >= rMin && Red <= rMax && 
          Green >= gMin && Green <= gMax && 
          Blue >= bMin && Blue <= bMax);
}

void resetSaldoEEPROM() {
  Uang = 0;
  writeIntToEEPROM(0, Uang);
  Serial.println("Saldo direset ke 0!");
  
  if (chatID != "") {
    bot.sendMessage(chatID, "🔄 Saldo berhasil direset ke Rp 0");
  }
}

void requestChatID() {
  Serial.println("Menunggu Chat ID dari Telegram...");
  
  display.clearDisplay();
  displayCenteredText("Kirim pesan", 10, 1);
  displayCenteredText("apapun ke", 25, 1);
  displayCenteredText("bot Telegram", 40, 1);
  display.display();

  unsigned long startTime = millis();
  while (chatID == "" && millis() - startTime < 300000) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    
    if (numNewMessages > 0) {
      for (int i = 0; i < numNewMessages; i++) {
        chatID = bot.messages[i].chat_id;
        writeStringToEEPROM(100, chatID);
        
        Serial.print("Chat ID tersimpan: ");
        Serial.println(chatID);
        
        String welcomeMsg = "✅ Chat ID berhasil tersimpan!\n\n";
        welcomeMsg += "🏦 Celengan Pintar siap digunakan\n\n";
        welcomeMsg += "Perintah:\n";
        welcomeMsg += "/start - Menu utama\n";
        welcomeMsg += "/saldo - Cek saldo\n";
        welcomeMsg += "/reset - Reset saldo";
        
        bot.sendMessage(chatID, welcomeMsg);
        
        display.clearDisplay();
        displayCenteredText("Chat ID", 20, 1);
        displayCenteredText("Tersimpan!", 35, 2);
        display.display();
        delay(2000);
        
        return;
      }
    }
    delay(1000);
  }
  
  if (chatID == "") {
    Serial.println("Timeout menunggu Chat ID");
    display.clearDisplay();
    displayCenteredText("Timeout!", 20, 2);
    displayCenteredText("Restart ESP", 45, 1);
    display.display();
  }
}

void handleTelegramMessage(int index) {
  String text = bot.messages[index].text;
  String senderChatID = bot.messages[index].chat_id;
  String senderName = bot.messages[index].from_name;

  if (senderChatID != chatID) {
    chatID = senderChatID;
    writeStringToEEPROM(100, chatID);
    Serial.print("Chat ID diperbarui: ");
    Serial.println(chatID);
  }

  Serial.printf("Pesan dari %s: %s\n", senderName.c_str(), text.c_str());

  if (text.equalsIgnoreCase("/start")) {
    String msg = "🏦 Celengan Pintar\n\n";
    msg += "Halo " + senderName + "!\n\n";
    msg += "💰 Saldo saat ini: Rp " + String(Uang) + "\n\n";
    msg += "📋 Perintah yang tersedia:\n";
    msg += "/saldo - Cek saldo terkini\n";
    msg += "/reset - Reset saldo ke 0\n";
    msg += "/help - Bantuan";
    bot.sendMessage(chatID, msg, "Markdown");
    
  } else if (text.equalsIgnoreCase("/reset")) {
    resetSaldoEEPROM();
    displaySaldo();
    bot.sendMessage(chatID, "🔄 Saldo berhasil direset ke Rp 0");
    
  } else if (text.equalsIgnoreCase("/saldo")) {
    String msg = "💰 Saldo Celengan\n\n";
    msg += "Rp " + String(Uang);
    bot.sendMessage(chatID, msg, "Markdown");
    
  } else if (text.equalsIgnoreCase("/help")) {
    String msg = "ℹ️ Bantuan Celengan Pintar\n\n";
    msg += "Masukkan uang ke celengan, sistem akan:\n";
    msg += "• Mendeteksi nominal otomatis\n";
    msg += "• Menampilkan di OLED\n";
    msg += "• Mengirim notifikasi ke Telegram\n\n";
    msg += "Reset saldo:\n";
    msg += "• Tekan tombol reset 3 detik, atau\n";
    msg += "• Kirim perintah /reset";
    bot.sendMessage(chatID, msg, "Markdown");
    
  } else {
    bot.sendMessage(chatID, "❓ Perintah tidak dikenali\n\nGunakan:\n/start - Menu\n/saldo - Cek saldo\n/reset - Reset\n/help - Bantuan");
  }
}

void showReceivedAmount(int nominal) {
  display.clearDisplay();
  displayCenteredText("Uang Masuk!", 5, 1);
  String nominalText = "Rp " + String(nominal);
  displayCenteredText(nominalText, 25, 2);
  displayCenteredText("Diterima", 50, 1);
  display.display();
  delay(3000);
}

void displaySaldo() {
  display.clearDisplay();
  displayCenteredText("Yuk Nabung!", 0, 1);
  displayCenteredText("Saldo:", 20, 1);
  String uangText = "Rp " + String(Uang);
  displayCenteredText(uangText, 40, 2);
  display.display();
}

void displayCenteredText(String text, int y, int textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);
  int textWidth = text.length() * 6 * textSize;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.println(text);
}

int getRed() {
  digitalWrite(S2, LOW);
  digitalWrite(S3, LOW);
  Frequency = pulseIn(sensorOut, LOW, 100000); // Timeout 100ms
  if (Frequency == 0) Frequency = 255; // Jika timeout, anggap nilai tinggi
  return Frequency;
}

int getGreen() {
  digitalWrite(S2, HIGH);
  digitalWrite(S3, HIGH);
  Frequency = pulseIn(sensorOut, LOW, 100000);
  if (Frequency == 0) Frequency = 255;
  return Frequency;
}

int getBlue() {
  digitalWrite(S2, LOW);
  digitalWrite(S3, HIGH);
  Frequency = pulseIn(sensorOut, LOW, 100000);
  if (Frequency == 0) Frequency = 255;
  return Frequency;
}
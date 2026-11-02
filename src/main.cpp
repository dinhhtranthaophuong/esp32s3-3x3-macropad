#include <Arduino.h>
#include <Preferences.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLESecurity.h>
#include <BLEHIDDevice.h>

// =====================================================================
//  Macropad 3x3: USB HID + BLE HID, 2 layer, chuyển chế độ bằng phím tổ hợp
// =====================================================================

// ---- Chân ma trận ----
const uint8_t NUM_ROWS = 3;
const uint8_t NUM_COLS = 3;
const uint8_t ROW_PINS[NUM_ROWS] = {4, 5, 6};       // output
const uint8_t COL_PINS[NUM_COLS] = {15, 16, 17};    // input pull-up, có interrupt

// ---- Mã phím HID (Keyboard/Keypad Usage Page 0x07) ----
const uint8_t K_NONE = 0x00;
const uint8_t K_1 = 0x1E, K_2 = 0x1F, K_3 = 0x20, K_4 = 0x21, K_5 = 0x22;
const uint8_t K_6 = 0x23, K_7 = 0x24, K_8 = 0x25, K_9 = 0x26, K_0 = 0x27;
const uint8_t K_ENTER = 0x28, K_ESC = 0x29, K_BACKSPACE = 0x2A, K_TAB = 0x2B, K_SPACE = 0x2C;
const uint8_t K_F1 = 0x3A, K_F2 = 0x3B, K_F3 = 0x3C, K_F4 = 0x3D, K_F5 = 0x3E, K_F6 = 0x3F;
const uint8_t K_RIGHT = 0x4F, K_LEFT = 0x50, K_DOWN = 0x51, K_UP = 0x52;
const uint8_t K_LCTRL = 0xE0, K_LSHIFT = 0xE1, K_LALT = 0xE2, K_LGUI = 0xE3;  // phím bổ trợ

// Mã đặc biệt (không gửi đi, chỉ dùng trong code)
const uint8_t KC_FN   = 0xFF;   // giữ để sang layer 1
const uint8_t KC_MODE = 0xFE;   // bấm để đổi qua lại USB <-> BLE

const uint8_t NUM_LAYERS = 2;
const uint8_t KEYMAP[NUM_LAYERS][NUM_ROWS][NUM_COLS] = {
  { // Layer 0
    {K_1, K_2, K_3},
    {K_4, K_5, K_6},
    {K_7, K_8, KC_FN}
  },
  { // Layer 1 (giữ KC_FN)
    {K_F1,    K_F2,   K_F3},
    {K_LEFT,  K_UP,   K_RIGHT},
    {KC_MODE, K_DOWN, KC_FN}      // KC_MODE: đổi chế độ USB / BLE
  }
};

const uint32_t DEBOUNCE_MS = 5;

// ---- Chế độ kết nối ----
enum : uint8_t { MODE_USB = 0, MODE_BLE = 1 };
uint8_t     mode = MODE_USB;
Preferences prefs;

// ---- Trạng thái phím ----
TaskHandle_t scanTask;
bool     keyState[NUM_ROWS][NUM_COLS];
uint32_t lastChange[NUM_ROWS][NUM_COLS];
uint8_t  pressedKey[NUM_ROWS][NUM_COLS];   // phím thực sự đã gửi khi bấm (để nhả đúng phím)
uint8_t  currentLayer = 0;

// ---- USB ----
USBHIDKeyboard Keyboard;

// ---- BLE ----
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,       // Keyboard, Application collection
  0x85, 0x01,                               // Report ID 1
  0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,       // phím bổ trợ 0xE0..0xE7
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
  0x95, 0x01, 0x75, 0x08, 0x81, 0x01,       // 1 byte dự trữ
  0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,   // 6 phím
  0xC0
};

BLEHIDDevice*      hid;
BLECharacteristic* inputKeyboard;
BLEAdvertising*    advertising;
volatile bool      bleConnected = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    bleConnected = true;
    Serial.print("BLE connected\r\n");
  }
  void onDisconnect(BLEServer*) override {
    bleConnected = false;
    Serial.print("BLE disconnected\r\n");
    if (mode == MODE_BLE) advertising->start();   // chỉ quảng bá lại khi đang ở chế độ BLE
  }
};

// ---- Báo cáo bàn phím dùng chung cho cả hai kênh ----
KeyReport report;   // {modifiers, reserved, keys[6]}

void sendReport() {
  if (mode == MODE_USB) {
    Keyboard.sendReport(&report);
  } else if (bleConnected) {
    inputKeyboard->setValue((uint8_t*)&report, 8);
    inputKeyboard->notify();
  }
}

void reportPress(uint8_t usage) {
  if (usage >= 0xE0 && usage <= 0xE7) {
    report.modifiers |= (1 << (usage - 0xE0));
  } else {
    for (uint8_t i = 0; i < 6; i++) if (report.keys[i] == usage) return;   // đã có
    for (uint8_t i = 0; i < 6; i++) {
      if (report.keys[i] == 0) { report.keys[i] = usage; break; }
    }
  }
  sendReport();
}

void reportRelease(uint8_t usage) {
  if (usage >= 0xE0 && usage <= 0xE7) {
    report.modifiers &= ~(1 << (usage - 0xE0));
  } else {
    for (uint8_t i = 0; i < 6; i++) if (report.keys[i] == usage) report.keys[i] = 0;
  }
  sendReport();
}

void releaseAllKeys() {
  memset(&report, 0, sizeof(report));
  sendReport();
}

// ---- Đổi chế độ ----
void setMode(uint8_t newMode) {
  releaseAllKeys();                 // gửi báo cáo rỗng qua kênh cũ để không kẹt phím
  mode = newMode;
  prefs.putUChar("mode", mode);     // nhớ chế độ sau khi khởi động lại
  if (mode == MODE_BLE) {
    if (!bleConnected) advertising->start();
  } else {
    advertising->stop();
  }
  Serial.printf("MODE -> %s\r\n", mode == MODE_BLE ? "BLE" : "USB");
}

// ---- Xử lý một sự kiện phím ----
void onKeyEvent(uint8_t r, uint8_t c, bool pressed) {
  uint8_t key;
  if (pressed) {
    key = KEYMAP[currentLayer][r][c];
    pressedKey[r][c] = key;
    if (key == KC_FN)        currentLayer = 1;
    else if (key == KC_MODE) setMode(mode == MODE_USB ? MODE_BLE : MODE_USB);
    else if (key != K_NONE)  reportPress(key);
  } else {
    key = pressedKey[r][c];         // nhả đúng phím đã bấm, kể cả khi layer đã đổi
    pressedKey[r][c] = K_NONE;
    if (key == KC_FN)        currentLayer = 0;
    else if (key != K_NONE && key != KC_MODE) reportRelease(key);
  }
  Serial.printf("%s  r%d c%d  layer %d  key 0x%02X  mode %s\r\n",
                pressed ? "DOWN" : "UP  ", r, c, currentLayer, key,
                mode == MODE_BLE ? "BLE" : "USB");
}

// ---- Ngắt + quét ma trận ----
void IRAM_ATTR onColumnChange() {
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(scanTask, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void armInterrupts() {
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
  }
  delayMicroseconds(10);
  for (uint8_t c = 0; c < NUM_COLS; c++) {
    attachInterrupt(digitalPinToInterrupt(COL_PINS[c]), onColumnChange, CHANGE);
  }
}

void disarmInterrupts() {
  for (uint8_t c = 0; c < NUM_COLS; c++) {
    detachInterrupt(digitalPinToInterrupt(COL_PINS[c]));
  }
  for (uint8_t r = 0; r < NUM_ROWS; r++) pinMode(ROW_PINS[r], INPUT);
}

bool scanMatrix() {
  bool busy = false;
  for (uint8_t r = 0; r < NUM_ROWS; r++) {
    pinMode(ROW_PINS[r], OUTPUT);
    digitalWrite(ROW_PINS[r], LOW);
    delayMicroseconds(10);

    for (uint8_t c = 0; c < NUM_COLS; c++) {
      bool pressed = (digitalRead(COL_PINS[c]) == LOW);
      uint32_t now = millis();
      if (pressed != keyState[r][c]) {
        if (now - lastChange[r][c] >= DEBOUNCE_MS) {
          keyState[r][c] = pressed;
          lastChange[r][c] = now;
          onKeyEvent(r, c, pressed);
        } else {
          busy = true;
        }
      }
      if (keyState[r][c]) busy = true;
    }
    pinMode(ROW_PINS[r], INPUT);
  }
  return busy;
}

void setup() {
  Serial.begin(115200);                    // log qua cổng UART
  scanTask = xTaskGetCurrentTaskHandle();
  for (uint8_t c = 0; c < NUM_COLS; c++) pinMode(COL_PINS[c], INPUT_PULLUP);

  prefs.begin("macropad", false);
  mode = prefs.getUChar("mode", MODE_USB);

  // USB: luôn bật, chỉ gửi phím khi mode == USB
  Keyboard.begin();
  USB.begin();

  // BLE: khởi tạo một lần, chỉ quảng bá khi mode == BLE
  BLEDevice::init("Macropad");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  hid = new BLEHIDDevice(server);
  inputKeyboard = hid->inputReport(1);     // Report ID 1
  hid->manufacturer()->setValue("DIY");
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);

  BLESecurity* security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_BOND);

  hid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  hid->startServices();
  hid->setBatteryLevel(100);

  advertising = server->getAdvertising();
  advertising->setAppearance(0x03C1);      // bàn phím
  advertising->addServiceUUID(hid->hidService()->getUUID());
  if (mode == MODE_BLE) advertising->start();

  armInterrupts();
  Serial.printf("Macropad 3x3 ready, mode %s\r\n", mode == MODE_BLE ? "BLE" : "USB");
}

void loop() {
  static bool busy = false;

  // Rảnh: ngủ đến khi có ngắt. Đang giữ phím hoặc chờ debounce: thức dậy mỗi 5 ms.
  ulTaskNotifyTake(pdTRUE, busy ? pdMS_TO_TICKS(5) : portMAX_DELAY);

  disarmInterrupts();
  busy = scanMatrix();
  armInterrupts();

  // Nếu có phím bị bấm đúng lúc đang quét (ngắt đang tắt), đánh thức lại ngay
  if (!busy) {
    for (uint8_t c = 0; c < NUM_COLS; c++) {
      if (digitalRead(COL_PINS[c]) == LOW) {
        xTaskNotifyGive(scanTask);
        break;
      }
    }
  }
}
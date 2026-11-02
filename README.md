# Macropad 3×3 ESP32-S3 (USB + Bluetooth)

Macropad tự làm dùng ESP32-S3, gửi phím về máy tính qua **USB HID** hoặc **Bluetooth (BLE HID)**, có 2 layer phím và chuyển chế độ bằng phím tổ hợp.

## Tính năng
- Ma trận phím 3×3, quét bằng ngắt (CPU ngủ khi không bấm phím).
- Chống dội phím (debounce 5 ms).
- Gửi phím qua USB HID hoặc BLE HID (ghép đôi được với laptop Windows).
- 2 layer: giữ phím Fn để sang layer 1.
- Phím tổ hợp đổi chế độ USB ↔ BLE, nhớ chế độ sau khi khởi động lại (lưu bằng `Preferences`).

## Phần cứng
| Linh kiện | Ghi chú |
|---|---|
| ESP32-S3-DevKitC-1 | 2 cổng USB-C: UART (nạp code, xem log) và USB native (làm bàn phím) |
| 9 nút nhấn | Ma trận 3×3 trên breadboard |

Sơ đồ chân:

| Chức năng | GPIO |
|---|---|
| Hàng 0, 1, 2 (output) | 4, 5, 6 |
| Cột 0, 1, 2 (input pull-up, có ngắt) | 15, 16, 17 |

Mỗi nút nối giữa một hàng và một cột. Hiện **chưa gắn diode**, nên giữ 3 phím trở lên cùng lúc có thể gây ghosting (phím ma). Nên thêm diode 1N4148 (anode về phía cột, cathode về phía hàng) khi làm bản chính thức.

## Cách build
1. Cài VSCode + extension PlatformIO.
2. Mở thư mục project, build bằng nút ✓ ở thanh dưới.
3. Cắm cổng **UART** để nạp code, cắm thêm cổng **USB** native để máy tính nhận bàn phím.

Lưu ý: `platformio.ini` có một đường dẫn tuyệt đối (`-IC:/Users/LENOVO/...`) để tránh lỗi thiếu `FS.h` khi build thư viện USB. Nếu clone về máy khác, bạn cần sửa lại đường dẫn này theo tên người dùng Windows của mình.

## Cách dùng
- **Layer 0:** các phím gõ `1` đến `8`.
- **Layer 1:** giữ phím Fn (góc dưới bên phải), các phím còn lại thành `F1`–`F3`, mũi tên, `Down`.
- **Đổi chế độ USB ↔ BLE:** giữ Fn và bấm phím góc dưới bên trái.
- **Ghép đôi Bluetooth:** chuyển sang chế độ BLE, trên Windows vào Settings → Bluetooth & devices → Add device, chọn "Macropad".
- Đổi chức năng phím bằng bảng `KEYMAP` ở đầu `src/main.cpp`.

## Cấu trúc thư mục
```
src/main.cpp                  # bản đầy đủ: USB + BLE, 2 layer, đổi chế độ
examples/main_test_hid.cpp    # thử USB HID tối thiểu bằng nút BOOT
examples/main_test_ble.cpp    # thử BLE HID tối thiểu bằng nút BOOT
platformio.ini
```
Hai file trong `examples/` nằm ngoài `src/` nên không bị biên dịch cùng project. Muốn thử, chép nội dung vào `src/main.cpp`.
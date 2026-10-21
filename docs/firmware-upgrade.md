# Nâng firmware Phicomm R1 lên 3448

Build 3448 là firmware cuối cùng Phicomm phát hành. DB-Robot chạy được trên các bản cũ hơn, nên chỉ
nâng khi cần. Đã làm trên loa thật ngày 21/10/2026: 3174 → 3318 → 3331 → 3448.

## Cần gì

- adb (`Android\Sdk\platform-tools`) và loa mở adb ở cổng 5555.
- Các gói `incremental-ota-<build>.zip`. Gói là bản vá chính thức của Phicomm, được recovery kiểm chữ
  ký trước khi cài, nên gói sai bản sẽ bị từ chối chứ không cài dở. Gói không nằm trong git
  (khoảng 66 MB); kích thước và md5 của từng gói ở [`tools/firmware/manifest.txt`](../tools/firmware/manifest.txt).
  Nguồn: bộ "Update-PhicommR1-v5.2.0" (AI-BOX-PLUS) hoặc "R1 One-click FW upgrade v2.3".

## Đường nâng cấp

Mỗi bản vá chỉ áp lên đúng một bản. File `tools/firmware/ota/ota-<build>.txt` cho biết bản kế tiếp.

| Đang ở | Lên | Gói |
|---|---|---|
| 3119 | 3166 | `incremental-ota-3119.zip` |
| 3166 | 3331 | `incremental-ota-3166.zip` (bản Update v5.2.0) |
| 3166 | 3415 | `incremental-ota-3166.zip` của One-click v2.3, cấu hình `ota-3166-to-3415.txt` (khác md5) |
| 3174 | 3318 | `incremental-ota-3174.zip` |
| 3318 | 3331 | `incremental-ota-3318.zip` |
| 3331 | 3448 | `incremental-ota-3331.zip` |
| 3415 | 3448 | `incremental-ota-3415.zip` |

## Cách chạy

```powershell
.\tools\firmware\upgrade-firmware.ps1 -Ip 192.168.1.224 -PackageDir D:\path\to\firmware
```

Mỗi lần chạy làm một bước: đọc bản hiện tại, kiểm md5 gói, đẩy gói và `otaprop.txt` vào `/sdcard`,
rồi khởi động lại loa. Chạy lại sau khi loa lên bản mới, cho tới khi script báo 3448.

## Điều đã gặp

- **Bản vá chỉ được cài khi loa có internet.** Dịch vụ `com.phicomm.speaker.otaservice` đọc
  `otaprop.txt` lúc khởi động nhưng chờ có mạng mới vào recovery. Ở chế độ hotspot thì nó đứng chờ mãi.
  Đưa loa vào Wi-Fi nhà (trang điều khiển DB-Robot, thẻ Wi-Fi) là bản vá tự cài, loa tự khởi động lại.
- **Sau một số bước loa quên Wi-Fi** và tự phát hotspot `Phicomm_R1_xxxx`, nói "ni hao". Giữ nút trên
  đỉnh 6 giây nếu không thấy hotspot, nối máy tính vào, rồi gửi Wi-Fi qua
  `POST http://192.168.43.1:8088/api/wifi/connect`. Đừng gọi `/api/wifi/scan` lúc máy tính đang nối
  qua hotspot: lệnh quét tắt hotspot và có lần nó không bật lại.
- **Firmware 3448 chặn lệnh adb bắt đầu bằng `pm`**: `adb shell pm ...` trả `error: closed`, kể cả
  `pm path`. Gọi thẳng như `/system/bin/pm` vẫn làm:
  `export CLASSPATH=/system/framework/pm.jar; app_process /system/bin com.android.commands.pm.Pm <lệnh>`.
  [`tools/install-apk.ps1`](../tools/install-apk.ps1) và script nâng firmware dùng cách này.
- Bộ AI-BOX-PLUS còn cài `new_EchoService.apk` (thay `com.phicomm.speaker.player`) trước bước đầu.
  Chưa rõ việc nâng firmware có cần nó không; lần chạy ngày 21/10 đã cài nó.

## Sau khi nâng

Việc nên làm cho một loa chạy DB-Robot (bước 3 của bộ AI-BOX-PLUS, bỏ phần cài app AI của họ):

- Ẩn các app Phicomm nói tiếng Trung và dịch vụ OTA: `airskill exceptionreporter systemtool device
  otaservice productiontest bugreport` (`Pm hide com.phicomm.speaker.<tên>`); giữ `ijetty`, `netctl`.
- Tuỳ chọn: `autodlna.apk` (DLNA + AirPlay, thay `com.phicomm.speaker.player`).
- Đổi ngôn ngữ hệ thống không cần root:
  `.\tools\locale\set-locale.ps1 -Ip <ip> -Language vi -Country VN`. Shell có quyền
  `CHANGE_CONFIGURATION`; `system_server` tự lưu `persist.sys.language` khi cấu hình báo người dùng
  đã chọn ngôn ngữ. Không đổi được câu "ni hao" vì đó là file ghi âm của app Phicomm.

# Hướng dẫn sử dụng loa DB-Robot (Phicomm R1)

Tài liệu hỏi đáp về cách dùng loa thông minh DB-Robot. Mỗi mục trả lời một câu hỏi thường gặp.

## Loa DB-Robot làm được những gì?
Loa là trợ lý giọng nói tiếng Việt. Gọi "OK Nabu" rồi nói. Loa trò chuyện, trả lời câu hỏi, mở nhạc và radio, phát podcast, đặt báo thức, cho biết giờ, thời tiết, giá vàng, giá xăng, ngày âm lịch, xem tử vi, và điều khiển thiết bị nhà thông minh qua Home Assistant. Việc nghe hiểu và trả lời do máy chủ làm; loa chỉ thu âm, nhận từ đánh thức và phát tiếng.

## Gọi loa bằng cách nào?
Nói "OK Nabu". Từ đánh thức có thể đổi sang "Alexa" hoặc "Nabi ơi" trong trang điều khiển, tab System, thẻ Wake word, sau đó khởi động lại ứng dụng. Bấm nút trên đỉnh loa cũng đánh thức được. Đang lúc loa trả lời, bấm nút một lần nữa để ngắt lời hoặc cho loa nghỉ.

## Trang điều khiển của loa ở đâu?
Mở trình duyệt trên điện thoại hoặc máy tính cùng mạng Wi-Fi và vào địa chỉ http://<IP-của-loa>:8088. Muốn biết địa chỉ IP thì hỏi loa: "địa chỉ IP của bạn là gì?". Trang có ba tab: Settings (âm lượng, equalizer, micro, đèn LED), System (máy chủ, máy chủ nhạc, vị trí và thời tiết, Wi-Fi, Bluetooth, model AI, Home Assistant, từ đánh thức, cập nhật, báo thức) và Media (nhạc, YouTube, radio, podcast).

## Làm sao mở nhạc hoặc radio?
Nói "mở bài Nơi này có anh", "mở VOV giao thông", "dừng nhạc", "tăng âm lượng lên 70". Radio có 8 kênh: VOV1, VOV2, VOV3, VOV4, VOV5, VOV Giao thông Hà Nội, VOV Giao thông TP.HCM, VOV Mekong FM. Trong tab Media còn tìm được bài trên YouTube và podcast.

## Loa nghe nhầm tiếng tivi hoặc tiếng nhạc thì làm sao?
Bấm nút "Tắt nghe" ở góc dưới bên trái trang điều khiển: loa thôi nghe từ đánh thức cho tới khi bấm lại. Nút "Gọi loa" bắt đầu nghe ngay cả khi đang tắt nghe. Bật "Tắt khi phát nhạc" trong thẻ Wake word để loa tự thôi nghe trong lúc phát nhạc hoặc radio. Nút trên đỉnh loa luôn đánh thức được.

## Đặt báo thức thế nào?
Vào tab System, thẻ Báo thức, bấm "+ Thêm", chọn giờ, ngày lặp lại, âm thanh và ghi chú rồi bấm Lưu. Tối đa 12 báo thức. Không chọn ngày nào thì báo một lần. Tắt báo thức bằng nút trên đỉnh loa hoặc thanh "Tắt báo thức" trên trang; không ai tắt thì loa tự tắt sau 10 phút. Báo thức chạy cả khi mất Internet.

## Đổi mạng Wi-Fi cho loa thế nào?
Vào tab System, thẻ Wi-Fi, bấm "Quét mạng", chọn mạng, nhập mật khẩu, bấm "Kết nối". Nếu sau khoảng 30 giây loa không vào được mạng mới, nó tự quay lại mạng cũ. Sang mạng mới thì loa có thể đổi địa chỉ IP.

## Loa mất Wi-Fi hoặc nói "ni hao" thì làm sao?
Loa không vào được mạng nào. Giữ nút trên đỉnh loa khoảng 5 đến 6 giây cho tới khi loa phát mạng Wi-Fi tên "Phicomm R1" hoặc "Phicomm_R1_...". Nối điện thoại vào mạng đó, mở http://192.168.43.1:8088, vào tab System, thẻ Wi-Fi, chọn mạng nhà và nhập mật khẩu. Câu "ni hao" là lời của phần mềm gốc Phicomm khi loa chưa có mạng.

## Loa không trả lời thì làm sao?
Kiểm tra loa còn Wi-Fi bằng cách mở trang điều khiển. Nếu trang không mở được hoặc loa vẫn im, rút điện loa 10 giây rồi cắm lại; ứng dụng DB-Robot tự chạy khi loa khởi động. Nếu vẫn không được, xem mục Log trên trang điều khiển.

## Cập nhật phần mềm cho loa thế nào?
Loa tự kiểm tra bản mới vài giờ một lần. Khi có bản mới, trang điều khiển báo ở đầu trang; vào tab System, thẻ Cập nhật phần mềm, bấm "Cập nhật ngay". Bật "Tự động cập nhật" để loa tự cài khi rảnh. Mọi cài đặt được giữ nguyên. Nếu trang báo loa đang bị một máy tính điều khiển qua adb, ngắt kết nối adb trên máy tính đó rồi thử lại.

## Phát nhạc từ điện thoại hoặc máy tính ra loa thế nào?
Loa có AirPlay và DLNA. Từ iPhone dùng AirPlay. Từ điện thoại Android dùng ứng dụng AirMusic. Từ máy tính Windows cài TuneBlade, hoặc bấm chuột phải vào tệp nhạc và chọn "Cast to Device". Thiết bị phải cùng mạng Wi-Fi với loa.

## Phát nhiều loa cùng lúc được không?
Được. Vào tab System, thẻ Nhóm loa, bấm "Tìm loa trong mạng", đánh dấu các loa muốn phát cùng rồi bật "Phát cùng các loa trong nhóm". Nhạc, radio và podcast phát ở loa chính sẽ phát đồng thời ở các loa đã chọn.

## Loa có kết nối loa Bluetooth được không?
Được. Vào tab System, thẻ Bluetooth, bấm quét, chọn loa hoặc tai nghe rồi ghép đôi. Mọi âm thanh của loa sẽ phát ra thiết bị đó.

## Giờ và thời tiết của loa theo nơi nào?
Theo thành phố chọn trong tab System, thẻ Vị trí và thời tiết. Hỏi "mấy giờ rồi" hay "thời tiết hôm nay" là loa trả lời theo nơi đó. Hỏi thời tiết của thành phố khác cũng được.

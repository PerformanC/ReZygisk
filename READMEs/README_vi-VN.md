# ReZygisk

[English](../README.md)

ReZygisk làm một nhánh phát triển lấy từ ZygiskNext, một triển khai độc lập của Zygisk, cung cấp và hỗ trợ Zygisk API cho KernelSU, APatch và Magisk (chính thức và Kitsune).

Mục tiêu của ReZygisk là mô-đun hoá và viết lại toàn bộ codebase sang C, cho phép triển khai API Zygisk hiệu quả hơn và nhanh hơn với giấy phép dễ dàng tái sử dụng hơn.

## Tại sao nhánh phát triển này lại xuất hiện?

Bản ra mắt mới nhất của Zygisk Next không còn là mã nguồn mở, dành toàn bộ quyền phát triển phần mềm cho những nhà phát triển gốc. Không chỉ giới hạn chúng ta đóng gốp cho dự án, mà còn bất khả thi trong việc kiểm trả độ đảm bảo của mã, điều mà có thể coi là mối quan tâm lớn về tính bảo mật và độ an toàn, bởi Zygisk Next là mô-đun chạy dưới quyền quản trị (root), có khả năng truy cập vào toàn bộ hệ thống trong thiết bị của bạn.

Các nhà phát triển Zygisk Next đều là những người nổi tiếng và được tin tưởng bởi cộng đồng sử dụng Android, tuy nhiên, điều đó không có nghĩa là mã nguồn của họ không có lỗ hổng hoặc có nguy cơ bị tấn công. Chúng tôi (PerformanC) thông cảm được lý do mà học đóng mã nguồn của họ vào, tuy nhiễn chúng tôi tin vào điều ngược lại.

## Ưu điểm

- Mã nguồn mở (Vĩnh Viễn)

## Các công cụ/thư viện được sử dụng

| Công cụ / Thư Viện        | Mô tả                                     |
|---------------------------|-------------------------------------------|
| `Android NDK`             | Bộ công cụ phát triển cốt lõi cho Android |

### Các công cụ/thư viện của C++ được sử dụng

| Thư Viện   | Mô Tả                                        |
|------------|----------------------------------------------|
| `lsplt`    | Công cụ **móc** vào PLT đơn giản cho Android |

## Cài Đặt

### 1. Sử dụng đúng tệp zip

Chọn đúng tệp bản dựng / zip là một điều tất yếu, bởi nó sẽ xác định khả năng ẩn của ReZygisk. Về cơ bản đây không phải là một việc khó:

- `release` bản này sẽ được chọn trong hầy hết các trường hợp sử dụng, bản này loại bỏ nhật ký phát triển cấp độ ứng dụng và cung cấp các tệp nhị phân được tối ưu hóa hơn.
- `debug`,  bản này tuy nhiên không được tối ưu và đi kèm với nó là ghi lại nhật ký phát triển khá nhiều. Vì lý do này, **chỉ nên sử dụng khi cần gỡ lỗi** và **khi cần ghi lại nhật lý để tạo báo về lỗi hoặc gì đó**.

As for branches, you should always use the `main` branch, unless told otherwise by the developers, or if you want to test upcoming features and are aware of the risks involved.

### 2. Flash the zip

Sau khi chọn một bản dựng phù hợp với bạn, điều cần làm là flash nó bằng trình quản lý root như Magisk hay KernelSU. Bạn có thể làm điều này bằng cách vào mục `Mô-đun / Module` của trình quản lý root của bạn và chọn tệp zip vừa tải xuống.

Sau khi flash, kiểm tra lại nhật ký lỗi để chắc chắn rằng không có lỗi nào xảy ra, nếu mọi thứ xuôn sẻ, khởi động lại thiết bị.

> [!WARNING]
> Người dùng Magisk cần phải tắt `built-in Zygisk`, bởi nó sẽ xung đột với ReZygisk. Điều này có thể thực hiện bằng cách vào `Cài Đặt` và tắt tùy chọn `ZygiskZygisk`

### 3. Verify the installation

Sau khi khởi động lại, bạn có thể xác minh ReZygisk có hoạt động bình thường không bằng cách kiểm tra mô tả module trong phần `Modules` của trình quản lý gốc. Mô tả sẽ chỉ ra rằng các daemon cần thiết đang chạy. Ví dụ, nếu môi trường của bạn hỗ trợ cả cấu trúctrúc 64-bit và 32-bit, nó sẽ trông giống như thế này: `[monitor: 😋 tracing, zygote64: 😋 inject, daemon64: 😋 running (...) zygote32: 😋 inject, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## Dịch WebUI cho mô-đun

Hiện tại có hai cách khác nhau để đóng góp bản dịch cho ReZygisk:

- Đối với bản dịch của README, bạn có thể tạo một tệp mới trong thư mục `READMEs`, theo quy ước đặt tên `README_<language>.md`, trong đó `<language>` là mã ngôn ngữ (ví dụ: `README_pt-BR.md` cho tiếng Bồ Đào Nha Brazil) và mở yêu cầu kéo đến nhánh `main` với các thay đổi của bạn.
- Đối với bản dịch của ReZygisk WebUI, trước tiên bạn phải đóng góp cho [Crowdin](https://crowdin.com/project/rezygisk) của chúng tôi. Sau khi được chấp thuận, hãy lấy tệp `.json` từ đó và mở yêu cầu kéo với các thay đổi của bạn -- thêm tệp `.json` vào thư mục `webroot/lang` và ghi công của bạn vào tệp `TRANSLATOR.md`, theo thứ tự bảng chữ cái.

## Hỗ trợ

Nếu bạn có những câu hỏi nào dành cho ReZygisk hoặc bất kì một dự án nào của PerformanC, hãy tự nhiên tham gia các kênh trò chuyện dưới đây:

- Kênh Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Kênh Telegram ReZygisk: [@rezygisk](https://t.me/rezygisk)
- Kênh Telegram PerformanC: [@performancorg](https://t.me/performancorg)
- Nhóm Signal PerformanC: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Đóng góp cho dự án này

Tuân theo [hướng dẫn đóng góp](https://github.com/PerformanC/contributing) của PerformanC là một điều tất yếu mà bạn bắt buộc phải làm. Hãy tuân theo chính sách bảo mật, quy tắc ứng xử/đóng góp mã nguồn và tiêu chuẩn cú pháp riêng.

## Bản quyền

Hầu hết các thành phần của ReZygisk để dưới bản quyền GPL (bởi Dr-TSNG) và AGPL 3.0 (bởiThe PerformanC Organization) cho những phần được viết lại. Bạn có thể xem thêm trong trang [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).

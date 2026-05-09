# FlashLogNUartOut

Raspberry Pi Pico / RP2040 上で動作する、FreeRTOS + UART DMA + SPI Flash ログ保存システムです。

ログフレームを外部 SPI Flash にリングバッファ形式で保存し、必要に応じて UART DMA 経由で PC 側へ送信します。  
疑似電源断テストにより、ログ書き込み途中で停止した場合でも、最後の正常フレームまで復旧できることを確認しています。

---

## 目的

このプロジェクトの目的は、組込み機器向けに以下を実現することです。

- 実行中のイベントログをバイナリフレームとして保存する
- UART DMA により低負荷でログを送信する
- SPI Flash にログを永続保存する
- 電源断後も Flash 内ログから復旧できる
- 書きかけログフレームを誤って有効ログとして扱わない
- 復旧後も安全な位置からログ追記を再開する

---

## 主な機能

- FreeRTOS タスクベースのログ処理
- UART DMA によるバイナリログ送信
- 外部 SPI Flash へのログ保存
- Flash 上のリングバッファ管理
- CRC32 によるログフレーム検証
- 古い順のログ読み出し
- 最新 N 件のログ送信
- 疑似電源断復旧テスト
- dirty tail 検出と次 sector への退避

---

## ハードウェア構成

### 使用ボード

- Raspberry Pi Pico
- RP2040

### 外部 SPI Flash

例：

- W25Q32 系 SPI Flash
- 確認済み JEDEC ID: `EF 40 16`

### SPI Flash 接続例

| 信号 | GPIO |
| ---- | --- :|
| MISO | GPIO16 |
| CS   | GPIO17 |
| SCK  | GPIO18 |
| MOSI | GPIO19 |

---

## ソフトウェア構成

主な構成は以下の通りです。

```text
src/
├── main.cpp
├── EventLogger.cpp
├── FlashLogStorage.cpp
├── FlashDriver.cpp
├── UartDma.cpp
├── Crc32.cpp
├── LogEvent.cpp
├── DebugUtils.cpp
├── task_command.cpp
├── led25.c
└── freertos_hooks.c

include/
├── EventLogger.h
├── FlashLogStorage.h
├── FlashDriver.h
├── UartDma.h
├── Crc32.h
├── LogTypes.h
├── LogProtocol.h
├── EventId.h
└── DebugUtils.h
```

---

## ログ保存の流れ

通常動作時の大まかな流れは以下です。

```text
Application
   ↓
EventLogger
   ↓
LogProtocol / buildFrame()
   ↓
FlashLogStorage
   ↓
FlashDriver
   ↓
External SPI Flash
```

Flash に保存されたログは、後で古い順または最新 N 件として UART DMA 経由で PC 側へ送信できます。

```text
SPI Flash
   ↓
FlashLogStorage
   ↓
UartDma
   ↓
PC logger_viewer.py
```

---

## ログフレーム形式

ログはバイナリフレームとして保存・送信されます。

```text
Magic      : 2 bytes
Header     : 12 bytes
Payload    : 0〜64 bytes
CRC32      : 4 bytes
```

### Magic

```text
0xA5 0x5A
```

### Header

```cpp
struct LogFrameHeader {
    uint32_t seq;
    uint16_t event_id;
    uint8_t  level;
    uint8_t  length;
    uint32_t timestamp_us;
};
```

### CRC32

CRC32 は以下を対象に計算します。

```text
Magic + Header + Payload
```

CRC の対象に CRC32 自身は含めません。

---

## FlashLogStorage の特徴

`FlashLogStorage` は SPI Flash 上にログフレームを保存するためのクラスです。

主な特徴は以下です。

- Flash 領域を sector 単位で管理
- 書き込み位置 `write_addr_` を保持
- 最古フレーム `oldest_addr_` を保持
- 最新フレーム `newest_addr_` と `newest_seq_` を保持
- 有効フレーム数 `valid_frame_count_` を保持
- フレームの妥当性を magic / length / CRC32 で確認
- 起動時に Flash 全体をスキャンして index を再構築

---

## Flash 書き込み方針

### sector を跨ぐフレームは書かない

1つのログフレームが sector 境界を跨ぎそうな場合は、次の sector 先頭へ移動します。

```text
sector A
[ frame ][ frame ][ 残りが足りない ]

↓ 次の sector へ移動

sector B
[ new frame ]
```

### page 境界は分割して書く

SPI Flash の page program は 256 byte 境界を跨げないため、必要に応じて page 単位に分割して書き込みます。

### sector 先頭に書く前に erase する

書き込み位置が sector 先頭の場合、その sector を erase してから書き込みます。

---

## 電源断復旧の考え方

電源断が発生すると、Flash 上に以下のような状態が残る可能性があります。

```text
[正常 frame0][正常 frame1][正常 frame2][書きかけ frame3...]
```

書きかけフレームは CRC32 が一致しない、または header が不正になるため、有効フレームとして扱いません。

起動時には `rebuildIndexFromFlash()` により Flash をスキャンし、有効なフレームだけを index に登録します。

---

## dirty tail 対策

書きかけフレームがある場合、最後の正常フレームの直後には不完全なデータが残っています。

例：

```text
0x00001000: 正常 frame0
0x00001021: 正常 frame1
0x00001042: 正常 frame2
0x00001063: 正常 frame3
0x00001084: 正常 frame4
0x000010A5: 書きかけ frame5
```

この場合、単純に `write_addr_ = 0x000010A5` として再開すると危険です。

Flash は erase しない限り、0 にした bit を 1 に戻せないためです。

そのため、復旧時に `write_addr_` が汚れている場合は、次の sector 先頭へ退避します。

```text
dirty tail detected
↓
write_addr_ = next sector start
```

例：

```text
write_addr_ = 0x000010A5
↓
write_addr_ = 0x00002000
```

---

## 疑似電源断テスト

### テスト内容

疑似電源断テストでは、以下を確認します。

1. 正常ログを 5 個保存する
2. 次のログフレームを先頭 8 byte だけ Flash に直接書く
3. 再起動相当として `FlashLogStorage` を再初期化する
4. 書きかけフレームを無効として扱う
5. `write_addr_` を次の sector へ退避する
6. 復旧後にログ追記できることを確認する
7. 古い順に読み出せることを確認する
8. UART DMA で送信し、PC 側 viewer で CRC OK を確認する

---

## 疑似電源断テストの確認結果

確認例：

```text
=== storage ===
start   = 0x00001000
end     = 0x00003000
write   = 0x000010A5
oldest  = 0x00001000
newest  = 0x00001084
seq     = 4
count   = 5
remain  = 8027
```

この時点で、`seq=0` から `seq=4` までの 5 フレームが保存されています。

その後、`seq=5` 相当のフレームを先頭 8 byte だけ書き込み、疑似電源断状態を作ります。

```text
pseudo power cut addr=0x000010A5
full frame len=49
write only first 8 bytes
```

復旧後：

```text
=== restored ===
start   = 0x00001000
end     = 0x00003000
write   = 0x00002000
oldest  = 0x00001000
newest  = 0x00001084
seq     = 4
count   = 5
remain  = 4096
```

ポイントは以下です。

```text
seq     = 4
count   = 5
write   = 0x00002000
```

つまり、

- 書きかけ `seq=5` は無効扱い
- 正常な `seq=0〜4` は復旧
- 汚れた `0x000010A5` は避ける
- 次 sector の `0x00002000` へ退避

できています。

復旧後にさらに 3 フレーム追加すると、以下のようになります。

```text
=== after append following restore ===
start   = 0x00001000
end     = 0x00003000
write   = 0x00002075
oldest  = 0x00001000
newest  = 0x0000204E
seq     = 7
count   = 8
remain  = 3979
```

これにより、

```text
seq=0〜4 : 電源断前の正常ログ
seq=5〜7 : 復旧後に追記したログ
```

として保存できていることが確認できます。

---

## UART 送信確認

Flash から読み出したログを UART DMA 経由で PC へ送信し、`logger_viewer.py` で受信します。

確認例：

```text
seq=0 INFO TEXT_LOG msg="normal frame 0" crc=OK
seq=1 INFO TEXT_LOG msg="normal frame 1" crc=OK
seq=2 INFO TEXT_LOG msg="normal frame 2" crc=OK
seq=3 INFO TEXT_LOG msg="normal frame 3" crc=OK
seq=4 INFO TEXT_LOG msg="normal frame 4" crc=OK
seq=5 INFO TEXT_LOG msg="after restore frame 5" crc=OK
seq=6 INFO TEXT_LOG msg="after restore frame 6" crc=OK
seq=7 INFO TEXT_LOG msg="after restore frame 7" crc=OK
```

すべて `crc=OK` になっているため、以下を確認できています。

- Flash 内のフレームが壊れていない
- Flash から正しく読み出せている
- UART DMA で送信できている
- PC 側 viewer で正しく復号できている

---

## ビルド方法

### 環境変数

`PICO_SDK_PATH` を設定しておきます。

例：

```bash
export PICO_SDK_PATH=/home/user/pico/pico-sdk
```

### ビルド

```bash
./mk.sh
```

### クリーンビルド

```bash
./mk.sh clean
```

---

## 実行モードの切り替え

`CMakeLists.txt` で以下の compile definition を切り替えます。

### 通常アプリモード

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_NORMAL_APP=1)
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_PSEUDO_POWER_CUT_TEST=0)
```

### 疑似電源断テストモード

```cmake
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_NORMAL_APP=0)
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_PSEUDO_POWER_CUT_TEST=1)
```

注意点として、`target_compile_definitions()` は `add_executable()` の後に書きます。

正しい順序：

```cmake
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/EventLogger.cpp
    src/UartDma.cpp
    src/FlashLogStorage.cpp
    src/FlashDriver.cpp
    src/Crc32.cpp
    ...
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_NORMAL_APP=1)
target_compile_definitions(${PROJECT_NAME} PRIVATE RUN_PSEUDO_POWER_CUT_TEST=0)
```

---

## PC 側でのログ受信

UART から送信されるログはバイナリフレームです。

そのため、Minicom では正しく読めません。  
確認には `logger_viewer.py` を使用します。

例：

```bash
python3 logger_viewer.py --port /dev/ttyACM0 --baud 460800
```

baudrate は Pico 側の `UartDma` 設定と一致させてください。

---

## Minicom 使用時の注意

Minicom は主にコマンド入力や `printf` 確認用です。

バイナリログ送信中は、表示不能文字や制御文字が混ざるため、画面上では文字化けしたように見える場合があります。

バイナリログの正否は Minicom ではなく、`logger_viewer.py` の `crc=OK` で確認します。

---

## コマンド例

通常アプリモードでは、コマンドタスクからログ操作を行います。

例：

```text
s    : FlashLogStorage の状態表示
p    : ログ生成停止
r    : ログ生成再開
a    : 全ログを古い順に UART 送信
l10  : 最新 10 件を UART 送信
```

---

## 現在確認済みの内容

以下を確認済みです。

```text
OK: SPI Flash JEDEC ID 読み出し
OK: Flash sector erase
OK: Flash page program
OK: ログフレーム append
OK: CRC32 検証
OK: Flash 上の有効フレーム scan
OK: oldest / newest / count の index 再構築
OK: 疑似電源断による書きかけフレーム検出
OK: 書きかけフレームの無効化
OK: dirty tail 検出
OK: 次 sector への write_addr_ 退避
OK: 復旧後 append
OK: 古い順読み出し
OK: UART DMA 送信
OK: logger_viewer.py による CRC OK 確認
```

---

## 今後の改善候補

今後の改善候補は以下です。

- テスト用 main と通常 main の切り替えをさらに整理する
- CMake option でテストモードを選択できるようにする
- `DEBUG_FlashLogStorage` の出力を整理する
- `invalid length=255` などの debug 出力を release 時には抑制する
- Flash log 領域を設定ファイル化する
- 電源断テストのパターンを増やす
  - magic だけ書いた場合
  - header 途中で止まった場合
  - payload 途中で止まった場合
  - CRC 途中で止まった場合
  - sector 境界付近で止まった場合
- README に実測ログを追加する
- GitHub Actions で host 側ユニットテストを追加する

---

## ライセンス

MIT License

Pico SDK、FreeRTOS、その他外部ライブラリを使用する場合は、それぞれのライセンスに従ってください。

---

## まとめ

このプロジェクトでは、Raspberry Pi Pico 上で FreeRTOS、UART DMA、外部 SPI Flash を組み合わせ、電源断に強いログ保存システムを構築しています。

疑似電源断テストにより、書きかけフレームを無効として扱い、最後の正常フレームまで復旧し、さらに安全な sector からログ追記を再開できることを確認しました。

また、復旧後のログを UART DMA で送信し、PC 側 viewer で全フレーム `crc=OK` として確認できています。
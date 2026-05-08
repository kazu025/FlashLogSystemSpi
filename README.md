---
# FlashLogNUartOut

Raspberry Pi Pico + FreeRTOS + UART DMA + SPI Flash を使った、実験用のフラッシュログ保存システムです。

通常動作中に生成したログを外部SPI Flashへ保存し、USB CDCコマンドから以下の操作ができます。

- Flashログの全件UART出力
- 最新N件だけUART出力
- ログ生成の一時停止 / 再開
- ログ領域のerase
- FlashLogStorageの状態表示

UART出力されたログは、PC側の `logger_viewer.py` で受信・CRC確認・seq確認できます。

---

## 目的

組込み機器では、実行中のログをすべてUARTへ出し続けると、通信帯域不足や取りこぼしが起きることがあります。

このプロジェクトでは、ログをいったん外部SPI Flashへ保存し、必要なタイミングで後から取り出せる仕組みを作っています。

主な目的は以下です。

- 実機上で発生したイベントをFlashに保存する
- 起動後にFlash上の有効ログを復元する
- リングバッファ方式で古いログを上書きする
- 最新N件だけを後からUARTへ再出力する
- Viewer側でCRCとseqを確認できるようにする
- FreeRTOS環境でFlashアクセスをMutex保護する

---

## システム構成

```text
Raspberry Pi Pico
  |
  +-- FreeRTOS
  |
  +-- EventLogger
  |     |
  |     +-- LogFrame生成
  |
  +-- FlashLogStorage
  |     |
  |     +-- SPI Flashへ保存
  |     +-- リングバッファ管理
  |     +-- 起動時restore
  |     +-- 最新N件出力
  |     +-- 全件出力
  |
  +-- UartDma
  |     |
  |     +-- UART DMAでPCへバイナリログ送信
  |
  +-- USB CDC
        |
        +-- コマンド入力
````

PC側では、UARTに接続した `logger_viewer.py` がバイナリログを受信します。

```text
Pico UART
  ↓
USB-UART変換
  ↓
PC /dev/ttyUSB0
  ↓
logger_viewer.py
```

USB CDCはコマンド操作用です。

```text
Pico USB CDC
  ↓
PC /dev/ttyACM0
  ↓
picocom / minicom
```

---

## 主な機能

### 1. Flashリングバッファ保存

ログフレームを外部SPI Flashに保存します。

ログ保存領域はセクタ単位で管理します。

```text
LOG_START_ADDR ～ LOG_END_ADDR
```

現在のテストでは例として以下のような領域を使用しています。

```text
start = 0x00001000
end   = 0x00003000
size  = 8192 bytes
```

ログ領域は `FlashLogStorage` の設定で変更できます。

---

### 2. 起動時restore

起動時にFlashログ領域をスキャンし、有効なログフレームを探します。

復元する情報は以下です。

* 最古ログアドレス
* 最新ログアドレス
* 最新seq
* 次回書き込みアドレス
* 有効フレーム数

これにより、再起動後もFlashに残っているログを読み出せます。

---

### 3. 最新N件UART出力

USB CDCコマンドから、最新N件だけをUARTへ出力できます。

例：

```text
l10
l20
l100
```

出力順は、Viewerで見やすいように古い順です。

例：Flash内の最新seqが `300` の場合

```text
l10
```

を実行すると、

```text
seq=291
seq=292
...
seq=300
```

の順で出力します。

---

### 4. 全件UART出力

Flash内の有効ログを、古い順ですべてUARTへ出力できます。

```text
a
```

リングバッファとして保存されている範囲を、最古ログから最新ログまで順に送信します。

---

### 5. pause / resume

ログ生成中にFlashリプレイを実行すると、通常ログと過去ログが混ざる可能性があります。

そのため、USB CDCコマンドで通常ログ生成を一時停止できます。

```text
p : pause log generation
r : resume log generation
```

基本的な使い方は以下です。

```text
p
l20
r
```

意味：

```text
p    通常ログ生成を停止
l20  Flash内の最新20件をUARTへ出力
r    通常ログ生成を再開
```

これにより、リプレイ中に新しいログが混ざることを防ぎます。

---

### 6. FreeRTOS Recursive Mutexによる排他

FlashLogStorageでは、Flashアクセスと内部状態更新を保護するためにFreeRTOSのRecursive Mutexを使用します。

対象となる主な処理は以下です。

* `append()`
* `eraseLogArea()`
* `read()`
* `readFrame()`
* `sendFramesOldestFirst()`
* `sendLatestFrames()`
* `restoreWriteAddress()`

`sendLatestFrames()` や `sendFramesOldestFirst()` は内部で `readFrame()` を呼ぶため、通常のMutexではなくRecursive Mutexを使用しています。

これにより、同じタスク内で再帰的にロックしてもデッドロックしないようにしています。

---

## ログフレーム仕様

ログはバイナリフレームとして保存・送信します。

基本構造は以下です。

```text
Magic  + Header + Payload + CRC32
```

### フレーム構造

```text
Magic       : 2 bytes
Header      : 12 bytes
Payload     : variable
CRC32       : 4 bytes
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

### CRC

CRC32は以下を対象に計算します。

```text
Magic + Header + Payload
```

CRCフィールド自体は計算対象に含めません。

---

## USB CDCコマンド

USB CDC側でコマンドを入力します。

例：

```bash
picocom /dev/ttyACM0 -b 115200
```

### コマンド一覧

```text
h       help表示
s       FlashLogStorage状態表示
p       ログ生成pause
r       ログ生成resume
a       全件を古い順にUART出力
lN      最新N件をUART出力
e       ログ領域erase
```

### 使用例

```text
s
```

状態表示：

```text
=== FlashLogStorage Status ===
start   = 0x00001000
end     = 0x00003000
write   = 0x00001386
oldest  = 0x00001000
newest  = 0x00001360
count   = 24
remain  = 7290
Paused  = no
==============================
```

最新10件出力：

```text
p
l10
r
```

全件出力：

```text
p
a
r
```

ログ領域erase：

```text
p
e
s
```

erase後は以下のようになります。

```text
count   = 0
write   = start address
remain  = log area size
```

---

## logger_viewer.py の使い方

UART側のバイナリログをPCで受信します。

例：

```bash
./logger_viewer.py --port /dev/ttyUSB0 --baud 460800
```

正常に受信できると、以下のように表示されます。

```text
[ 839165003 us] seq=29 INFO TEXT_LOG msg="wrap restore frame29" crc=OK seq_ok=1 ok=1 ng=0 gap=0 late=0 dup=0
[ 839664997 us] seq=30 INFO TEXT_LOG msg="wrap restore frame30" crc=OK seq_ok=1 ok=2 ng=0 gap=0 late=0 dup=0
```

重要な確認ポイントは以下です。

```text
crc=OK
crc_ng=0
gap=0
```

---

## late / dup について

Flashリプレイでは、すでにViewerが受信済みのseqを再送することがあります。

その場合、Viewerでは以下のような表示になることがあります。

```text
late
dup
```

これは必ずしも異常ではありません。

### late

すでに新しいseqを見たあとに、古いseqが来たという意味です。

例：

```text
seq=20 を受信済み
その後に seq=10 が来た
```

### dup

同じseqをもう一度受信したという意味です。

例：

```text
seq=20 を受信済み
その後にまた seq=20 が来た
```

Flashリプレイでは、過去ログを再送するため `late` や `dup` が出ることがあります。

ただし、以下が正常ならFlash読み出しやUART送信は正常です。

```text
crc=OK
crc_ng=0
```

---

## ビルド方法

例：

```bash
mkdir -p debug
cd debug
cmake ..
make -j4
```

または、プロジェクトに `mk.sh` がある場合は以下でビルドします。

```bash
./mk.sh
```

---

## CMake設定の注意

USB CDCをコマンド用に使い、UARTをバイナリログ専用にするため、CMakeでは以下のようにするのが望ましいです。

```cmake
pico_enable_stdio_usb(event_logger 1)
pico_enable_stdio_uart(event_logger 0)
```

`event_logger` は実際のターゲット名に合わせてください。

この構成にすると、

```text
USB CDC:
    printf表示
    コマンド入力

UART:
    バイナリログ出力
```

を分離できます。

UART側に `printf()` の文字列が混ざると、`logger_viewer.py` のバイナリ解析を邪魔する可能性があります。

---

## FreeRTOSConfig.h の注意

Recursive Mutexを使うため、`FreeRTOSConfig.h` で以下が有効になっている必要があります。

```c
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
```

---

## 動作確認済みの項目

以下を確認済みです。

### 最新N件出力

```text
l1
l5
l10
l20
l100
```

期待通り、最新N件が古い順で出力されました。

例：

```text
l10 → 最新10件を出力
```

### erase後の空ログ処理

erase直後に以下を実行しても正常に処理されます。

```text
a
l10
```

表示例：

```text
sendFramesOldestFirst: no valid frame
sendLatestFrames: no valid frame
```

### pause / resume

```text
p
s
l20
r
s
```

により、ログ生成停止・リプレイ・再開を確認しました。

### 全件出力

`a` コマンドで、Flash内の全有効ログを古い順で出力できることを確認しました。

例：

```text
seq=29
seq=30
...
seq=67
```

39件が古い順で出力され、以下を確認しました。

```text
crc_ng=0
gap=0
late=0
dup=0
```

### Mutex確認

`send latest` や `send all` 実行後に `send done` まで戻り、resume後も通常ログ生成が再開することを確認しました。

つまり、Recursive Mutex導入後もデッドロックは発生していません。

---

## 現在の到達点

このプロジェクトでは、以下の基本機能が動作しています。

```text
Flash保存
リングバッファ管理
起動時restore
全件読み出し
最新N件読み出し
USB CDCコマンド操作
pause/resume
FreeRTOS Mutex排他
ViewerでCRC確認
```

実用的なFlashログ機構の基本形として使える段階です。

---

## 今後の課題

今後の改善候補です。

### 1. 長時間耐久テスト

数千〜数万件のログを書き込み、リング周回後も以下が正しく動くか確認します。

* restore
* latest N
* all replay
* erase
* CRC確認

### 2. 電源断復旧テスト

Flash書き込み途中で電源断が起きた場合でも、有効なフレームだけを復元できるか確認します。

### 3. logger_viewer.py のリプレイモード

Flashリプレイ時は `late` / `dup` が出やすいため、Viewer側にリプレイモードを追加すると見やすくなります。

例：

```bash
./logger_viewer.py --port /dev/ttyUSB0 --baud 460800 --replay
```

リプレイモードでは、seqが戻っても警告扱いしないようにできます。

### 4. ログ検索機能

将来的には以下のような検索機能を追加できます。

* event_id指定
* seq範囲指定
* 最新エラーのみ出力
* 指定レベル以上のみ出力

### 5. Flash使用量の可視化

USB CDCの `s` コマンドで、Flash使用率を表示できるようにすると便利です。

例：

```text
used    = 1482
remain  = 6710
usage   = 18%
```

---

## ライセンス

このプロジェクト内の自作コードは、MIT Licenseなどの任意のライセンスで公開できます。

Pico SDK、FreeRTOS、TinyUSBなどの外部ライブラリについては、それぞれのライセンスに従ってください。


---

## メモ

このプロジェクトは学習・実験用です。

Raspberry Pi Pico、FreeRTOS、UART DMA、SPI Flash、CRC、リングバッファ、Mutex排他を組み合わせた、組込みログ機構の実装例として作成しています。

```
```


````markdown
# FlashLogStorageRing

Raspberry Pi Pico（RP2040）で、外付けSPI Flashにバイナリログを保存し、リングバッファとして古いログを上書きしながら保持する実験プロジェクトです。

保存したログは、再起動後にFlashから復元し、古い順にUARTへ再送信できます。PC側の `logger_viewer.py` で受信すると、保存済みログを通常のUARTログと同じ形式で復元できます。

## 特徴

- Raspberry Pi Pico + 外付けSPI Flashを使用
- Flash上にログ保存領域を作成
- ログをバイナリフレーム形式で保存
- CRC32でフレーム破損を検出
- Flashログ領域をリングバッファとして使用
- Flash再起動後にログインデックスを復元
- 古い順にログを読み出し
- 保存済みログをUARTへ再送信
- PC側Viewerでログを復元可能

## 目的

組み込み機器では、異常発生時のログを後から取り出したいことがあります。

UARTへリアルタイム送信するだけでは、PCを接続していない間のログは失われます。そこで、ログを外付けFlashに保存し、必要なタイミングでUARTから取り出せるようにします。

このプロジェクトでは、以下の流れを確認しています。

```text
ログ生成
  ↓
バイナリフレーム化
  ↓
SPI Flashへ保存
  ↓
リングバッファとして古いログを上書き
  ↓
再起動後にFlashをスキャンして復元
  ↓
古い順に読み出し
  ↓
UARTへ再送信
  ↓
PC Viewerで復元
````

## 動作確認済みの内容

以下の流れを確認済みです。

```text
Flash保存
restoreWriteAddress()
dumpFramesOldestFirst()
readFrame()
readFramesOldestFirstTest()
sendFramesOldestFirst()
logger_viewer.pyで受信
CRC OK
seq OK
```

Viewer側では、保存済みログが以下のように復元されました。

```text
seq=106 ... crc=OK seq_ok=1
seq=107 ... crc=OK seq_ok=1
...
seq=300 ... crc=OK seq_ok=1
```

つまり、Flash上に残っている有効ログを、古い順にUARTへ送信できています。

## ハードウェア構成

想定している構成は以下です。

```text
Raspberry Pi Pico
  |
  | SPI
  |
外付けSPI Flash
  |
  | UART
  |
USB-UART変換
  |
PC Viewer
```

## 使用している主な要素

* Raspberry Pi Pico / RP2040
* Pico SDK
* C++
* SPI Flash
* UART
* バイナリログフレーム
* CRC32
* リングバッファ
* Python製ログViewer

## Flashログ領域

Flash上の一部領域をログ保存用として使います。

ログ領域の開始アドレスと終了アドレスは、ヘッダ側の定数で定義します。

例：

```cpp
LOG_START_ADDR
LOG_END_ADDR
```

ログ領域は、Flashのセクタサイズに揃える必要があります。

```cpp
start_addr % SECTOR_SIZE == 0
end_addr   % SECTOR_SIZE == 0
```

## ログフレーム構造

Flashには、UART送信時と同じバイナリフレームを保存します。

```text
+--------+--------+---------+------+
| magic  | header | payload | CRC  |
+--------+--------+---------+------+
```

### magic

フレームの先頭を識別するための固定値です。

```text
0xA5 0x5A
```

### header

ログのメタ情報を持ちます。

主な情報：

* sequence number
* event id
* log level
* payload length
* timestamp

### payload

ログ本文です。

テキストログの場合は文字列が入ります。

### CRC

magic、header、payloadを対象にCRC32を計算し、フレーム末尾に付加します。

読み出し時にはCRCを再計算し、保存されたCRCと一致するか確認します。

## リングバッファ動作

Flashは、基本的に1bitを `1` から `0` にする方向へしか書き換えられません。

そのため、上書きするにはセクタ単位でeraseする必要があります。

このプロジェクトでは、次のように動作します。

```text
現在の書き込み位置にログを書く
  ↓
セクタ末尾を跨ぐ場合は次のセクタへ移動
  ↓
次の書き込み位置がセクタ先頭なら、そのセクタをerase
  ↓
erase対象セクタに含まれる古いログをindexから除外
  ↓
新しいログを書き込む
```

ログ領域の終端に到達した場合は、開始アドレスへ戻ります。

```text
LOG_END_ADDR到達
  ↓
LOG_START_ADDRへwrap
```

## 注意点

Flashのerase単位はセクタです。

そのため、ログは1フレームずつ消えるのではなく、セクタ単位でまとめて消えます。

例：

```text
seq=1〜105  : erase済み
seq=106〜300: 有効ログとして残っている
```

このように、最古ログが `seq=1` ではなく、途中の `seq=106` から始まることがあります。

これはリングバッファとして正常な動作です。

## インデックス管理

`FlashLogStorage` は以下の情報を保持します。

```cpp
write_addr_
oldest_addr_
newest_addr_
newest_seq_
valid_frame_count_
```

### write_addr_

次にログを書き込むアドレスです。

### oldest_addr_

現在Flash上に残っている有効ログのうち、最も古いログのアドレスです。

### newest_addr_

現在Flash上に残っている有効ログのうち、最も新しいログのアドレスです。

### newest_seq_

最新ログのsequence numberです。

### valid_frame_count_

有効なログフレーム数です。

## 再起動後の復元

再起動するとRAM上のインデックス情報は失われます。

そのため、初期化時にFlashログ領域をスキャンして、有効フレームを探します。

```cpp
restoreWriteAddress();
```

内部では、Flash全体をスキャンし、CRCが正しいフレームだけを有効ログとして扱います。

```text
Flashログ領域を先頭からスキャン
  ↓
magic確認
  ↓
header読み出し
  ↓
payload length確認
  ↓
CRC確認
  ↓
有効フレームとして採用
```

これにより、再起動後でも以下を復元できます。

* 次の書き込み位置
* 最古ログの位置
* 最新ログの位置
* 有効ログ数

## 古い順の読み出し

保存されたログは、`oldest_addr_` から読み始めます。

```cpp
dumpFramesOldestFirst();
```

この関数は、Flash上の有効フレームを古い順に表示します。

リングバッファなので、物理アドレス順とは限りません。

例：

```text
0x2000 seq=106
0x2028 seq=107
...
0x2FC8 seq=207
0x1000 seq=208
0x1028 seq=209
...
0x1E60 seq=300
```

このように、途中でFlashログ領域の先頭へwrapします。

## 保存ログのUART送信

保存済みログは、以下の関数でUARTへ送信できます。

```cpp
sendFramesOldestFirst(uart_dma);
```

内部では、Flashから1フレームずつ読み出し、バイナリフレームをそのままUARTへ送信します。

```text
oldest_addr_から開始
  ↓
readFrame()
  ↓
uart.write_buffer_blocking()
  ↓
次フレームへ
  ↓
newest_seq_まで繰り返し
```

PC側では通常のリアルタイムログと同じように受信できます。

## PC側Viewer

PC側では `logger_viewer.py` を使用します。

例：

```bash
cd debug
./logger_viewer.py --port /dev/ttyUSB0 --baud 460800
```

出力例：

```text
seq=106 INFO TEXT_LOG msg="wrap restore frame 105" crc=OK seq_ok=1
seq=107 INFO TEXT_LOG msg="wrap restore frame 106" crc=OK seq_ok=1
...
seq=300 INFO TEXT_LOG msg="wrap restore frame 299" crc=OK seq_ok=1
```

## ビルド方法

Pico SDKの環境変数を設定しておきます。

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

ビルドスクリプトを使う場合：

```bash
./mk.sh
```

または手動でビルドする場合：

```bash
mkdir -p build
cd build
cmake ..
make -j4
```

生成された `.uf2` ファイルをPicoへ書き込みます。

## ディレクトリ構成例

```text
.
├── CMakeLists.txt
├── README.md
├── include
│   ├── FlashDriver.h
│   ├── FlashLogStorage.h
│   ├── LogProtocol.h
│   ├── LogTypes.h
│   ├── EventLogger.h
│   └── UartDma.h
├── src
│   ├── FlashDriver.cpp
│   ├── FlashLogStorage.cpp
│   ├── EventLogger.cpp
│   ├── UartDma.cpp
│   ├── Crc32.cpp
│   └── main.cpp
├── check
│   └── check.cpp
└── debug
    └── logger_viewer.py
```

## 主なクラス

### FlashDriver

SPI Flashの低レベル操作を担当します。

主な機能：

* JEDEC ID読み出し
* Read
* Page Program
* Sector Erase
* Write Enable
* Busy Wait

### FlashLogStorage

Flash上にログを保存する管理クラスです。

主な機能：

* ログ追記
* リングバッファ管理
* セクタerase時のインデックス更新
* 再起動後のインデックス復元
* 古い順のログ読み出し
* UARTへの保存ログ送信

### EventLogger

ログイベントを作成し、バイナリフレーム化します。

### UartDma

UART送信を担当します。

保存済みログの送信時にも使用します。

## 代表的な処理の流れ

### ログ保存

```text
EventLogger
  ↓
LogProtocolでバイナリフレーム生成
  ↓
FlashLogStorage::append()
  ↓
Flashへ保存
```

### 保存ログの復元

```text
FlashLogStorage::init()
  ↓
restoreWriteAddress()
  ↓
rebuildIndexFromFlash()
  ↓
oldest/newest/write_addrを復元
```

### 保存ログのUART送信

```text
FlashLogStorage::sendFramesOldestFirst()
  ↓
readFrame()
  ↓
uart.write_buffer_blocking()
  ↓
logger_viewer.pyで受信
```

## テスト内容

### wrap後のrestore確認

ログ領域の終端まで書き込み、開始アドレスへwrapした後でも、再起動相当の復元で正しいwrite addressが復元されることを確認します。

確認例：

```text
wrap detected
expected write_addr=0x00001E88
restored write_addr=0x00001E88
restore after wrap OK
```

### 古い順dump確認

保存済みログを古い順に表示します。

確認例：

```text
oldest=0x00002000 newest=0x00001E60 newest_seq=300 count=195
frame: addr=0x00002000 seq=106
...
frame: addr=0x00001E60 seq=300
```

### 古い順read確認

`readFrame()` を使い、Flash上のフレーム本体を古い順に読み出します。

確認例：

```text
readFramesOldestFirstTest: read_count=195
```

### UART再送信確認

保存済みログをUARTへ再送信し、PC側Viewerで確認します。

確認例：

```text
seq=106 crc=OK seq_ok=1
...
seq=300 crc=OK seq_ok=1
```

## 現在できていること

* Flashにログを保存できる
* Flashログ領域をリングバッファとして扱える
* セクタerase時に古いログを無効化できる
* 再起動後にFlashからインデックスを復元できる
* 保存済みログを古い順に読める
* 保存済みログをUARTに再送信できる
* PC側ViewerでCRC OK / seq OKを確認できる

## 今後の改善候補

### デバッグ出力の整理

動作確認用の `printf()` が多いため、本番用には整理します。

例：

```cpp
readFrame: OK ...
addr before=...
```

これらはデバッグ時のみ有効にするのが望ましいです。

### 電源断対策

現在はCRCが正しいフレームのみを有効扱いするため、書き込み途中で電源断が起きても、壊れたフレームは無視できます。

ただし、より堅牢にするなら以下を検討できます。

* フレーム書き込み完了マーカー
* sector単位のメタデータ
* 起動時の詳細な修復処理

### インデックス更新の最適化

現在はFlashスキャンにより復元します。

ログ領域が大きくなると起動時スキャン時間が増えるため、将来的には以下を検討できます。

* sector header
* checkpoint情報
* 最新write address保存領域

### 読み出し条件の追加

現在は保存済みログをすべて送信します。

将来的には以下も追加できます。

* 最新N件だけ送信
* 指定seq以降だけ送信
* event_idでフィルタ
* levelでフィルタ

## メモ

Flashはセクタ単位でeraseする必要があります。

そのため、リングバッファとして使う場合、古いログはセクタ単位で消えます。

これはFlashの仕様に沿った自然な動作です。

## ライセンス

このプロジェクトの自作ソースコードは MIT License で公開します。

Raspberry Pi Pico SDK は BSD-3-Clause License、FreeRTOS は MIT License で提供されています。  
それぞれの外部ライブラリを使用する場合は、各プロジェクトのライセンス条件に従ってください。

詳細は `LICENSE` ファイルを参照してください。

```


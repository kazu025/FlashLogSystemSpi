---

# Flash Ring Buffer Logger (Raspberry Pi Pico / RP2040)

## ■ 概要

本プロジェクトは、Raspberry Pi Pico（RP2040）上で動作する
**高信頼なログシステム（UART + SPI Flash + リングバッファ）**の実装です。

以下の特徴を持ちます：

* バイナリログフレーム（CRC32付き）
* UART DMAによる高速送信
* SPI Flashへの永続ログ保存
* リングバッファによる自動上書き
* 電源再投入後のログ復元（restore）
* 古い順でのログ読み出し

---

## ■ システム構成

```text
[Task / ISR]
      ↓
EventLogger
      ↓
+------------------------+
| UART DMA (リアルタイム) |
+------------------------+
      ↓
+------------------------+
| FlashLogStorage        |
| (リングバッファ)       |
+------------------------+
      ↓
SPI Flash (W25Q32 等)
```

---

## ■ ログフレーム仕様

```text
[magic(2)][header(12)][payload(variable)][crc32(4)]
```

### ● magic

* 固定値：0xA5 0x5A

### ● header

```cpp
struct LogFrameHeader {
    uint32_t seq;
    uint16_t event_id;
    uint8_t  level;
    uint8_t  length;
    uint32_t timestamp_us;
};
```

### ● CRC

* CRC32（Polynomial: 0xEDB88320）
* 計算範囲：magic + header + payload

---

## ■ Flashリングバッファ仕様

### ● ログ領域

```cpp
LOG_START_ADDR ～ LOG_END_ADDR
```

* 4KB sector単位
* sector単位でerase

---

### ● 書き込みルール

1. フレームを書き込む
2. sectorを跨ぐ場合は次sectorへ移動
3. sector先頭でerase実行
4. 領域末尾でwrap（先頭へ戻る）

---

### ● 上書き動作

```text
新しいログ → 古いログを上書き
```

* 明示的な削除は不要
* erase時に古いログが消える

---

## ■ インデックス管理（重要）

本実装では、以下の情報を保持：

```cpp
write_addr_   // 次に書き込む位置
oldest_addr_  // 最古ログ
newest_addr_  // 最新ログ
newest_seq_   // 最新seq
valid_frame_count_
```

---

### ● 高速化ポイント

従来：

```text
appendごとにFlash全スキャン
```

本実装：

```text
append時に差分更新
erase時のみ部分スキャン（sector単位）
```

👉 **O(N) → O(1)**

---

## ■ リストア（電源再投入対応）

```cpp
restoreWriteAddress()
```

Flash全体をスキャンして：

* oldest
* newest
* write_addr

を復元

---

## ■ ログ読み出し

```cpp
dumpFramesOldestFirst()
```

特徴：

* seqベースで順序保証
* wrap対応
* 空き領域は自動スキップ

---

## ■ 動作確認内容

### ✔ リング書き込み

* wrap動作確認
* address循環確認

### ✔ restore

* 電源再投入後の復元
* write_addr一致

### ✔ dump

* 古い順で出力
* seq連続性確認

例：

```text
seq=106 → seq=300
(途中でwrapあり)
```

---

## ■ 使用ハードウェア

* Raspberry Pi Pico (RP2040)
* SPI Flash（例：W25Q32）
* UART（USB CDC or 外部UART）

---

## ■ ビルド環境

* Pico SDK
* FreeRTOS
* CMake
* GCC (arm-none-eabi)

---

## ■ 主なモジュール

| モジュール           | 内容          |
| --------------- | ----------- |
| EventLogger     | ログ生成        |
| UartDma         | UART DMA送信  |
| FlashDriver     | SPI Flash制御 |
| FlashLogStorage | リングバッファ管理   |
| Crc32           | CRC計算       |

---

## ■ 今後の拡張案

* UARTへの過去ログ一括送信
* メタデータ永続化（高速起動）
* ログフィルタ機能
* SDカード保存
* Viewer連携強化

---

## ■ まとめ

本プロジェクトは、

```text
組み込み向けログシステムの実践的な実装例
```

として、

* 高速
* 信頼性
* 永続性

をバランスよく実現しています。

---

## ■ ライセンス

MIT License

---

## ■ 作者メモ

* UART DMA + Flashログの統合がポイント
* seqベースの順序管理がリングの核心
* 「eraseをトリガに古いログが消える」設計がシンプルで強い

---

# Raspberry Pi Pico ADC Logger

Raspberry Pi PicoでADC値を取得し、ログとして保存・出力し、PC側でCSV化してPythonでグラフ化する実験プロジェクトです。

ADCの瞬時値、移動平均値、電圧換算値をログとして扱い、取得したデータを後から確認・解析できるようにすることを目的としています。

---

## 目的

このプロジェクトの目的は、Raspberry Pi Picoを使って、以下の一連の流れを確認することです。

```text
ADC入力を取得
↓
ADC raw値をログ化
↓
移動平均を計算
↓
電圧値に変換
↓
ログをCSVとして保存
↓
Pythonでグラフ化
```

単にADC値を表示するだけでなく、ログデータとして保存し、あとからグラフで確認できる構成を目指しています。

---

## 現在できていること

現在、以下の内容を確認済みです。

- Raspberry Pi PicoでADC値を取得
- ADC raw値をログ出力
- 16点移動平均値を計算
- ADC値を電圧に変換
- ログをCSV形式で保存
- Pythonスクリプトでグラフを自動生成
- CRC OKの行だけを対象にしてグラフ化
- ADC raw値、移動平均、電圧、ヒストグラムをPNG出力

---

## システム構成

```text
Raspberry Pi Pico
  |
  | ADC入力
  v
ADC取得タスク
  |
  | raw値 / moving average / voltage
  v
ログ出力
  |
  | UART / Flash / Viewerなど
  v
CSVファイル
  |
  | plot_adc_log.py
  v
PNGグラフ
```

---

## データの流れ

### 1. Pico側

Pico側では、ADC値を読み取り、以下のような情報をログとして出力します。

```text
ADC raw=1115 avg=1112 voltage=0.896
```

主な値は以下です。

| 項目 | 内容 |
|---|---|
| `adc_raw` | ADCの瞬時値 |
| `adc_avg` | 移動平均後のADC値 |
| `adc_voltage` | ADC平均値を電圧換算した値 |

---

### 2. PC側

PC側では、ログをCSVとして保存します。

CSVには以下のような列が含まれます。

| 列名 | 内容 |
|---|---|
| `pc_time` | PC側で受信した時刻 |
| `seq` | ログのシーケンス番号 |
| `event_id` | イベントID |
| `event_name` | イベント名 |
| `level_name` | ログレベル名 |
| `timestamp_us` | Pico側のタイムスタンプ |
| `payload_text` | ログ本文 |
| `adc_raw` | ADC raw値 |
| `adc_avg` | ADC移動平均値 |
| `adc_voltage` | 電圧値 |
| `crc_ok` | CRCチェック結果 |

---

## グラフ作成スクリプト

ADCログCSVをグラフ化するために、`plot_adc_log.py` を使用します。

### 必要なPythonライブラリ

以下のライブラリを使用します。

```bash
pip install pandas matplotlib
```

仮想環境を使う場合は、例えば以下のようにします。

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install pandas matplotlib
```

---

## 使い方

### 基本実行

```bash
python3 plot_adc_log.py log.csv
```

デフォルトでは、`graphs` ディレクトリにグラフ画像が出力されます。

```text
graphs/adc_raw_avg.png
graphs/adc_voltage.png
graphs/adc_raw_hist.png
graphs/adc_voltage_hist.png
```

---

### 出力先を指定する場合

```bash
python3 plot_adc_log.py log.csv --outdir adc_graphs
```

この場合、`adc_graphs` ディレクトリにPNGファイルが出力されます。

---

### 画面にも表示する場合

```bash
python3 plot_adc_log.py log.csv --show
```

PNG保存に加えて、matplotlibの画面表示も行います。

---

### CRC NG行も含める場合

通常は `crc_ok=1` の行だけをグラフ化します。

CRC NG行も含めたい場合は、以下のように実行します。

```bash
python3 plot_adc_log.py log.csv --include-crc-ng
```

---

## 出力されるグラフ

### 1. ADC raw値と移動平均

```text
graphs/adc_raw_avg.png
```

ADCの瞬時値と移動平均値を同じグラフに表示します。

raw値は入力変化にすぐ反応しますが、移動平均値はなめらかに変化します。  
これにより、移動平均による平滑化の効果を確認できます。

---

### 2. ADC電圧

```text
graphs/adc_voltage.png
```

ADC平均値を電圧換算した値を表示します。

Pico側で計算した電圧値の変化を確認できます。

---

### 3. ADC raw値のヒストグラム

```text
graphs/adc_raw_hist.png
```

ADC raw値の分布を表示します。

ADC値のばらつきやノイズ傾向を確認するために使います。

---

### 4. ADC電圧のヒストグラム

```text
graphs/adc_voltage_hist.png
```

電圧値の分布を表示します。

入力信号の安定性や測定値の偏りを確認するために使います。

---

## CSVファイル例

CSVの一部は以下のような形式です。

```csv
pc_time,seq,event_id,event_name,level,level_name,timestamp_us,length,payload_text,adc_raw,adc_avg,adc_voltage,crc_ok
2026-05-12T20:14:55,0,200,TEXT_LOG,0,INFO,2084037045,35,"ADC raw=1115 avg=1112 voltage=0.896",1115,1112.0,0.896,1
2026-05-12T20:14:55,1,200,TEXT_LOG,0,INFO,2085036998,35,"ADC raw=1118 avg=1112 voltage=0.896",1118,1112.0,0.896,1
```

---

## plot_adc_log.py の仕様

`plot_adc_log.py` は、以下の列を使用します。

| 列名 | 必須 | 用途 |
|---|---:|---|
| `adc_raw` | 必須 | ADC raw値のグラフとヒストグラム |
| `adc_avg` | 任意 | 移動平均グラフ |
| `adc_voltage` | 任意 | 電圧グラフとヒストグラム |
| `timestamp_us` | 任意 | 横軸を経過秒に変換 |
| `seq` | 任意 | `timestamp_us` がない場合の横軸 |
| `crc_ok` | 任意 | CRC OK行の抽出 |

横軸は、以下の優先順位で決まります。

```text
timestamp_us がある場合: 開始時刻からの経過秒
timestamp_us がない場合: seq
seq もない場合: 行番号
```

---

## 実行例

```bash
python3 plot_adc_log.py log.csv
```

実行すると、以下のようなサマリが表示されます。

```text
=== ADC log summary ===
all rows  : 31
plot rows : 31
crc OK    : 31
crc NG    : 0
adc_raw    : min=114.000, max=4095.000, mean=2515.516
adc_avg    : min=1112.000, max=2732.000, mean=2173.903
adc_voltage: min=0.896, max=2.202, mean=1.751
=======================
```

---

## 確認ポイント

グラフを見るときは、以下を確認します。

### ADC raw値

- 急激な変化があるか
- 4095付近に張り付いていないか
- 0付近に張り付いていないか
- ノイズが大きすぎないか

### 移動平均値

- raw値に対してなめらかに追従しているか
- 反応が遅れすぎていないか
- 平滑化の効果が出ているか

### 電圧値

- 想定した範囲の電圧になっているか
- 入力変化に対して自然に変化しているか
- 上限・下限に張り付いていないか

### ヒストグラム

- 値がどの範囲に集中しているか
- ノイズのばらつきがどの程度あるか
- 外れ値がないか

---

## 注意点

### ADC入力電圧について

Raspberry Pi PicoのADC入力は、通常0Vから3.3Vの範囲で扱います。  
3.3Vを超える電圧を直接入力しないように注意してください。

### ADC値の上限

12bit ADCの場合、ADC raw値は以下の範囲になります。

```text
0 ～ 4095
```

`adc_raw=4095` が頻繁に出る場合、入力電圧が上限付近に達している可能性があります。

### 移動平均について

移動平均を大きくすると、ノイズは減りますが、変化への追従は遅くなります。

今回の例では、16点移動平均を想定しています。

---

## 今後の拡張案

今後、以下のような拡張が考えられます。

- CSVファイル名を自動で日付付きにする
- 複数CSVをまとめて比較する
- ADC raw値と電圧値を同じ図に表示する
- 平均値、最大値、最小値をCSVに追記する
- グラフにしきい値ラインを表示する
- 異常値を検出する
- 長時間ログの分割保存に対応する
- Flashログから読み出したデータを直接グラフ化する
- Note記事やREADME用にグラフ画像を自動コピーする

---

## ディレクトリ構成例

```text
.
├── README.md
├── plot_adc_log.py
├── log.csv
└── graphs/
    ├── adc_raw_avg.png
    ├── adc_voltage.png
    ├── adc_raw_hist.png
    └── adc_voltage_hist.png
```

Pico側のソースコードも含める場合は、以下のような構成にできます。

```text
.
.
├── CMakeLists.txt
├── FreeRTOS-Kernel
├── LICENSE
├── README.md
├── check
│   ├── check.cpp
│   └── check.h
├── debug
│   ├── adc_log.csv
│   ├── graphs
│   ├── logger_stas
│   ├── logger_viewer.py
│   └── plot_adc_log.py
├── include
│   ├── AdcLoggerTask.h
│   ├── CommandTask.h
│   ├── Crc32.h
│   ├── EventId.h
│   ├── EventLogger.h
│   ├── FlashDriver.h
│   ├── FlashLogStorage.h
│   ├── FreeRTOSConfig.h
│   ├── LogPayloads.h
│   ├── LogProtocol.h
│   ├── LogTypes.h
│   ├── UartDma.h
│   ├── freertos_hooks.h
│   ├── led25.h
│   └── utility.h
├── ld.sh
├── mk.sh
└── src
    ├── AdcLoggerTask.cpp
    ├── Crc32.cpp
    ├── EventLogger.cpp
    ├── FlashDriver.cpp
    ├── FlashLogStorage.cpp
    ├── LogEvent.cpp
    ├── UartDma.cpp
    ├── freertos_hooks.c
    ├── led25.c
    ├── main.cpp
    ├── task_command.cpp
    └── utility.cpp


```

---

## ライセンス

MIT License

Pico SDK、FreeRTOS、その他外部ライブラリを使用している場合は、それぞれのライセンスにも従ってください。

---

## メモ

このプロジェクトは、Raspberry Pi Picoを使ったADC測定、ログ保存、CSV解析、Pythonグラフ化の基本的な流れを確認するためのものです。

組込みソフトウェアのログ設計、データ可視化、実験記録の自動化の練習として利用できます。

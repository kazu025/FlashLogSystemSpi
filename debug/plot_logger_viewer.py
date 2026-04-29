import pandas as pd
import matplotlib.pyplot as plt
import re

CSV_FILE = "log.csv"

def parse_stats(text: str):
    if not isinstance(text, str):
        return None
    m = re.search(
        r"LOGGER_STATS\[v(?P<ver>\d+)\]\s+ok=(?P<ok>\d+)\s+drop=(?P<drop>\d+)\s+tx_frames=(?P<tx_frames>\d+)\s+tx_bytes=(?P<tx_bytes>\d+)\s+hwm=(?P<hwm>\d+)",
        text,
    )
    if not m:
        return None
    return {
        "version": int(m.group("ver")),
        "ok": int(m.group("ok")),
        "drop": int(m.group("drop")),
        "tx_frames": int(m.group("tx_frames")),
        "tx_bytes": int(m.group("tx_bytes")),
        "hwm": int(m.group("hwm")),
    }

def main():
    df = pd.read_csv(CSV_FILE)

    stats_rows = df[df["event_name"] == "LOGGER_STATS"].copy()
    parsed = stats_rows["payload_text"].apply(parse_stats)

    parsed_df = pd.DataFrame([x for x in parsed if x is not None])
    stats_rows = stats_rows.reset_index(drop=True).iloc[:len(parsed_df)]

    if parsed_df.empty:
        print("LOGGER_STATS が見つかりませんでした。")
        return

    out = pd.concat([stats_rows, parsed_df], axis=1)

    # 横軸を単純なサンプル番号にする
    x = range(len(out))

    plt.figure(figsize=(10, 4))
    plt.plot(x, out["drop"])
    plt.title("LOGGER_STATS: drop")
    plt.xlabel("sample")
    plt.ylabel("drop")
    plt.grid(True)
    plt.show()

    plt.figure(figsize=(10, 4))
    plt.plot(x, out["hwm"])
    plt.title("LOGGER_STATS: high_water_mark")
    plt.xlabel("sample")
    plt.ylabel("hwm")
    plt.grid(True)
    plt.show()

    plt.figure(figsize=(10, 4))
    plt.plot(x, out["tx_bytes"])
    plt.title("LOGGER_STATS: tx_bytes")
    plt.xlabel("sample")
    plt.ylabel("tx_bytes")
    plt.grid(True)
    plt.show()

    print(out[["pc_time", "ok", "drop", "tx_frames", "tx_bytes", "hwm"]].tail())

if __name__ == "__main__":
    main()
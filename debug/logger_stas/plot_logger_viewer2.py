import pandas as pd
import matplotlib.pyplot as plt
import re

CSV_FILE = "log.csv"
CHUNK_SIZE = 50000

pattern = re.compile(
    r"LOGGER_STATS\[v(?P<ver>\d+)\]\s+"
    r"ok=(?P<ok>\d+)\s+"
    r"drop=(?P<drop>\d+)\s+"
    r"tx_frames=(?P<tx_frames>\d+)\s+"
    r"tx_bytes=(?P<tx_bytes>\d+)\s+"
    r"hwm=(?P<hwm>\d+)"
)

def parse_stats(text: str):
    if not isinstance(text, str):
        return None
    m = pattern.search(text)
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

def load_logger_stats(csv_file: str) -> pd.DataFrame:
    rows = []

    reader = pd.read_csv(
        csv_file,
        usecols=["pc_time", "event_name", "payload_text"],
        chunksize=CHUNK_SIZE,
    )

    for chunk in reader:
        stats_rows = chunk[chunk["event_name"] == "LOGGER_STATS"]

        for _, row in stats_rows.iterrows():
            parsed = parse_stats(row["payload_text"])
            if parsed is None:
                continue

            rows.append({
                "pc_time": row["pc_time"],
                "version": parsed["version"],
                "ok": parsed["ok"],
                "drop": parsed["drop"],
                "tx_frames": parsed["tx_frames"],
                "tx_bytes": parsed["tx_bytes"],
                "hwm": parsed["hwm"],
            })

    return pd.DataFrame(rows)

def main():
    out = load_logger_stats(CSV_FILE)

    if out.empty:
        print("LOGGER_STATS が見つかりませんでした。")
        return

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
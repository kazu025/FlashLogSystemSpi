#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_adc_log.py

ADCログCSVを読み込み、以下のグラフをPNGで出力する。

出力:
  1. adc_raw_avg.png       : adc_raw と adc_avg
  2. adc_voltage.png       : adc_voltage
  3. adc_raw_hist.png      : adc_raw のヒストグラム
  4. adc_voltage_hist.png  : adc_voltage のヒストグラム

想定CSV列:
  seq
  timestamp_us
  adc_raw
  adc_avg
  adc_voltage
  crc_ok

使い方:
  python3 plot_adc_log.py log.csv

出力先を指定:
  python3 plot_adc_log.py log.csv --outdir graphs

画面にも表示:
  python3 plot_adc_log.py log.csv --show

CRC NG行も含める:
  python3 plot_adc_log.py log.csv --include-crc-ng
"""

from __future__ import annotations

import argparse
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


def to_numeric_if_exists(df: pd.DataFrame, columns: list[str]) -> pd.DataFrame:
    """存在する列だけ数値化する。"""
    for col in columns:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def make_x_axis(df: pd.DataFrame) -> tuple[pd.Series, str]:
    """
    横軸を作る。

    優先順位:
      1. timestamp_us があれば、開始時刻からの経過秒
      2. seq があれば、seq
      3. どちらもなければ、行番号
    """
    if "timestamp_us" in df.columns and df["timestamp_us"].notna().any():
        x = (df["timestamp_us"] - df["timestamp_us"].iloc[0]) / 1_000_000.0
        return x, "Elapsed time [s]"

    if "seq" in df.columns and df["seq"].notna().any():
        return df["seq"], "seq"

    return pd.Series(range(len(df))), "sample index"


def save_raw_avg_graph(df: pd.DataFrame, x: pd.Series, xlabel: str, outpath: Path, show: bool) -> None:
    """adc_raw と adc_avg を同じグラフに描く。"""
    if "adc_raw" not in df.columns:
        print("[skip] adc_raw column not found")
        return

    plt.figure(figsize=(10, 5))

    plt.plot(x, df["adc_raw"], marker="o", label="ADC raw")

    if "adc_avg" in df.columns:
        plt.plot(x, df["adc_avg"], marker="o", label="ADC moving average")

    plt.xlabel(xlabel)
    plt.ylabel("ADC count")
    plt.title("ADC raw and moving average")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(outpath, dpi=150)

    if show:
        plt.show()
    else:
        plt.close()

    print(f"[save] {outpath}")


def save_voltage_graph(df: pd.DataFrame, x: pd.Series, xlabel: str, outpath: Path, show: bool) -> None:
    """adc_voltage を描く。"""
    if "adc_voltage" not in df.columns:
        print("[skip] adc_voltage column not found")
        return

    plt.figure(figsize=(10, 5))
    plt.plot(x, df["adc_voltage"], marker="o", label="ADC voltage")
    plt.xlabel(xlabel)
    plt.ylabel("Voltage [V]")
    plt.title("ADC voltage")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    plt.savefig(outpath, dpi=150)

    if show:
        plt.show()
    else:
        plt.close()

    print(f"[save] {outpath}")


def save_histogram(df: pd.DataFrame, column: str, xlabel: str, title: str, outpath: Path, show: bool) -> None:
    """指定列のヒストグラムを描く。"""
    if column not in df.columns:
        print(f"[skip] {column} column not found")
        return

    values = df[column].dropna()

    if values.empty:
        print(f"[skip] {column} has no valid data")
        return

    plt.figure(figsize=(10, 5))
    plt.hist(values, bins=30)
    plt.xlabel(xlabel)
    plt.ylabel("Count")
    plt.title(title)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(outpath, dpi=150)

    if show:
        plt.show()
    else:
        plt.close()

    print(f"[save] {outpath}")


def print_summary(df_all: pd.DataFrame, df_plot: pd.DataFrame) -> None:
    """簡単な統計情報を表示する。"""
    print()
    print("=== ADC log summary ===")
    print(f"all rows  : {len(df_all)}")
    print(f"plot rows : {len(df_plot)}")

    if "crc_ok" in df_all.columns:
        crc_ok_count = int((df_all["crc_ok"] == 1).sum())
        crc_ng_count = int((df_all["crc_ok"] != 1).sum())
        print(f"crc OK    : {crc_ok_count}")
        print(f"crc NG    : {crc_ng_count}")

    for col in ["adc_raw", "adc_avg", "adc_voltage"]:
        if col in df_plot.columns:
            s = df_plot[col].dropna()
            if not s.empty:
                print(
                    f"{col:11s}: "
                    f"min={s.min():.3f}, "
                    f"max={s.max():.3f}, "
                    f"mean={s.mean():.3f}"
                )

    print("=======================")
    print()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot ADC log CSV generated from Pico logger."
    )
    parser.add_argument(
        "csv",
        nargs="?",
        default="log.csv",
        help="input CSV file path. default: log.csv",
    )
    parser.add_argument(
        "--outdir",
        default="graphs",
        help="output directory. default: graphs",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="show graphs on screen",
    )
    parser.add_argument(
        "--include-crc-ng",
        action="store_true",
        help="include rows where crc_ok is not 1",
    )

    args = parser.parse_args()

    csv_path = Path(args.csv)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    if not csv_path.exists():
        raise FileNotFoundError(f"CSV file not found: {csv_path}")

    df = pd.read_csv(csv_path)

    numeric_columns = [
        "seq",
        "event_id",
        "level",
        "timestamp_us",
        "length",
        "adc_raw",
        "adc_avg",
        "adc_voltage",
        "crc_ok",
    ]
    df = to_numeric_if_exists(df, numeric_columns)

    # CRC OK の行だけを基本対象にする
    if "crc_ok" in df.columns and not args.include_crc_ng:
        df_plot = df[df["crc_ok"] == 1].copy()
    else:
        df_plot = df.copy()

    if df_plot.empty:
        raise ValueError("No rows to plot. Check crc_ok or use --include-crc-ng.")

    x, xlabel = make_x_axis(df_plot)

    print_summary(df, df_plot)

    save_raw_avg_graph(
        df_plot,
        x,
        xlabel,
        outdir / "adc_raw_avg.png",
        args.show,
    )

    save_voltage_graph(
        df_plot,
        x,
        xlabel,
        outdir / "adc_voltage.png",
        args.show,
    )

    save_histogram(
        df_plot,
        "adc_raw",
        "ADC count",
        "ADC raw histogram",
        outdir / "adc_raw_hist.png",
        args.show,
    )

    save_histogram(
        df_plot,
        "adc_voltage",
        "Voltage [V]",
        "ADC voltage histogram",
        outdir / "adc_voltage_hist.png",
        args.show,
    )


if __name__ == "__main__":
    main()

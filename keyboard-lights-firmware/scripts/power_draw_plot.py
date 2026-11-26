"""Plots a power_draw_log.py CSV: current over the whole run, and power against level per color.

    uv run --with numpy --with matplotlib python scripts/power_draw_plot.py power_draw.csv

Writes <csv stem>_current.png, <csv stem>_power.png and <csv stem>_brightness.png beside the CSV,
and prints the per-channel fits.

A level's reading is the median of its samples from STEADY_FROM_S into the step onward, so the
PSU's lag and slow update can't mix in the previous level.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

STEADY_FROM_S = 0.5
NUM_LEDS = 88
MIN_PEAK = 11  # hardware.h: lowest value an LED shows steadily
STEPS = 48  # app_conf.h: MASTER_BRIGHTNESS_STEPS

COLORS = ["red", "green", "blue", "white"]
INK = {"red": "#c8323c", "green": "#2e8b57", "blue": "#2f5fd0", "white": "#5b5b66"}
MUTED = "#8a8a94"
GRID = "#e4e4ea"


def load(path: Path):
    with open(path, newline="") as f:
        rows = list(csv.DictReader(f))
    t = np.array([float(r["t_s"]) for r in rows])
    v = np.array([float(r["volts"]) for r in rows])
    a = np.array([float(r["amps"]) for r in rows])
    return rows, t, v, a


def segments(rows, t):
    """(segment, color, t_start, t_end) for each contiguous run of one label."""
    out = []
    start = 0
    for i in range(1, len(rows) + 1):
        if i == len(rows) or (rows[i]["segment"], rows[i]["color"]) != (rows[start]["segment"], rows[start]["color"]):
            out.append((rows[start]["segment"], rows[start]["color"], t[start], t[i - 1]))
            start = i
    return out


def per_level(rows, v, a, segment, color):
    """level -> (median A, median W) over the steady part of each step."""
    acc: dict[int, list[tuple[float, float]]] = {}
    for r, vi, ai in zip(rows, v, a):
        if r["segment"] == segment and r["color"] == color and float(r["t_in_step_s"]) >= STEADY_FROM_S:
            acc.setdefault(int(r["level"]), []).append((ai, vi * ai))
    levels = np.array(sorted(acc))
    amps = np.array([np.median([x[0] for x in acc[l]]) for l in levels])
    watts = np.array([np.median([x[1] for x in acc[l]]) for l in levels])
    return levels, amps, watts


def style(ax):
    ax.grid(True, color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    for s in ("left", "bottom"):
        ax.spines[s].set_color(MUTED)
    ax.tick_params(colors="#444", labelsize=8)


def plot_current(rows, t, a, out: Path):
    fig, ax = plt.subplots(figsize=(15, 4.8), layout="constrained")
    style(ax)
    ax.plot(t, a, color="#33333d", linewidth=0.7)
    segs = segments(rows, t)
    ymax = a.max() * 1.12
    for seg, color, t0, t1 in segs:
        if seg in ("single", "all"):
            ax.axvspan(t0, t1, color=INK[color], alpha=0.07 if seg == "single" else 0.13, linewidth=0)
            name = f"{color}\n{'LED 0' if seg == 'single' else 'all 88'}"
            ax.text((t0 + t1) / 2, ymax * 0.97, name, ha="center", va="top", fontsize=8, color="#333")
        elif seg in ("sync", "baseline"):
            ax.text((t0 + t1) / 2, ymax * 0.97, seg, ha="center", va="top", fontsize=7, color=MUTED, rotation=90)
        ax.axvline(t1, color=MUTED, linewidth=0.8, linestyle="--")
    ax.set_xlim(0, t[-1] + 10)
    ax.set_ylim(0, ymax)
    ax.set_xlabel("time since PSU output on (s)", fontsize=9)
    ax.set_ylabel("current (A)", fontsize=9)
    ax.set_title("Board current at 5 V through the power_draw test (dashed lines: end of each sub-test)", fontsize=11, loc="left")
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_power(rows, v, a, baseline_w, out: Path):
    fig, axes = plt.subplots(2, 4, figsize=(15, 7.5), layout="constrained", sharex=True)
    fits = {}
    for col, color in enumerate(COLORS):
        for row, seg in enumerate(("all", "single")):
            ax = axes[row, col]
            style(ax)
            levels, amps, watts = per_level(rows, v, a, seg, color)
            if seg == "all":
                y, unit = watts, "W"
            else:
                y, unit = (watts - baseline_w) * 1000, "mW above baseline"
            ax.plot(levels, y, "o", markersize=2.5, color=INK[color], alpha=0.75, markeredgewidth=0)
            # least-squares line through the net power (above baseline)
            net_w = watts - baseline_w
            slope, icpt = np.polyfit(levels, net_w, 1)
            fit = slope * np.arange(256) + icpt
            fit_plot = fit + baseline_w if seg == "all" else fit * 1000
            ax.plot(np.arange(256), fit_plot, color="#222", linewidth=1, alpha=0.6)
            per_led_mw = slope * 255 * 1000 / (NUM_LEDS if seg == "all" else 1)
            per_led_ma = per_led_mw / 5.0
            fits[(seg, color)] = (per_led_mw, levels.max())
            note = f"≈ {per_led_ma:.1f} mA / {per_led_mw:.0f} mW per LED at 255"
            if levels.max() < 255:
                note += f"\n(run ended at level {levels.max()}; extrapolated)"
            ax.text(0.03, 0.95, note, transform=ax.transAxes, fontsize=8, va="top", color="#333")
            ax.set_xlim(0, 256)
            ax.set_xticks([0, 64, 128, 192, 255])
            if row == 0:
                ax.set_title(f"{color}", fontsize=11, loc="left", color="#222")
                ax.axhline(baseline_w, color=MUTED, linewidth=0.8, linestyle=":")
            if col == 0:
                ax.set_ylabel(("all 88 LEDs: board P (W)" if seg == "all" else "LED 0 only: ΔP (mW)"), fontsize=9)
            if row == 1:
                ax.set_xlabel("channel level (0–255)", fontsize=9)
    fig.suptitle(f"Power (P = V·I) against level. Dotted: baseline with all LEDs off ({baseline_w * 1000:.0f} mW). Line: least-squares fit.", fontsize=11, x=0.01, ha="left")
    fig.savefig(out, dpi=150)
    plt.close(fig)
    return fits


def cie_lightness_to_luminance(L):
    """CIE 1976 L* (0–100) -> relative luminance Y (0–1)."""
    L = np.asarray(L, dtype=float)
    return np.where(L > 8, ((L + 16) / 116) ** 3, L / 903.3)


def luminance_to_cie_lightness(Y):
    Y = np.asarray(Y, dtype=float)
    return np.where(Y > 0.008856, 116 * np.cbrt(Y) - 16, 903.3 * Y)


def suggested_scales(steps=STEPS, floor=MIN_PEAK):
    """Global brightness per Master Brightness step: even in L* from the floor up to 255."""
    L0 = luminance_to_cie_lightness(floor / 255)
    L = L0 + (100 - L0) * np.arange(steps) / (steps - 1)
    return np.clip(np.round(cie_lightness_to_luminance(L) * 255), floor, 255).astype(int)


def plot_brightness(out: Path):
    x = np.linspace(0, 1, 256)
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5), layout="constrained")
    for ax in (ax1, ax2):
        style(ax)
    ax1.plot(x * 255, x * 255, color=MUTED, linewidth=1.5, linestyle="--", label="linear (what the strip does)")
    ax1.plot(x * 255, cie_lightness_to_luminance(x * 100) * 255, color="#2f5fd0", linewidth=2, label="CIE L* inverse (suggested)")
    ax1.plot(x * 255, x ** 2.2 * 255, color="#c8323c", linewidth=1.5, label="gamma 2.2")
    ax1.plot(x * 255, x ** 2.8 * 255, color="#e0a030", linewidth=1.5, label="gamma 2.8")
    ax1.axhline(MIN_PEAK, color="#222", linewidth=0.8, linestyle=":")
    ax1.text(4, MIN_PEAK + 4, f"MIN_PEAK = {MIN_PEAK}", fontsize=8, color="#333")
    ax1.set_xlabel("set brightness (0–255, perceptual)", fontsize=9)
    ax1.set_ylabel("PWM value sent to the LED (0–255)", fontsize=9)
    ax1.set_title("Set brightness → PWM value", fontsize=11, loc="left")
    ax1.legend(fontsize=8, frameon=False)
    ax1.set_xlim(0, 255)
    ax1.set_ylim(0, 255)

    steps = np.arange(1, STEPS + 1)
    min_scale = (MIN_PEAK * 256 + 254) // 255 - 1
    current = min_scale + (255 - min_scale) * (steps - 1) // (STEPS - 1)
    suggested = suggested_scales()
    for scales, color, name in ((current, MUTED, "current (linear in scale)"), (suggested, "#2f5fd0", "suggested (even in L*)")):
        L = luminance_to_cie_lightness(scales / 255)
        ax2.plot(steps, L, "o-", color=color, markersize=3, linewidth=1.5, label=name)
    ax2.set_xlabel(f"Master Brightness step (1–{STEPS})", fontsize=9)
    ax2.set_ylabel("perceived lightness L* of a full-value channel", fontsize=9)
    ax2.set_title("Master Brightness steps, as the eye sees them", fontsize=11, loc="left")
    ax2.legend(fontsize=8, frameon=False, loc="lower right")
    ax2.set_xlim(0, STEPS + 1)
    ax2.set_ylim(0, 102)
    fig.savefig(out, dpi=150)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()

    rows, t, v, a = load(args.csv)
    base = [vi * ai for r, vi, ai in zip(rows, v, a) if r["segment"] == "baseline" and float(r["t_in_step_s"]) >= 2]
    baseline_w = float(np.median(base))

    stem = args.csv.with_suffix("")
    plot_current(rows, t, a, Path(f"{stem}_current.png"))
    fits = plot_power(rows, v, a, baseline_w, Path(f"{stem}_power.png"))
    plot_brightness(Path(f"{stem}_brightness.png"))

    print(f"baseline: {baseline_w * 1000:.0f} mW ({baseline_w / 5 * 1000:.0f} mA)")
    for (seg, color), (mw, top) in fits.items():
        print(f"{seg:6s} {color:5s}: {mw:6.1f} mW = {mw / 5:5.2f} mA per LED at 255" + (f"  (data to level {top})" if top < 255 else ""))
    print("suggested scale table:", ", ".join(map(str, suggested_scales())))


if __name__ == "__main__":
    main()

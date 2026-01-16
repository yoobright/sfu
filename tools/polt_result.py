import os
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

plt.rcParams.update(
    {
        "axes.titlesize": 50,
        "axes.labelsize": 50,
        "xtick.labelsize": 46,
        "ytick.labelsize": 46,
        "legend.fontsize": 46,
        "axes.linewidth": 4,
    }
)

RESULT_DIR = "result"


def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)


def parse_csv_name(csv_path):
    """
    Expect:
      <FUNC>_dut<DUT>_ref<REF>_result.csv
    """
    base = os.path.basename(csv_path)
    name = base.replace("_result.csv", "")
    parts = name.split("_")

    func = parts[0]
    dut = parts[1].replace("dut", "")
    ref = parts[2].replace("ref", "")
    return func, dut, ref


def setup_symlog_xaxis(ax, xdata):
    ax.set_xscale("log", base=2)

    max_x = np.max(xdata)
    min_x = np.min(xdata[xdata > 0])

    ax.set_xlim(min_x, max_x)

    ax.grid(False)


def plot_compare(df, outdir, title):
    fig, ax = plt.subplots(figsize=(28, 20))

    ax.scatter(df["input"], df["ref"], s=4, label="ref", alpha=0.1)
    ax.scatter(df["input"], df["dut"], s=4, label="dut", alpha=0.9)

    setup_symlog_xaxis(ax, df["input"].values)

    ax.set_xlabel("input (log2 scale)")
    ax.set_ylabel("value")
    ax.set_title(title)

    fig.tight_layout()
    fig.savefig(os.path.join(outdir, "compare.png"), dpi=200)
    plt.close(fig)


def plot_error(df, col, outdir, title, ylabel, fname, ylog=True):
    fig, ax = plt.subplots(figsize=(28, 20))

    ax.scatter(df["input"], df[col], s=4)

    setup_symlog_xaxis(ax, df["input"].values)

    if ylog:
        ax.set_yscale("log")

    ax.set_xlabel("input (log2 scale)")
    ax.set_ylabel(ylabel)
    ax.set_title(title)

    fig.tight_layout()
    fig.savefig(os.path.join(outdir, fname), dpi=400)
    plt.close(fig)


def main():
    csv_files = glob.glob(os.path.join(RESULT_DIR, "*.csv"))

    if not csv_files:
        print("No CSV files found under result/")
        return

    for csv_path in csv_files:
        func, dut, ref = parse_csv_name(csv_path)

        outdir = os.path.join(RESULT_DIR, f"dut{dut}-ref{ref}", func)
        ensure_dir(outdir)

        print(f"[+] Processing {csv_path}")
        print(f"    -> {outdir}")

        df = pd.read_csv(csv_path)

        plot_compare(df, outdir, title=f"{func}: DUT({dut}) vs REF({ref})")

        plot_error(
            df,
            "AbsErr",
            outdir,
            title=f"{func}: Absolute Error",
            ylabel="Absolute Error",
            fname="abs_error.png",
        )

        plot_error(
            df,
            "RelErr",
            outdir,
            title=f"{func}: Relative Error",
            ylabel="Relative Error",
            fname="rel_error.png",
        )

        plot_error(
            df,
            "ULP",
            outdir,
            title=f"{func}: ULP Error",
            ylabel="ULP",
            fname="ulp_error.png",
            ylog=False,
        )

    print("All plots generated.")


if __name__ == "__main__":
    main()

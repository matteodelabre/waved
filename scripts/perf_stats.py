#!/usr/bin/env python3
"""
Generate summary statistics from a waved performance report.

The output is a CSV file, with one row per update mode, containing the
average and standard deviation for the following measurements:

* latency - the delay between when an update is added to the queue and
            when it is taken out of it
* generation - the time it takes to generate a single frame for an update
* generation_per_area - the time it takes to generate a single frame divided
                        by the number of pixels in that frame
* vsync - the time it takes to send a frame to the display
* vsync_per_area - the time it takes to send a frame to the display divided by
                   the number of pixels in that frame
"""
import argparse
import math
import sys
from statistics import mean, stdev
from itertools import groupby, pairwise
from perf import parse_updates_csv

def round_signif_digits(number, digits):
    if number == 0:
        return 0
    else:
        magnitude = int(math.floor(math.log10(abs(number))))
        return round(number, digits - 1 - magnitude)

def series_stats(series):
    return {
        "mean": mean(series),
        "stdev": stdev(series),
    }

def series_quotient_stats(series, quotients):
    return {
        "mean": mean(item / quo for item, quo in zip(series, quotients)),
        "stdev": mean(item / quo for item, quo in zip(series, quotients)),
    }


def generate_stats(updates):
    key = lambda update: update["mode"]
    results = {}

    for mode, group in groupby(sorted(updates, key=key), key):
        latency = []
        generation = []
        vsync = []
        areas = []

        for update in group:
            latency.append(update["dequeue_time"] - update["queue_time"])

            for start, end in pairwise(update["generate_times"]):
                generation.append(end - start)

            for start, end in pairwise(update["vsync_times"]):
                vsync.append(end - start)

            areas.append(update["width"] * update["height"])

        results[mode] = {
            "latency": series_stats(latency),
            "generation": series_stats(generation),
            "generation_per_area": series_quotient_stats(generation, areas),
            "vsync": series_stats(vsync),
            "vsync_per_area": series_quotient_stats(vsync, areas),
        }

    return results


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "input", type=str, nargs='?',
        help="path to the performance report data file (default: read from \
standard input)"
    )
    parser.add_argument(
        "output", type=str, nargs='?',
        help="where to write the resulting CSV file (default: write to \
standard output)"
    )

    args = parser.parse_args()
    in_file = open(args.input, "r") if args.input is not None else sys.stdin
    out_file = open(args.output, "w") if args.output is not None else sys.stdout

    updates = parse_updates_csv(in_file)
    stats = generate_stats(updates)

    print("mode,", end="", file=out_file)
    print(",".join(
        f"{kind}_{stat}"
        for kind, values in stats["0"].items()
        for stat in values.keys()
    ), file=out_file)

    for mode, data in stats.items():
        print(f"{mode},", end="", file=out_file)
        print(",".join(
            str(round_signif_digits(value, 6))
            for values in data.values()
            for value in values.values()
        ), file=out_file)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Generate a plot of update times from a waved performance report.

In the generated plot, each row represents an update and time flows on the
horizontal axis from left to right. The following symbols can be seen
on each row:

* circles marking the times where an update is enqueued, dequeued and finalized
* a green rectangle that spans the frame generation step,
* a blue rectangle that spans the vsync step.
"""
import sys
import argparse
from perf.perf import parse_updates_csv, generate_report


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
        help="where to write the resulting plot (default: write to standard \
output)"
    )

    args = parser.parse_args()

    in_file = open(args.input, "r") if args.input is not None else sys.stdin
    out_file = open(args.output, "w") if args.output is not None else sys.stdout

    updates = parse_updates_csv(in_file)
    out_file.write(generate_report(updates))


if __name__ == "__main__":
    main()

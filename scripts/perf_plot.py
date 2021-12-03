#!/usr/bin/env python3
"""
Generate a plot of update times from a waved performance report.

In the generated plot, each row represents an update and time flows on the
horizontal axis from left to right. The following symbols can be seen
on each row:

* a diamond shape marking the time of insertion of the update into the queue,
* a red rectangle that spans the update pre-processing step,
* a green rectangle that spans the frame generation step,
* a blue rectangle that spans the vsync step.
"""
import csv
import sys
import argparse


TIME_MARGIN = 500_000
TIME_UNIT_WIDTH = 0.000_1
TIME_TICK_SPACE = 1_000_000
UPDATE_ROW_HEIGHT = 10
UPDATE_LABEL_SPACING = 5


def parse_updates_csv(in_file):
    reader = csv.DictReader(in_file, delimiter=",")
    updates = list(reader)

    for update in updates:
        update["width"] = int(update["width"])
        update["height"] = int(update["height"])
        update["queue_time"] = int(update["queue_time"])
        update["dequeue_time"] = int(update["dequeue_time"])
        update["generate_times"] = \
            list(map(int, update["generate_times"].split(":")))
        update["vsync_times"] = \
            list(map(int, update["vsync_times"].split(":")))
        update["start"] = update["queue_time"]
        update["end"] = update["vsync_times"][-1]

    return updates


def draw_updates(updates, out):
    start_time = min(update["start"] for update in updates)
    start_time -= TIME_MARGIN
    end_time = max(update["end"] for update in updates)
    end_time += TIME_MARGIN

    delta_to_x = lambda t1, t2: TIME_UNIT_WIDTH * (t2 - t1)
    time_to_x = lambda time: delta_to_x(start_time, time)

    print(f"""<svg version="1.1" xmlns="http://www.w3.org/2000/svg" \
width="{delta_to_x(start_time, end_time)}" \
height="{len(updates) * UPDATE_ROW_HEIGHT}">
<style type="text/css"><![CDATA[
    .stripe-even {{
        fill: #f0f0f0;
    }}
    .stripe-odd {{
        fill: #ddd;
    }}
    .time-tick {{
        stroke: #AAA;
        stroke-width: 1;
    }}
    .update-label {{
        fill: #555;
        text-anchor: end;
        dominant-baseline: middle;
        font-size: {0.8 * UPDATE_ROW_HEIGHT}px;
        font-family: "Liberation Sans", sans-serif;
    }}
    .update-label-end {{
        text-anchor: start;
    }}
    .update-queue {{
        fill: #333;
    }}
    .update-prepare {{
        fill: #dd5555;
    }}
    .update-generate-even {{
        fill: #55dd55;
    }}
    .update-generate-odd {{
        fill: #22aa22;
    }}
    .update-vsync-even {{
        fill: #5555dd;
    }}
    .update-vsync-odd {{
        fill: #2222aa;
    }}
]]></style>""", file=out)

    # Add alternating row stripes
    for y in range(len(updates)):
        print(f"""<rect x="0" y="{y * UPDATE_ROW_HEIGHT}" \
width="{delta_to_x(start_time, end_time)}" \
height="{UPDATE_ROW_HEIGHT}" \
class="stripe-{"even" if y % 2 == 0 else "odd"}" />""", file=out)

    # Add time ticks
    for time in range(start_time, end_time, TIME_TICK_SPACE):
        print(f"""<line x1="{time_to_x(time)}" x2="{time_to_x(time)}" \
y1="0" y2="{len(updates) * UPDATE_ROW_HEIGHT}" class="time-tick" />""", file=out)

    # Add update rects
    for y, update in enumerate(updates):
        # Update ID labels on both ends
        print(f"""<text x="{time_to_x(update["start"]) -
UPDATE_LABEL_SPACING}" y="{(y + 0.55) * UPDATE_ROW_HEIGHT}" \
class="update-label">\
#{update["id"]} \
(mode {update["mode"]})\
</text>""", file=out)
        print(f"""<text x="{time_to_x(update["end"]) +
UPDATE_LABEL_SPACING}" y="{(y + 0.55) * UPDATE_ROW_HEIGHT}" \
class="update-label update-label-end">\
{round((update["dequeue_time"] - update["start"]) / 1_000)} ms + \
{round((update["end"] - update["dequeue_time"]) / 1_000)} ms</text>""", file=out)

        # Add rectangle for the preparation time
        print(f"""<rect x="{time_to_x(update["dequeue_time"])}" \
y="{y * UPDATE_ROW_HEIGHT}" \
width="{delta_to_x(update["dequeue_time"], update["generate_times"][0])}" \
height="{UPDATE_ROW_HEIGHT}" \
class="update-prepare"><title>\
Update #{update["id"]} — Prepare — \
{round((update["generate_times"][0] - update["dequeue_time"]) / 1_000)} ms\
</title></rect>""", file=out)

        # Add one rectangle per frame generation time
        for x, (start, end) in enumerate(zip(
            update["generate_times"][:-1],
            update["generate_times"][1:]
        )):
            print(f"""<rect x="{time_to_x(start)}" \
y="{y * UPDATE_ROW_HEIGHT}" \
width="{delta_to_x(start, end)}" \
height="{UPDATE_ROW_HEIGHT}" \
class="update-generate-{"even" if x % 2 == 0 else "odd"}"><title>\
Update #{update["id"]} — Generate frame #{x} — \
{round((end - start) / 1_000)} ms\
</title></rect>""", file=out)

        # Add one rectangle per frame vsync time
        for x, (start, end) in enumerate(zip(
            update["vsync_times"][:-1],
            update["vsync_times"][1:]
        )):
            print(f"""<rect x="{time_to_x(start)}" \
y="{y * UPDATE_ROW_HEIGHT}" \
width="{delta_to_x(start, end)}" \
height="{UPDATE_ROW_HEIGHT}" \
class="update-vsync-{"even" if x % 2 == 0 else "odd"}"><title>\
Update #{update["id"]} — Vsync frame #{x} — \
{round((end - start) / 1_000)} ms\
</title></rect>""", file=out)

        # Add diamond for time where the update was queued
        print(f"""<rect \
x="{time_to_x(update["queue_time"]) - 0.15 * UPDATE_ROW_HEIGHT}" \
y="{(y + 0.35) * UPDATE_ROW_HEIGHT}" width="{0.3 * UPDATE_ROW_HEIGHT}" \
height="{0.3 * UPDATE_ROW_HEIGHT}" class="update-queue" \
transform="rotate(45 {time_to_x(update["queue_time"])} \
{(y + 0.5) * UPDATE_ROW_HEIGHT})"><title>\
Update #{update["id"]} — Queue time\
</title></rect>""", file=out)

    print("</svg>", file=out)


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
    draw_updates(updates, out_file)


if __name__ == "__main__":
    main()

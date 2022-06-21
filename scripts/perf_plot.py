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
import sys
import argparse
from perf import parse_updates_csv


TIME_MARGIN = 500_000
TIME_UNIT_WIDTH = 0.000_1
TIME_TICK_SPACE = 1_000_000
UPDATE_ROW_HEIGHT = 10
UPDATE_LABEL_SPACING = 5


def draw_updates(updates, out):
    start_time = min(
        time
        for update in updates
        for time in update["enqueue_times"]
    )
    end_time = max(
        time
        for update in updates
        for time in update["vsync_end_times"]
    )

    delta_to_text = lambda t1, t2: f"{(t2 - t1) / 1_000} ms"
    delta_to_x = lambda t1, t2: TIME_UNIT_WIDTH * (t2 - t1)
    time_to_x = lambda time: delta_to_x(start_time, time)

    timeline_width = delta_to_x(start_time, end_time)

    print(f"""\
<!doctype html>
<html lang="en">
    <head>
        <title>Waved Performance Report</title>
        <style>
            body, html, .timeline {{
                width: 100%;
                height: 100%;
                margin: 0;
                padding: 0;
            }}

            *, *::before, *::after {{
                box-sizing: border-box;
            }}

            .timeline {{
                position: relative;
                overflow: auto;
            }}

            .timeline-tick {{
                position: absolute;
                z-index: 1;
                top: 0;
                height: 100%;
                width: 2px;
                background: #bbb;
            }}

            .timeline-row {{
                position: relative;
                height: 20px;
                width: 100%;
            }}

            .timeline-row:nth-child(2n) {{
                background: #f0f0f0;
            }}

            .timeline-row:nth-child(2n + 1) {{
                background: #ddd;
            }}

            .update-queue-item {{
                --height: 2px;
                position: absolute;
                z-index: 2;
                background: #333;
                height: var(--height);
                top: calc(50% - var(--height) / 2);
            }}

            .update-queue-item::before, .update-queue-item::after {{
                content: "";
                display: block;
                position: absolute;
                --size: 10px;
                width: var(--size);
                height: var(--size);
                border-radius: var(--size);
                border: 2px solid #333;
                top: calc(var(--height) / 2 - var(--size) / 2);
            }}

            .update-queue-item::before {{
                left: calc(-1 * var(--size) / 2);
                background: #ddd;
                z-index: 3;
            }}

            .update-queue-item::after {{
                right: calc(-1 * var(--size) / 2);
                background: #888;
                z-index: 2;
            }}

            .update-generate-item, .update-vsync-item {{
                height: 50%;
                top: 25%;
                position: absolute;
                z-index: 2;
            }}

            .update-generate-item:nth-child(2n) {{
                background: #22aa22;
            }}

            .update-generate-item:nth-child(2n + 1) {{
                background: #55dd55;
            }}

            .update-vsync-item:nth-child(2n) {{
                background: #5555dd;
            }}

            .update-vsync-item:nth-child(2n + 1) {{
                background: #2222aa;
            }}
        </style>
    </head>
    <body>
        <div class="timeline" style="width: {timeline_width}px">""", file=out)

    for y, update in enumerate(updates):
        # Show enqueue and dequeue times for all updates merged in the group
        print("""\
            <div class="timeline-row">""", file=out)

        for upd_id, enqueue, dequeue in zip(
            update["id"],
            update["enqueue_times"],
            update["dequeue_times"],
        ):
            left = time_to_x(enqueue)
            width = delta_to_x(enqueue, dequeue)
            label = f"Update #{upd_id}: {delta_to_text(enqueue, dequeue)} in queue"
            print(f"""\
                <div class="update-queue-item" title="{label}" style="left: {left}px; width: {width}px;"></div>""", file=out)

        # Show frame generation times
        print("""\
            </div>
            <div class="timeline-row">""", file=out)

        for frame_id, (start, end) in enumerate(zip(
            update["generate_start_times"],
            update["generate_end_times"],
        )):
            left = time_to_x(start)
            width = delta_to_x(start, end)
            label = f"Generation of frame #{frame_id}: {delta_to_text(start, end)}"
            print(f"""\
                <div class="update-generate-item" title="{label}" style="left: {left}px; width: {width}px;"></div>""", file=out)

        # Show frame vsync times
        print("""\
            </div>
            <div class="timeline-row">""", file=out)

        for frame_id, (start, end) in enumerate(zip(
            update["vsync_start_times"],
            update["vsync_end_times"],
        )):
            left = time_to_x(start)
            width = delta_to_x(start, end)
            label = f"Vsync of frame #{frame_id}: {delta_to_text(start, end)}"
            print(f"""\
                <div class="update-vsync-item" title="{label}" style="left: {left}px; width: {width}px;"></div>""", file=out)

        print("""\
            </div>""", file=out)

    # Add time ticks
    for time in range(start_time, end_time, TIME_TICK_SPACE):
        print(f"""\
            <div class="timeline-tick" style="left: {time_to_x(time)}px"></div>""", file=out)

    print("""\
        </div>
    </body>
</html>""", file=out)


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

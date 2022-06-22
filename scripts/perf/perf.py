import csv
import textwrap
from os import path


MODULE_DIR = path.dirname(path.abspath(__file__))

with open(path.join(MODULE_DIR, "style.css"), "r") as file:
    REPORT_STYLESHEET = file.read()


with open(path.join(MODULE_DIR, "script.js"), "r") as file:
    REPORT_SCRIPT = file.read()


def parse_list(map_fun, field):
    filtered = filter(lambda x: x, field.split(":"))
    return list(map(map_fun, filtered))


def parse_updates_csv(in_file):
    reader = csv.DictReader(in_file, delimiter=",")
    updates = list(reader)

    for update in updates:
        update["id"] = parse_list(str, update["id"])
        update["immediate"] = bool(int(update["immediate"]))
        update["width"] = int(update["width"])
        update["height"] = int(update["height"])
        update["enqueue_times"] = parse_list(int, update["enqueue_times"])
        update["dequeue_times"] = parse_list(int, update["dequeue_times"])
        update["generate_start_times"] = \
            parse_list(int, update["generate_start_times"])
        update["generate_end_times"] = \
            parse_list(int, update["generate_end_times"])
        update["vsync_start_times"] = \
            parse_list(int, update["vsync_start_times"])
        update["vsync_end_times"] = \
            parse_list(int, update["vsync_end_times"])

    return updates


TIME_TICK_SPACE = 1_000_000
INITIAL_ZOOM = 0.000_1


def generate_report(updates):
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
    delta_to_x = lambda t1, t2: INITIAL_ZOOM * (t2 - t1)
    time_to_x = lambda time: delta_to_x(start_time, time)

    timeline_width = delta_to_x(start_time, end_time)
    result = textwrap.dedent(
        """\
        <!doctype html>
        <html lang="en">
        <head>
        <title>Waved Performance Report</title>
        <style>
        """
    ) + REPORT_STYLESHEET + textwrap.dedent(
        f"""\
        </style>
        </head>
        <body>
        <p class="controls">
        Zoom: <input id="zoom" type="range" value="{INITIAL_ZOOM}"
                     min="0.00001" max="0.001" step="0.000005">
        </p>
        <div class="timeline-container">
        <div class="timeline" data-start="{start_time}" data-end="{end_time}" \
style="width: {timeline_width}px">
        """
    )

    for y, update in enumerate(updates):
        # Show enqueue and dequeue times for all updates merged in the group
        result += '<div class="timeline-row">\n'

        for upd_id, enqueue, dequeue in zip(
            update["id"],
            update["enqueue_times"],
            update["dequeue_times"],
        ):
            label = f"Update #{upd_id}: {delta_to_text(enqueue, dequeue)} in queue"
            result += (
                f'<div class="update-queue-item" title="{label}" '
                f'data-start="{enqueue}" data-end="{dequeue}" '
                f'style="left: {time_to_x(enqueue)}px; '
                f'width: {delta_to_x(enqueue, dequeue)}px;"></div>\n'
            )

        # Show frame generation times
        result += '</div>\n<div class="timeline-row">\n'

        for frame_id, (start, end) in enumerate(zip(
            update["generate_start_times"],
            update["generate_end_times"],
        )):
            label = f"Generation of frame #{frame_id}: {delta_to_text(start, end)}"
            result += (
                f'<div class="update-generate-item" title="{label}" '
                f'data-start="{start}" data-end="{end}" '
                f'style="left: {time_to_x(start)}px; '
                f'width: {delta_to_x(start, end)}px;"></div>\n'
            )

        # Show frame vsync times
        result += '</div>\n<div class="timeline-row">\n'

        for frame_id, (start, end) in enumerate(zip(
            update["vsync_start_times"],
            update["vsync_end_times"],
        )):
            label = f"Vsync of frame #{frame_id}: {delta_to_text(start, end)}"
            result += (
                f'<div class="update-vsync-item" title="{label}" '
                f'data-start="{start}" data-end="{end}" '
                f'style="left: {time_to_x(start)}px; '
                f'width: {delta_to_x(start, end)}px;"></div>\n'
            )

        result += '</div>\n'

    # Add time ticks
    for time in range(start_time, end_time, TIME_TICK_SPACE):
        result += (
            f'<div class="timeline-tick" '
            f'data-start="{time}" style="left: {time_to_x(time)}px;">'
            f'</div>\n'
        )

    result += textwrap.dedent(
        """\
        </div>
        </div>
        <script>
        """
    ) + REPORT_SCRIPT + textwrap.dedent(
        """\
        </script>
        </body>
        </html>
        """
    )
    return result

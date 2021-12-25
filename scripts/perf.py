import csv


def parse_updates_csv(in_file):
    reader = csv.DictReader(in_file, delimiter=",")
    updates = list(reader)

    for update in updates:
        update["width"] = int(update["width"])
        update["height"] = int(update["height"])
        update["queue_time"] = int(update["queue_time"])
        update["dequeue_time"] = int(update["dequeue_time"])
        update["generate_times"] = \
            list(map(int, update["generate_times"].split(":"))) \
            if update["generate_times"] else []
        update["vsync_times"] = \
            list(map(int, update["vsync_times"].split(":"))) \
            if update["vsync_times"] else []
        update["start"] = update["queue_time"]
        update["end"] = update["vsync_times"][-1] \
            if update["vsync_times"] else update["generate_times"][-1]

    return updates

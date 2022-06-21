import csv


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

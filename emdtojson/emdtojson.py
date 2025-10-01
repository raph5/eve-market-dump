import struct
import sys
import json

def unpack(format, file):
    size = struct.calcsize(format)
    raw = file.read(size)
    return struct.unpack(format, raw)

def unpack_string(file):
    len, = unpack("!Q", file)
    return file.read(len).decode("utf-8")

def unpack_loc_table(file):
    loc_table = []
    loc_table_len, = unpack("!Q", file)
    for _ in range(loc_table_len):
        id, type_id, owner_id, system_id, security = unpack("!QQQQf", file)
        name = unpack_string(file)
        loc_table.append({
            "id": id,
            "type_id": type_id,
            "owner_id": owner_id,
            "system_id": system_id,
            "security": security,
            "name": name,
        })
    return loc_table

def unpack_order_table(file):
    order_table = []
    order_table_len, = unpack("!Q", file)
    for _ in range(order_table_len):
        is_buy_order, _range, duration, issued, min_volume, volume_remain, volume_total, location_id, system_id, type_id, region_id, order_id, price = unpack("!BbIQQQQQQQQQd", file)
        order_table.append({
            "is_buy_order": is_buy_order,
            "duration": duration,
            "range": _range,
            "issued": issued,
            "min_volume": min_volume,
            "volume_remain": volume_remain,
            "volume_total": volume_total,
            "location_id": location_id,
            "system_id": system_id,
            "type_id": type_id,
            "region_id": region_id,
            "order_id": order_id,
            "price": price,
        })
    return order_table

def unpack_history_day(file):
    stats = []
    year, day, stats_len = unpack("!HHQ", file)
    for _ in range (stats_len):
        region_id, type_id, average, highest, lowest, order_count, volume = unpack("!QQdddQQ", file)
        stats.append({
            "region_id": region_id,
            "type_id": type_id,
            "average": average,
            "highest": highest,
            "lowest": lowest,
            "order_count": order_count,
            "volume": volume,
        })
    return { "year": year, "day": day, "stats": stats }

dump_json = {}

version, _type, checksum, snapshot, expiration, ascii_art = unpack("!BBIQQ32s", sys.stdin.buffer)
dump_json["version"] = version
dump_json["type"] = _type
dump_json["checksum"] = checksum
dump_json["snapshot"] = snapshot
dump_json["expiration"] = expiration 
dump_json["ascii_art"] = ascii_art.decode("utf-8")

# TODO: check checksum

if _type == 0:  # locations
    dump_json["data"] = unpack_loc_table(sys.stdin.buffer)
elif _type == 1:  # orders
    dump_json["data"] = unpack_order_table(sys.stdin.buffer)
elif _type == 2:  # histories
    dump_json["data"] = unpack_history_day(sys.stdin.buffer)
else:
    print("unknown dump type")

json.dump(dump_json, sys.stdout)

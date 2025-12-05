import struct
import sys
import json
import zlib

def unpack(format, file, checksum):
    size = struct.calcsize(format)
    raw = file.read(size)
    checksum[0] = zlib.crc32(raw, checksum[0])
    return struct.unpack(format, raw)

def unpack_string(file, checksum):
    len, = unpack("!Q", file, checksum)
    return file.read(len).decode("utf-8")

def unpack_loc_table(file, checksum):
    loc_table = []
    loc_table_len, = unpack("!Q", file, checksum)
    for _ in range(loc_table_len):
        id, type_id, owner_id, system_id, security = unpack("!QQQQf", file, checksum)
        name = unpack_string(file, checksum)
        loc_table.append({
            "id": id,
            "type_id": type_id,
            "owner_id": owner_id,
            "system_id": system_id,
            "security": security,
            "name": name,
        })
    return loc_table

def unpack_order_table(file, checksum):
    order_table = []
    order_table_len, = unpack("!Q", file, checksum)
    for _ in range(order_table_len):
        is_buy_order, _range, duration, issued, min_volume, volume_remain, volume_total, location_id, system_id, type_id, region_id, order_id, price = unpack("!BbIQQQQQQQQQd", file, checksum)
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

def unpack_history_day(file, checksum):
    stats = []
    year, day, len = unpack("!HHQ", file, checksum)
    for _ in range (len):
        region_id, type_id, average, highest, lowest, order_count, volume = unpack("!QQdddQQ", file, checksum)
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

version, _type, checksum, expiration, ascii_art = unpack("!BBIQ32s", sys.stdin.buffer, [0])
dump_json["version"] = version
dump_json["type"] = _type
dump_json["checksum"] = checksum
dump_json["expiration"] = expiration
dump_json["ascii_art"] = ascii_art.decode("utf-8")

checksum = [0]
if _type == 0:  # locations
    dump_json["data"] = unpack_loc_table(sys.stdin.buffer, checksum)
elif _type == 1:  # orders
    dump_json["data"] = unpack_order_table(sys.stdin.buffer, checksum)
elif _type == 2:  # histories
    dump_json["data"] = unpack_history_day(sys.stdin.buffer, checksum)
else:
    print("unknown dump type", file=sys.stderr)

if checksum[0] != dump_json["checksum"]:
    print(r"/!\ checksum does not match", file=sys.stderr)

json.dump(dump_json, sys.stdout)

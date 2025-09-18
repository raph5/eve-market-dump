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

dump_json = {}

version, checksum, date, ascii_art = unpack("!BIQ32s", sys.stdin.buffer)
dump_json["version"] = version
dump_json["checksum"] = checksum
dump_json["date"] = date
dump_json["ascii_art"] = ascii_art.decode("utf-8")

loc_table = []
loc_table_len, = unpack("!Q", sys.stdin.buffer)
for _ in range(loc_table_len):
    id, type_id, owner_id, system_id, security = unpack("!QQQQf", sys.stdin.buffer)
    name = unpack_string(sys.stdin.buffer)
    loc_table.append({
        "id": id,
        "type_id": type_id,
        "owner_id": owner_id,
        "system_id": system_id,
        "security": security,
        "name": name,
    })
dump_json["data"] = loc_table

json.dump(dump_json, sys.stdout)

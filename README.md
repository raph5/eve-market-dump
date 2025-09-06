
# EVE Market Dump

*EVE Market Dump* (EMD) is an angry little web client that is screaming at the
ESI to send it the entier market state so that other developpers don't have to.

EMD distributes a copy *all* market orders every 10 minutes and a copy of
*active markets* history every 24 hours. This data is encoded as an array of
structs then gziped and then sent over HTTP following a subscription system.

## Documentation

### Data Format

The market data is packed into *dumps*. Each dump is a gziped array of structs
with a little header at the top. The first byte of this buffer (once
descompessed) is the version. The following is the documentation of version 1.

Three type of dumps are available: the *orders dump*, the *histories dump* and
the *locations dump*. They all share the same header:

| Length | Type | Description | Example |
| ------ | ---- | ----------- | ------- |
| 1 byte | uint8 | Version of the dump | 1 |
| 1 byte | uint8 | Dump type (0 = orders, 1 = histories, 2 = locations) | 0 |

```c
typedef struct {
    uint8_t version;  // version of the dump
    uint8_t type;  // dump type (0 = orders, 1 = histories, 2 = locations)
} DumpHeader;
```

All ints and floats are sent in network endianness (big-endian). Be also aware
that c structs may have padding. Therefore, you can't directly bit cast raw
data of the dump to their corresponding structs (which would also break strict
aliasing btw).

**Orders Dump:**

Directly following is small header specific to the orders dump:

| Length | Type | Description | Example |
| ------ | ---- | ----------- | ------- |
| 4 bytes | uint64 | Number of buy orders | 123 |
| 4 bytes | uint64 | Number of sell orders | 123 |

```c
typedef struct {
    uint64_t buy_orders_len;
    uint64_t sell_orders_len;
}
```

Then is a array of structs of length `buy_orders_len`.

| Length | Type | Description | Example |
| ------ | ---- | ----------- | ------- |
| 1 byte | uint8 | The number of day the order was created to last | 90 |
| 1 byte | int8 | Order range in jumps (0 = region, -1 = solarsystem, -2 = station) | -2 |
| 4 bytes | uint64 | The epoch date of the order issue | 1754152686 |
| 4 bytes | uint64 | For buy orders, the minimum quantity that will be accepted | 1 |
| 4 bytes | uint64 | Quantity of items still required or offered | 1023 |
| 4 bytes | uint64 | Quantity of items required or offered at time order was issued | 1024 |
| 4 bytes | uint64 | The `locationsId` of the location where the order was issued | 60003760 |
| 4 bytes | uint64 | The `systemId` of the system where the order was issued | 30000142 |
| 4 bytes | uint64 | The `typeId` of the item transacted in this order | 11393 |
| 4 bytes | uint64 | The `orderId` | 1234567 |
| 4 bytes | float64 | The cost per unit for this order | 40000000 |

```c
typedef struct {
    uint8_t duration;
    int8_t range;
    uint64_t issued;
    uint64_t min_volume;
    uint64_t volume_remain;
    uint64_t volume_total;
    uint64_t location_id;
    uint64_t system_id;
    uint64_t type_id;
    uint64_t order_id;
    double price;
} BuyOrder;
```

Then is a array of structs of length `sell_orders_len`.

| Length | Type | Description | Example |
| ------ | ---- | ----------- | ------- |
| 1 byte | uint8 | The number of day the order was created to last | 90 |
| 4 bytes | uint64 | The epoch date of the order issue | 1754152686 |
| 4 bytes | uint64 | Quantity of items still required or offered | 1023 |
| 4 bytes | uint64 | Quantity of items required or offered at time order was issued | 1024 |
| 4 bytes | uint64 | The `locationsId` of the location where the order was issued | 60003760 |
| 4 bytes | uint64 | The `systemId` of the system where the order was issued | 30000142 |
| 4 bytes | uint64 | The `typeId` of the item transacted in this order | 11393 |
| 4 bytes | uint64 | The `orderId` | 1234567 |
| 4 bytes | float64 | The cost per unit for this order | 40000000 |

```c
typedef struct {
    uint8_t duration;
    uint64_t issued;
    uint64_t volume_remain;
    uint64_t volume_total;
    uint64_t location_id;
    uint64_t system_id;
    uint64_t type_id;
    uint64_t order_id;
    double price;
} SellOrder;
```

**Histories Dump:**

Not implemented yet

**Locations Dump:**

Not implemented yet

### Active Markers

Due to the way the ESI is setup, it's implossible to pull the hole market
history data in 24 hours. To remedy this problem we tag some market as active
and only pull the history of those markets. Here a market mean a couple
(typeId, marketId). To be taged as active, a market must contain at least one
order. The active market tags are reset and recomputed every days.

# Notes

This codebase aims for POSIX (IEEE Std 1003.1-2017) compliance.

## Dependecies

curl 8.15.0 (available in deps folder)
jansson 2.13.1 (available in deps folder)
zlib 1.3.1 (available in deps folder)
POSIX and libc

For convenience I am also using the __thread clang/gcc extension which requires
you to build this project with clang or gcc.

TODO: Include copyright notice

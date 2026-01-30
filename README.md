
# EVE Market Dump

*EVE Market Dump* (EMD) is an angry little web client that is screaming at the
ESI to send it the entier market state so that other developpers don't have to.

EMD distributes a copy *all* market orders every 10 minutes and a copy of
*active markets* history every 24 hours. This data is encoded as an array of
structs then gziped and then sent over HTTP following a subscription system.

package evemarketdump

import (
	"context"
	"fmt"
	"time"
)

type Order struct {
	IsBuyOrder   bool
	Range        int8 // -2 = station, -1 = system, 0 = region, 1 = 1 jump ...
	Duration     uint32
	Issued       uint64
	MinVolume    uint64
	VolumeRemain uint64
	VolumeTotal  uint64
	LocationId   uint64
	SystemId     uint64
	TypeId       uint64
	RegionId     uint64
	OrderId      uint64
	Price        float64
}

type esiOrder struct {
	Duration     uint32  `json:"duration"`
	IsBuyOrder   bool    `json:"is_buy_order"`
	Issued       string  `json:"issued"`
	LocationId   uint64  `json:"location_id"`
	MinVolume    uint64  `json:"min_volume"`
	OrderId      uint64  `json:"order_id"`
	Price        float64 `json:"price"`
	Range        string  `json:"range"`
	SystemId     uint64  `json:"system_id"`
	TypeId       uint64  `json:"type_id"`
	VolumeRemain uint64  `json:"volume_remain"`
	VolumeTotal  uint64  `json:"volume_total"`
}

func DownloadOrderDump(ctx context.Context) ([]Order, error) {
	orders := make([]Order, 0, 4000)

	for _, regionId := range Regions {
		var pages int

		for p := 1; p <= pages || p == 1; p++ {
			if err := ctx.Err(); err != nil {
				return nil, err
			}

			uri := fmt.Sprintf("/markets/%d/orders?order_type=all&page=%d", regionId, p)
			response, err := esiFetch[[]esiOrder](ctx, "GET", uri, false, nil, 5)
			if err != nil {
				return nil, fmt.Errorf("fetching page %d from region %d: %w", p, regionId, err)
			}
			esiOrders := *response.data
			pages = response.pages

			for i := range esiOrders {
				var order Order
				err = esiOrderToOrder(&esiOrders[i], &order, regionId)
				if err != nil {
					return nil, fmt.Errorf("invliad order: %w", err)
				}
				orders = append(orders, order)
			}
		}
	}

	return orders, nil
}

func esiOrderToOrder(esiOrder *esiOrder, order *Order, regionId uint64) error {
	issued, err := time.Parse(esiTimeLayout, esiOrder.Issued)
	if err != nil {
		return fmt.Errorf("invalid order date \"%v\": %w", esiOrder.Issued, err)
	}

	var rangeCode int8
	switch esiOrder.Range {
	case "station":
		rangeCode = -2
	case "solarsystem":
		rangeCode = -1
	case "region":
		rangeCode = 0
	case "1":
		rangeCode = 1
	case "2":
		rangeCode = 2
	case "3":
		rangeCode = 3
	case "4":
		rangeCode = 4
	case "5":
		rangeCode = 5
	case "10":
		rangeCode = 10
	case "20":
		rangeCode = 20
	case "30":
		rangeCode = 30
	case "40":
		rangeCode = 40
	default:
		return fmt.Errorf("invalid order range \"%v\"", esiOrder.Range)
	}

	*order = Order{
		IsBuyOrder:   esiOrder.IsBuyOrder,
		Range:        rangeCode,
		Duration:     esiOrder.Duration,
		Issued:       uint64(issued.Unix()),
		MinVolume:    esiOrder.MinVolume,
		VolumeRemain: esiOrder.VolumeRemain,
		VolumeTotal:  esiOrder.VolumeTotal,
		LocationId:   esiOrder.LocationId,
		SystemId:     esiOrder.SystemId,
		TypeId:       esiOrder.TypeId,
		RegionId:     regionId,
		OrderId:      esiOrder.OrderId,
		Price:        esiOrder.Price,
	}
	return nil
}

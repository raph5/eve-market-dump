package evemarketdump

import (
	"context"
	"os"
	"time"
)

type ApiSecrets struct {
	ssoClientId     string
	ssoClientSecret string
	ssoRefreshToken string
}

func EnableLogging(ctx context.Context) context.Context {
	return context.WithValue(ctx, "evemarketdump_logging_enabled", true)
}

type Location struct {
	Id       uint64
	TypeId   uint64 // station type id
	OwnerId  uint64 // station corporation id
	SystemId uint64
	Security float32
	Name     string
}

func DownloadLocationDump(ctx context.Context, unknown_location []uint64, secrets ApiSecrets) ([]Location, []uint64, error) {
	return nil, nil, nil
}

type Order struct {
	IsBuyOrder   bool
	Range        int8
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

func DownloadOrderDump(ctx context.Context) ([]Order, error) {
	return nil, nil
}

type HistoryDay struct {
	RegionId   uint64
	TypeId     uint64
	Average    float64
	Highest    float64
	Lowest     float64
	OrderCount uint64
	Volume     uint64
}

type FullHistoryDump struct {
	file os.File
	days []time.Time
}

func DownloadFullHistoryDump(ctx context.Context) (FullHistoryDump, error) {
	return FullHistoryDump{}, nil
}

func DownloadIncrementalHistoryDump(ctx context.Context, day time.Time) ([]HistoryDay, error) {
	return nil, nil
}

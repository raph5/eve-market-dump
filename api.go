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

type loggingKeyType struct{}

var loggingKey = loggingKeyType{}

func EnableLogging(ctx context.Context) context.Context {
	return context.WithValue(ctx, loggingKey, true)
}

func isLoggingEnabled(ctx context.Context) bool {
	v, ok := ctx.Value(loggingKey).(bool)
	return ok && v
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

type HistoryMarket struct {
	RegionId uint64
	TypeId   uint64
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
	filePath string
	Days     []time.Time
}

func (d FullHistoryDump) GetHistoryOfDay(day time.Time) ([]HistoryDay, error) {
	return nil, nil
}

func (d FullHistoryDump) Close() error {
	return os.Remove(d.filePath)
}

// DownloadFullHistoryDump will download the full market history available for
// a slice for markets. This history data represent multiple bigabyte of data.
// To avoid taking as much ram as web browser this data will written to disk
// and DownloadFullHistoryDump will return a handle to this data. You can then
// request history data for a specific day by calling the GetHistoryOfDay.
// Be carefull to defer the call to Close to remove the history data from disk
// when the program closes.
//
// By default, the history data is saved to a file in /tmp but you can specifiy
// a path yourself by passing a string to dumpFilePath. Otherwise set
// dumpFilePath to nil
func DownloadFullHistoryDump(
	ctx context.Context,
	markets []HistoryMarket,
	dumpFilePath *string,
) (FullHistoryDump, error) {
	return FullHistoryDump{}, nil
}

func DownloadIncrementalHistoryDump(
	ctx context.Context,
	markets []HistoryMarket,
	day time.Time,
) ([]HistoryDay, error) {
	return nil, nil
}

package evemarketdump

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"slices"
	"sort"
	"sync"
	"time"
)

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

type snapHistoryDay struct {
	Date       uint64
	RegionId   uint64
	TypeId     uint64
	Average    float64
	Highest    float64
	Lowest     float64
	OrderCount uint64
	Volume     uint64
}

type esiHistoryDay struct {
	Average    float64 `json:"average"`
	Date       string  `json:"date"`
	Highest    float64 `json:"highest"`
	Lowest     float64 `json:"lowest"`
	OrderCount uint64  `json:"order_count"`
	Volume     uint64  `json:"volume"`
}

type HistorySnapshot struct {
	closed bool
	file   *os.File
	fileMu sync.Mutex
	Dates  []time.Time
}

// Reads the whole snapshot file and returns the historyDays that match the
// date passed in arguments
func (s *HistorySnapshot) GetHistoryDataForDay(ctx context.Context, requestDate time.Time) ([]HistoryDay, error) {
	if !slices.Contains(s.Dates, requestDate) {
		return nil, nil
	}

	// this lock should not block if ctx.Done
	s.fileMu.Lock()
	defer s.fileMu.Unlock()
	_, err := s.file.Seek(0, 0)
	if err != nil {
		return nil, fmt.Errorf("seek: %w", err)
	}

	historyDays := make([]HistoryDay, 0, 1024)
	requestDateUnix := uint64(requestDate.Unix())
	for {
		if err := ctx.Err(); err != nil {
			return nil, err
		}

		var snapDay snapHistoryDay
		err := binary.Read(s.file, binary.NativeEndian, &snapDay)
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("reading day from snapshot file: %w", err)
		}

		if snapDay.Date == requestDateUnix {
			historyDays = append(historyDays, HistoryDay{
				RegionId:   snapDay.RegionId,
				TypeId:     snapDay.TypeId,
				Average:    snapDay.Average,
				Highest:    snapDay.Highest,
				Lowest:     snapDay.Lowest,
				OrderCount: snapDay.OrderCount,
				Volume:     snapDay.Volume,
			})
		}
	}

	return historyDays, nil
}

// Don't forget to defer the call to Close to remove the snapshot data from
// disk when you finished using it. Close can be called multiple times
func (s *HistorySnapshot) Close() error {
	// BUG: Sometimes the snapshot file is not removed properly
	if !s.closed {
		// this lock should not block if ctx.Done
		s.fileMu.Lock()
		defer s.fileMu.Unlock()
		err := s.file.Close()
		if err != nil {
			return fmt.Errorf("close: %w", err)
		}
		err = os.Remove(s.file.Name())
		if err != nil {
			return fmt.Errorf("remove: %w", err)
		}
		log.Printf("Debug: successfully removed %v", s.file.Name())
		s.closed = true
	}
	return nil
}

// DownloadFullHistoryDump will download the full market history available for
// a slice for markets. This history data represent multiple bigabyte of data.
// To avoid taking as much ram as web browser this data will written to disk
// and DownloadFullHistoryDump will return a handle to this data. You can then
// request history data for a specific day by calling the GetHistoryDataForDay.
// **Don't forget to defer the call to Close to remove the history data from
// disk when the program closes.** In documentation we refer to this history
// data as an history snapshot.
func DownloadFullHistoryDump(
	ctx context.Context,
	markets []HistoryMarket,
) (*HistorySnapshot, error) {
	var err error
	snapshot := HistorySnapshot{}
	snapshot.Dates = make([]time.Time, 0, 500)
	snapshot.file, err = os.CreateTemp("", "evemarketdump_history_*")
	if err != nil {
		return nil, fmt.Errorf("failed to os.CreateTemp: %w", err)
	}

	// PERF: this process could be parallelized over some number of worker
	// threads to improve speed
	for _, m := range markets {
		if err := ctx.Err(); err != nil {
			return nil, err
		}

		trails := 3
	TryAgain:
		uri := fmt.Sprintf("/markets/%d/history?type_id=%d", m.RegionId, m.TypeId)
		response, err := esiFetch[[]esiHistoryDay](ctx, "GET", uri, false, nil, 5)
		var esiError *esiError
		isEsiError := errors.As(err, &esiError)
		if isEsiError && (esiError.code == 400 || esiError.code == 404) {
			// Skip this market
			continue
		} else if err != nil && trails > 1 && !isEsiError {
			if isLoggingEnabled(ctx) {
				log.Print("DownloadFullHistoryDump: Encountered an error while downloading histories, taking a 15 minutes break")
			}
			err := sleepWithContext(ctx, 15*time.Minute)
			if err != nil {
				return nil, err
			}
			trails -= 1
			goto TryAgain
		} else if err != nil {
			return nil, fmt.Errorf("fetching history in region %d for type %d: %w", m.RegionId, m.TypeId, err)
		}
		esiHistory := *response.data

		for i := range esiHistory {
			var snapDay snapHistoryDay
			err := esiHistoryDayToSnapHistoryDay(&esiHistory[i], &snapDay, m.RegionId, m.TypeId)
			if err != nil {
				return nil, fmt.Errorf("invalid history day: %w", err)
			}
			snapshotAddDate(&snapshot, time.Unix(int64(snapDay.Date), 0))

			err = binary.Write(snapshot.file, binary.NativeEndian, snapDay)
			if err != nil {
				return nil, fmt.Errorf("can't write to disk history day: %w", err)
			}
		}
	}

	snapshotSortDates(&snapshot)
	return &snapshot, nil
}

func snapshotAddDate(snapshot *HistorySnapshot, date time.Time) {
	for _, d := range snapshot.Dates {
		if d == date {
			return
		}
	}
	snapshot.Dates = append(snapshot.Dates, date)
}

type ByDate []time.Time

func (a ByDate) Len() int           { return len(a) }
func (a ByDate) Swap(i, j int)      { a[i], a[j] = a[j], a[i] }
func (a ByDate) Less(i, j int) bool { return a[i].Before(a[j]) }

func snapshotSortDates(snapshot *HistorySnapshot) {
	sort.Sort(ByDate(snapshot.Dates))
}

func esiHistoryDayToSnapHistoryDay(esiHistoryDay *esiHistoryDay, historyDay *snapHistoryDay, regionId uint64, typeId uint64) error {
	date, err := time.Parse(esiDateLayout, esiHistoryDay.Date)
	if err != nil {
		return fmt.Errorf("invalid history date \"%v\": %w", esiHistoryDay.Date, err)
	}

	*historyDay = snapHistoryDay{
		Date:       uint64(date.Unix()),
		RegionId:   regionId,
		TypeId:     typeId,
		Average:    esiHistoryDay.Average,
		Highest:    esiHistoryDay.Highest,
		Lowest:     esiHistoryDay.Lowest,
		OrderCount: esiHistoryDay.OrderCount,
		Volume:     esiHistoryDay.Volume,
	}
	return nil
}

// DownloadIncrementalHistoryDump downloads the history data for said day and
// markets. This operarion can take a very long time depending upon the number
// of markets you are reading data from. For 300_000 markets this operation
// takes around 8 hours.
//
// Be carefull to pass a UTC date as parameter.
func DownloadIncrementalHistoryDump(
	ctx context.Context,
	markets []HistoryMarket,
	requestDate time.Time,
) ([]HistoryDay, error) {
	historyDays := make([]HistoryDay, 0, 1024)

	// PERF: this process could be parallelized over some number of worker
	// threads to improve speed
	for _, m := range markets {
		if err := ctx.Err(); err != nil {
			return nil, err
		}

		trails := 3
	TryAgain:
		uri := fmt.Sprintf("/markets/%d/history?type_id=%d", m.RegionId, m.TypeId)
		response, err := esiFetch[[]esiHistoryDay](ctx, "GET", uri, false, nil, 5)
		var esiError *esiError
		isEsiError := errors.As(err, &esiError)
		if isEsiError && (esiError.code == 400 || esiError.code == 404) {
      // Skip this market
      continue
    } else if err != nil && trails > 1 && !isEsiError {
			if isLoggingEnabled(ctx) {
				log.Print("DownloadIncrementalHistoryDump: Encountered an error while downloading histories, taking a 15 minutes break")
			}
			err := sleepWithContext(ctx, 15*time.Minute)
			if err != nil {
				return nil, err
			}
			trails -= 1
			goto TryAgain
		} else if err != nil {
			return nil, fmt.Errorf("fetching history in region %d for type %d: %w", m.RegionId, m.TypeId, err)
		}
		esiHistory := *response.data

		for i := range esiHistory {
			date, err := time.Parse(esiDateLayout, esiHistory[i].Date)
			if err != nil {
				return nil, fmt.Errorf("invalid history date \"%v\": %w", esiHistory[i].Date, err)
			}

			if date == requestDate {
				historyDays = append(historyDays, HistoryDay{
					RegionId:   m.RegionId,
					TypeId:     m.TypeId,
					Average:    esiHistory[i].Average,
					Highest:    esiHistory[i].Highest,
					Lowest:     esiHistory[i].Lowest,
					OrderCount: esiHistory[i].OrderCount,
					Volume:     esiHistory[i].Volume,
				})
				break
			}
		}
	}

	return historyDays, nil
}

func sleepWithContext(ctx context.Context, duration time.Duration) error {
	timer := time.NewTimer(duration)
	select {
	case <-timer.C:
		return nil
	case <-ctx.Done():
		if !timer.Stop() {
			<-timer.C
		}
		return ctx.Err()
	}
}

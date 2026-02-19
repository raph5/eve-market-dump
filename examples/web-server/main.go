package main

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"slices"
	"strconv"
	"sync"
	"syscall"
	"time"

	emd "github.com/raph5/eve-market-dump"
)

type historyDump struct {
	Date int64
	Data []emd.HistoryDay
}
type orderDump struct {
	Date int64
	Data []emd.Order
}
type locationDump struct {
	Date int64
	Data []emd.Location
}

var (
	globalHistories   []historyDump
	globalHistoriesMu sync.RWMutex

	globalOrders        orderDump
	globalOrdersMu      sync.RWMutex
	globalOrdersFetched chan struct{}

	globalLocations          locationDump
	globalLocationsMu        sync.RWMutex
	globalForbiddenLocations []uint64 // don't need to be protected by globalLocationsMu
)

func init() {
	globalOrdersFetched = make(chan struct{}, 1)
	globalLocations.Data = make([]emd.Location, 0)
	globalOrders.Data = make([]emd.Order, 0, 100_000)
	for i := range globalHistories {
		globalHistories[i].Data = make([]emd.HistoryDay, 100_000)
	}
}

func historyWorker(ctx context.Context) {
	globalHistories = nil

	select {
	case <-ctx.Done():
		log.Printf("History Worker Canceled: %v", ctx.Err())
		return
	case <-globalOrdersFetched:
	}

	globalOrdersMu.RLock()
	activeMarkets := getActiveMarkets(globalOrders.Data)
	globalOrdersMu.RUnlock()

	log.Printf("History Worker: full download start")
	fullDownloadStart := time.Now()
	snapshot, err := emd.DownloadFullHistoryDump(ctx, activeMarkets)
	if err != nil {
		log.Printf("History Worker Error: DownloadFullHistoryDump: %v", err)
		return
	}
	defer snapshot.Close() // Important
	log.Printf("History Worker: full download processing")

	i := 0
	if len(snapshot.Dates) > 10 {
		i = len(snapshot.Dates) - 10
	}
	for _, date := range snapshot.Dates[i:] {
		historyData, err := snapshot.GetHistoryDataForDay(ctx, date)
		if err != nil {
			log.Printf("History Worker Error: GetHistoryDataForDay: %v", err)
			return
		}
		globalHistoriesMu.Lock()
		globalHistories = append(globalHistories, historyDump{
			Date: date.Unix(),
			Data: historyData,
		})
		globalHistoriesMu.Unlock()
	}

	log.Printf("History Worker: full download end")
	err = snapshot.Close()
	if err != nil {
		log.Printf("History Worker Error: closing snapshot: %v", err)
		return
	}

	elevenFifteenTomorrow := getElevenFifteenTomorrow(fullDownloadStart)
	elevenFifteenToday := getElevenFifteenToday(fullDownloadStart)
	expiration := elevenFifteenTomorrow
	if fullDownloadStart.Before(elevenFifteenToday) {
		expiration = elevenFifteenToday
	}

	for {
		if err := ctx.Err(); err != nil {
			return
		}

		now := time.Now()
		timeToWait := expiration.Sub(now)
		if timeToWait > 0 {
			log.Print("History Worker: up to date")

			sleepWithContext(ctx, timeToWait)
			continue
		}

		// here we assume that every market is up to date at 11:15 as stated at
		// https://developers.eveonline.com/api-explorer#/operations/GetMarketsRegionIdHistory
		log.Printf("History Worker: incremental download start")
		date := getYesterday(now)
		globalOrdersMu.RLock()
		activeMarkets := getActiveMarkets(globalOrders.Data)
		globalOrdersMu.RUnlock()
		historyData, err := emd.DownloadIncrementalHistoryDump(ctx, activeMarkets, date)
		if err != nil {
			log.Printf("History Worker Error: DownloadIncrementalHistoryDump: %v", err)
			continue
		}
		log.Printf("History Worker: incremental download end")

		globalHistoriesMu.Lock()
		globalHistories = append(globalHistories, historyDump{
			Date: date.Unix(),
			Data: historyData,
		})
		globalHistoriesMu.Unlock()

		expiration = expiration.Add(24 * time.Hour)
	}
}

func orderWorker(ctx context.Context, secrets *emd.ApiSecrets) {
	expiration := time.Now()

	for {
		if err := ctx.Err(); err != nil {
			return
		}

		now := time.Now()
		timeToWait := expiration.Sub(now)
		if timeToWait > 0 {
			log.Print("Order Worker: up to date")

			sleepWithContext(ctx, timeToWait)
			continue
		}

		log.Printf("Order Worker: orders download start")
		orders, err := emd.DownloadOrderDump(ctx)
		if err != nil {
			log.Printf("Order Worker Error: DownloadOrderDump: %v", err)
			continue
		}
		expiration = expiration.Add(10 * time.Minute)
		log.Printf("Order Worker: orders download end")

		// Signaling to historyWorker that he can start working
		select {
		case globalOrdersFetched <- struct{}{}:
		default:
		}

		globalOrdersMu.Lock()
		globalOrders = orderDump{Date: now.Unix(), Data: orders}
		globalOrdersMu.Unlock()

		globalLocationsMu.RLock()
		unknownLocation := getUnknownLocations(orders, globalLocations.Data, globalForbiddenLocations)
		globalLocationsMu.RUnlock()
		if len(unknownLocation) > 0 {
			log.Printf("Order Worker: location download start")
			locations, forbiddenLocations, err := emd.DownloadLocationDump(ctx, unknownLocation, secrets)
			if err != nil {
				log.Printf("Order Worker Error: DownloadLocationDump: %v", err)
				globalForbiddenLocations = append(globalForbiddenLocations, forbiddenLocations...)
				continue
			}
			log.Printf("Order Worker: location download end")

			globalForbiddenLocations = append(globalForbiddenLocations, forbiddenLocations...)
			globalLocationsMu.Lock()
			globalLocations = locationDump{
				Date: now.Unix(),
				Data: append(globalLocations.Data, locations...),
			}
			globalLocationsMu.Unlock()
		}
	}
}

func httpServerWorker(ctx context.Context) {
	handleIndex := func(w http.ResponseWriter, r *http.Request) {
		globalLocationsMu.RLock()
		globalHistoriesMu.RLock()
		globalOrdersMu.RLock()
		fmt.Fprintf(w, "<h1>Eve Market Dump</h1>\n")
		fmt.Fprintf(w, "<h2>Locations</h2><hr>\n")
		fmt.Fprintf(w, "<a href=\"%s\">%s %s</a><br>\n", "/location", time.Unix(globalLocations.Date, 0).Format("2006-01-02T15:04:05Z"), "location dump")
		fmt.Fprintf(w, "<h2>Orders</h2><hr>\n")
		fmt.Fprintf(w, "<a href=\"%s\">%s %s</a><br>\n", "/order", time.Unix(globalOrders.Date, 0).Format("2006-01-02T15:04:05Z"), "order dump")
		fmt.Fprintf(w, "<h2>Histories</h2><hr>\n")
		for _, h := range globalHistories {
			fmt.Fprintf(w, "<a href=\"/history/%d\">%s history dump</a><br>\n", h.Date, time.Unix(h.Date, 0).Format("2006-01-02T15:04:05Z"))
		}
		globalLocationsMu.RUnlock()
		globalHistoriesMu.RUnlock()
		globalOrdersMu.RUnlock()
	}

	handleLocation := func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		globalLocationsMu.RLock()
		err := jsonEncodeArray(w, globalLocations.Data)
		if err != nil {
			log.Printf("Http Server Worker Error: encode location response: %v", err)
		}
		globalLocationsMu.RUnlock()
	}

  // NOTE: The endponit sends around 350megs of json. If you are sending this
  // over the network you should at least gzip it.
	handleOrder := func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		globalOrdersMu.RLock()
		err := jsonEncodeArray(w, globalOrders.Data)
		if err != nil {
			log.Printf("Http Server Worker Error: encode order response: %v", err)
		}
		globalOrdersMu.RUnlock()
	}

	handleHistory := func(w http.ResponseWriter, r *http.Request) {
		date, err := strconv.Atoi(r.PathValue("date"))
		if err != nil {
			http.NotFound(w, r)
			return
		}

		globalHistoriesMu.RLock()
		for _, h := range globalHistories {
			if h.Date == int64(date) {
				globalHistoriesMu.RUnlock()
				w.Header().Set("Content-Type", "application/json")
				w.WriteHeader(http.StatusOK)
				err := jsonEncodeArray(w, h.Data)
				if err != nil {
					log.Printf("Http Server Worker Error: encode history response: %v", err)
				}
				return
			}
		}
		globalHistoriesMu.RUnlock()
		http.NotFound(w, r)
	}

	errCh := make(chan error)
	mux := http.NewServeMux()
	mux.HandleFunc("/", handleIndex)
	mux.HandleFunc("/location", handleLocation)
	mux.HandleFunc("/order", handleOrder)
	mux.HandleFunc("/history/{date}", handleHistory)
	server := &http.Server{
		Addr:    ":8080",
		Handler: mux,
	}

	go func() {
		log.Printf("Http Server Worker: listening on http://localhost%s", server.Addr)
		err := server.ListenAndServe()
		if err != nil {
			errCh <- err
		}
	}()

	select {
	case <-ctx.Done():
	case err := <-errCh:
		log.Printf("Http Server Worker Error: %v", err)
	}

	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer shutdownCancel()
	err := server.Shutdown(shutdownCtx)
	if err != nil {
		log.Printf("Http Server Wroker Error: %v", err)
	}
}

func getElevenFifteenToday(now time.Time) time.Time {
	return time.Date(now.Year(), now.Month(), now.Day(), 11, 15, 0, 0, now.Location())
}

func getElevenFifteenTomorrow(now time.Time) time.Time {
	return time.Date(now.Year(), now.Month(), now.Day()+1, 11, 15, 0, 0, now.Location())
}

func getYesterday(now time.Time) time.Time {
	return time.Date(now.Year(), now.Month(), now.Day()-1, 0, 0, 0, 0, now.Location())
}

func getActiveMarkets(orders []emd.Order) []emd.HistoryMarket {
	activeMarketsMap := make(map[emd.HistoryMarket]struct{}, 350_000)
	activeMarkets := make([]emd.HistoryMarket, 0, 350_000)
	for _, o := range orders {
		market := emd.HistoryMarket{RegionId: o.RegionId, TypeId: o.TypeId}
		if _, ok := activeMarketsMap[market]; !ok {
			activeMarketsMap[market] = struct{}{}
			activeMarkets = append(activeMarkets, market)
		}
	}
	return activeMarkets
}

func getUnknownLocations(orders []emd.Order, knownLocations []emd.Location, forbiddenLocations []uint64) []uint64 {
	unknownLocations := make([]uint64, 0, 32)
OrderLoop:
	for _, o := range orders {
		for _, l := range knownLocations {
			if o.LocationId == l.Id {
				continue OrderLoop
			}
		}
		if slices.Contains(forbiddenLocations, o.LocationId) {
			continue OrderLoop
		}
		if slices.Contains(unknownLocations, o.LocationId) {
			continue OrderLoop
		}
		unknownLocations = append(unknownLocations, o.LocationId)
	}
	return unknownLocations
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

// Encode array with resoanble memory usage
func jsonEncodeArray[T any](w io.Writer, array []T) error {
	_, err := w.Write([]byte("["))
	if err != nil {
		return err
	}
	for i := range array {
		if i > 0 {
			_, err = w.Write([]byte(","))
			if err != nil {
				return err
			}
		}
		el, err := json.Marshal(array[i])
		if err != nil {
			return err
		}
		_, err = w.Write(el)
		if err != nil {
			return err
		}
	}
	_, err = w.Write([]byte("]"))
	if err != nil {
		return err
	}
	return nil
}

func main() {
	// Init logger
	log.SetFlags(log.LstdFlags)

	// Init secrets
	secrets := emd.ApiSecrets{
		SsoRefreshToken: os.Getenv("SSO_REFRESH_TOKEN"),
		SsoClientId:     os.Getenv("SSO_CLIENT_ID"),
		SsoClientSecret: os.Getenv("SSO_CLIENT_SECRET"),
	}
	if secrets.SsoClientId == "" || secrets.SsoClientSecret == "" || secrets.SsoRefreshToken == "" {
		log.Fatal("Environement variabels SSO_REFRESH_TOKEN, SSO_CLIENT_ID and SSO_CLIENT_SECRET are not set")
	}

	// Create context
	ctx, cancel := context.WithCancel(context.Background())
	ctx = emd.EnableLogging(ctx)
	exitCh := make(chan os.Signal, 1)
	signal.Notify(exitCh, syscall.SIGINT, syscall.SIGTERM)

	// Starting wrokers
	var mainWg sync.WaitGroup
	mainWg.Add(3)
	go func() {
		historyWorker(ctx)
		log.Print("History Worker: stopped")
		mainWg.Done()
		cancel()
	}()
	go func() {
		orderWorker(ctx, &secrets)
		log.Print("Order Worker: stopped")
		mainWg.Done()
		cancel()
	}()
	go func() {
		httpServerWorker(ctx)
		log.Print("Http Server Worker: stopped")
		mainWg.Done()
		cancel()
	}()
	log.Print("Server Started")

	// Handle store shutdown
	select {
	case <-exitCh:
		log.Print("Web Server Stopping...")
		cancel()
	case <-ctx.Done():
	}
	signal.Reset(syscall.SIGINT, syscall.SIGTERM)
	mainWg.Wait()
	log.Print("Web Server Stopped Gracefully")
}

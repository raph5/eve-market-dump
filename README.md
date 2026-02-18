
# EVE Market Dump

*EVE Market Dump* (EMD) is an angry little web client that is screaming at the
ESI to send it the entier market state so that other developpers don't have to.

Originally EMD was first built as a standalone C executable. Building this
backend in C taught me a lot. In part that taugth me that building a backend
on top of POSIX and libcurl is a real pain (how could have gessed right?). The
source code for this C executable is still available is the archive directory.

But having a C standalone process responsible for a part of your backend work
means that you are now responsible for managing this process and some IPC,
which also a pain. For these reasons I decided to refactor this executable to a
go library.

# Examples

An example web server using this library is available is the /example directory

# Documentation

```go
// common.go //////////////////////////////////////////////////////////////////

// ApiSecrets are required to fetch player strcture info (via the
// /universe/structures api endpoint).
//
// To get your clientId and clientSecret create and application at
// https://developers.eveonline.com/applications (CCP likes to change the url
// of this page from time to time). The refresh token allows authentifiation to
// an EVE character. To get the refresh token your need to follow the steps of
// SSO authentification until you recive, see
// https://docs.esi.evetech.net/docs/sso/web_based_sso_flow.html.
//
// A git push request with a bash script that simplify the request of getting
// refresh token for a character would be much appreciated.
type ApiSecrets struct {
	SsoClientId     string
	SsoClientSecret string
	SsoRefreshToken string
}

// returns a context that can be passed to library function to enable logging
func EnableLogging(ctx context.Context) context.Context

// esi.go /////////////////////////////////////////////////////////////////////
var ErrNoTrailsLeft = errors.New("No trails left")
var ErrImplicitTimeout = errors.New("Esi implicit timeout")
var ErrErrorRateTimeout = errors.New("Esi error rate timeout")
var ErrExplicitTimeout = errors.New("Esi explicit timeout")

// regions.go /////////////////////////////////////////////////////////////////
var GlobalPlexMarket = 19000001
var Regions = [...]uint64{
	10000002,
	10000043,
    ...
	19000001, // global plex market
}

// locations.go ///////////////////////////////////////////////////////////////
var ErrUnknownNpcStation = errors.New("Unknown npc station, You should renew data/stations.csv")
var ErrUnknownSystem = errors.New("Unknown solar system, You should renew data/systemscsv")

type Location struct {
	Id       uint64
	TypeId   uint64 // station type id
	OwnerId  uint64 // station corporation id
	SystemId uint64
	Security float32
	Name     string
}

// DownloadLocationDump will return a slice of forbidden locations. Avoid
// requesting these locations in your subsequent requests. Otherwise you will
// suffer error rate timeouts from the ESI.
func DownloadLocationDump(
	ctx context.Context,
	unknown_location []uint64,
	secrets *ApiSecrets,
) ([]Location, /* forbiddenLocations */ []uint64, error)

// orders.go //////////////////////////////////////////////////////////////////
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

func DownloadOrderDump(ctx context.Context) ([]Order, error)

// history.go /////////////////////////////////////////////////////////////////
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

// DownloadFullHistoryDump will download the full market history available for
// a slice for markets. This history data represent multiple bigabyte of data.
// To avoid taking as much ram as web browser this data will written to disk
// and DownloadFullHistoryDump will return a handle to this data. You can then
// request history data for a specific day by calling the GetHistoryDataForDay.
// **Don't forget to defer the call to Close to remove the history data from
// disk when the program closes.** In documentation we refer to this history
// data as an history snapshot.
//
// This operarion can take a very long time depending upon the number of
// markets you are reading data from. For 300_000 markets this operation takes
// around 8 hours.
func DownloadFullHistoryDump(
	ctx context.Context,
	markets []HistoryMarket,
) (*HistorySnapshot, error)

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
) ([]HistoryDay, error)

type HistorySnapshot struct {
    ...  // private fields
	Dates  []time.Time
}

// Reads the whole snapshot file and returns the historyDays that match the
// date passed in arguments
func (s *HistorySnapshot) GetHistoryDataForDay(ctx context.Context, requestDate time.Time) ([]HistoryDay, error)

// Don't forget to defer the call to Close to remove the snapshot data from
// disk when you finished using it. Close can be called multiple times
func (s *HistorySnapshot) Close() error
```

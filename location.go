package evemarketdump

import (
	"bytes"
	"context"
	_ "embed"
	"encoding/csv"
	"errors"
	"fmt"
	"io"
	"log"
	"strconv"
)

type Location struct {
	Id       uint64
	TypeId   uint64 // station type id
	OwnerId  uint64 // station corporation id
	SystemId uint64
	Security float32
	Name     string
}

type station struct {
	id       uint64
	name     string
	security float32
	typeId   uint64
	ownerId  uint64
	systemId uint64
}

type system struct {
	id       uint64
	security float32
}

type esiStructure struct {
	Name     string `json:"name"`
	SystemId uint64 `json:"solar_system_id"`
	TypeId   uint64 `json:"type_id"`
	OwnerId  uint64 `json:"owner_id"`
}

//go:embed data/stations.csv
var csvStations []byte
var stations []station

//go:embed data/systems.csv
var csvSystems []byte
var systems []system

var ErrUnknownNpcStation = errors.New("Unknown npc station, You should renew data/stations.csv")
var ErrUnknownSystem = errors.New("Unknown solar system, You should renew data/systemscsv")

func init() {
	var err error
	stations, err = readStationSvg()
	if err != nil {
		log.Panicf("readStationSvg: %v", err)
	}
	systems, err = readSystemSvg()
	if err != nil {
		log.Panicf("readSystemSvg: %v", err)
	}
}

// DownloadLocationDump will return a slice of forbidden locations. Avoid
// requesting these locations in your subsequent requests. Otherwise you will
// suffer error rate timeouts from the ESI.
//
// note that forbiddenLocations are returns even the functions errors
func DownloadLocationDump(
	ctx context.Context,
	unknown_location []uint64,
	secrets *ApiSecrets,
) ([]Location /* forbiddenLocations */, []uint64, error) {
	locationData := make([]Location, 0, len(unknown_location))
	forbiddenLocations := make([]uint64, 0, len(unknown_location))

	for _, locId := range unknown_location {
		isNpcStation := locId >= 60000000 && locId <= 64000000
		if isNpcStation {
			station := getStationById(stations, locId)
			if station != nil {
				locationData = append(locationData, Location{
					Id:       station.id,
					TypeId:   station.typeId,
					OwnerId:  station.ownerId,
					SystemId: station.systemId,
					Security: station.security,
					Name:     station.name,
				})
			} else {
				return nil, forbiddenLocations, ErrUnknownNpcStation
			}
		} else {
			uri := fmt.Sprintf("/universe/structures/%d", locId)
			response, err := esiFetch[esiStructure](ctx, "GET", uri, true, secrets, 1)
			var esiError *esiError
			if errors.As(err, &esiError) {
				forbiddenLocations = append(forbiddenLocations, locId)
				continue
			}
			if err != nil {
				return nil, forbiddenLocations, fmt.Errorf("fetching esi strucure info: %w", err)
			}

			system := getSystemById(systems, response.data.SystemId)
			if system == nil {
				return nil, forbiddenLocations, ErrUnknownSystem
			}

			locationData = append(locationData, Location{
				Id:       locId,
				Security: system.security,
				TypeId:   response.data.TypeId,
				SystemId: response.data.SystemId,
				OwnerId:  response.data.OwnerId,
				Name:     response.data.Name,
			})
		}
	}

	return locationData, forbiddenLocations, nil
}

func getSystemById(systemSlice []system, id uint64) *system {
	for _, s := range systemSlice {
		if s.id == id {
			return &s
		}
	}
	return nil
}

func getStationById(stationSlice []station, id uint64) *station {
	for _, s := range stationSlice {
		if s.id == id {
			return &s
		}
	}
	return nil
}

func readSystemSvg() ([]system, error) {
	r := csv.NewReader(bytes.NewReader(csvSystems))
	record, err := r.Read()
	if err != nil {
		return nil, fmt.Errorf("reader error: %w", err)
	}
	if record[0] != "solarSystemID" || record[1] != "security" {
		return nil, fmt.Errorf("invalid system csv header %v", record)
	}

	systemSlice := make([]system, 0, 9000)
	for {
		record, err := r.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("record read: %w", err)
		}

		id, err := strconv.ParseUint(record[0], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("id in not a valid uint64: %w", err)
		}
		security, err := strconv.ParseFloat(record[1], 32)
		if err != nil {
			return nil, fmt.Errorf("security in not a valid float32: %w", err)
		}

		systemSlice = append(systemSlice, system{
			id:       id,
			security: float32(security),
		})
	}

	return systemSlice, nil
}

func readStationSvg() ([]station, error) {
	r := csv.NewReader(bytes.NewReader(csvStations))
	record, err := r.Read()
	if err != nil {
		return nil, fmt.Errorf("reader error: %w", err)
	}
	if record[0] != "stationID" ||
		record[1] != "security" ||
		record[2] != "stationTypeID" ||
		record[3] != "corporationID" ||
		record[4] != "solarSystemID" ||
		record[5] != "stationName" {
		return nil, fmt.Errorf("invalid station csv header %v", record)
	}

	stationSlice := make([]station, 0, 6000)
	for {
		record, err := r.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("record read: %w", err)
		}

		id, err := strconv.ParseUint(record[0], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("id in not a valid uint64: %w", err)
		}
		security, err := strconv.ParseFloat(record[1], 32)
		if err != nil {
			return nil, fmt.Errorf("security in not a valid float32: %w", err)
		}
		typeId, err := strconv.ParseUint(record[2], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("typeId in not a valid uint64: %w", err)
		}
		ownerId, err := strconv.ParseUint(record[3], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("ownerId in not a valid uint64: %w", err)
		}
		systemId, err := strconv.ParseUint(record[4], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("systemId in not a valid uint64: %w", err)
		}

		stationSlice = append(stationSlice, station{
			id:       id,
			security: float32(security),
			typeId:   typeId,
			ownerId:  ownerId,
			systemId: systemId,
			name:     record[5],
		})
	}

	return stationSlice, nil
}

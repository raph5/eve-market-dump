#!/usr/bin/env bash
set -e

curl https://www.fuzzwork.co.uk/dump/latest/staStations.csv |
  mlr --csv cut -f stationID,security,stationTypeID,corporationID,solarSystemID,stationName \
  > stations.csv

xxd -i -n stations_csv \
  < stations.csv \
  > stations.csv.h

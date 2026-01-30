#!/usr/bin/env bash
set -e

curl https://www.fuzzwork.co.uk/dump/latest/mapSolarSystems.csv |
  mlr --csv cut -f solarSystemID,security \
  > systems.csv

xxd -i -n systems_csv \
  < systems.csv \
  > systems.csv.h

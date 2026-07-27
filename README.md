# Pebble WindSock
Basic Windsock app for Pebble watch.
I mainly built this for a quick wind check whilst drone flying, but I can imagine it being useful for many other an outdoor activity :-)

- Current release: 1.2.0
- Also released app to Pebble Store. PBW available in releases here

## Features
- Main screen with location, wind direction arrow (!) and windspeeds for 10m, gusts and 100m
- Forecast screen with a 24h forecast list (incl direction!)
- 6 Hour forecast graph on the third screen
- Phone-side settings screen to choose wind models: Global (default) or Benelux-KNMI. The latter actually reports high winds from 100m but is a more accurate model

<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/app_list.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/main.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/forecast.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/graph.png"></img>&nbsp;&nbsp;

Wind data comes from https://open-meteo.com from their free JSON spewing api which will basically cover the whole of Europe (or even the world?). Location data is provided by the phone of course but needs to be resolved into a friendly name string, which is done by a BigDataCloud call. Loads data fresh at every app start, select button triggers manual refresh.

## Phone settings

<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/phone_settings.png" width="320">

Coded completely with Claude and PebbleCloud.

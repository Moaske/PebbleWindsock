# Pebble WindSock
Basic Windsock app for Pebble watch.
I mainly built this for a quick wind check whilst drone flying, but I can imagine it being useful for many other an outdoor activity :-)

- Initial setup of repo with v1.0.0
- Still needs design tweaking (so new screenshots to expect)

## Features
- Main screen with location, wind direction arrow (!) and windspeeds for 10m, gusts and 100m
- Forecast screen with a 24h forecast list (incl direction!)
- 6 Hour forecast graph on the third screen

<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/app_list.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/main.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/forecast.png"></img>&nbsp;&nbsp;<img src="https://github.com/Moaske/PebbleWindsock/blob/main/docs/graph.png"></img>&nbsp;&nbsp;

Wind data comes from https://open-meteo.com from their free JSON spewing api which will basically cover the whole of Europe (or even the world?). Location data is provided by the phone of course but needs to be resolved into a friendly name string, which is done by a BigDataCloud call. Does not need any app settings, api keys or whatever, it just gets your location friendly name and the forecast everytime you start the app.

Coded completely with Claude and PebbleCloud.

When the polish is done, I'll release this to the Pebble Store

"""Convert an Open-Meteo JSON (daily + hourly incl. cloud_cover) to the
days.txt format read by tools/test_weather_derive.cpp."""
import json, sys
d = json.load(open(sys.argv[1])); D = d["daily"]; H = d["hourly"]
for i, date in enumerate(D["time"]):
    sr = int(D["sunrise"][i][11:13]); ss = int(D["sunset"][i][11:13])
    print("DAY", date, sr, ss, D["precipitation_sum"][i] or 0, D["weather_code"][i])
    for h in range(i * 24, i * 24 + 24):
        print(H["weather_code"][h], H["precipitation"][h] or 0, H["temperature_2m"][h], H["cloud_cover"][h])

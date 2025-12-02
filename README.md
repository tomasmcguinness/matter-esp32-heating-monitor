# Matter Heating Monitor

## Building

Start by ensuing the html_app is compiled

```
cp html_data
npm run build -- --emptyOutDir
```

Then compile the firmware

```
idf.py build
```

## Running

Flash the firmware onto an the S3 OTBR board, opening the monitor too

```
idf.py flash monitor
```

Enter the command into the console to connect the board to WiFi

```
matter esp wifi connect {SSID} {Password}
```
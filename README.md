# Matter Heating Monitor

The Heating Monitor is a Matter based application designed to aggregate all information about your home heating system. At present, it supports monitoring room temperatures, radiator temperatures and can integrate with any Heat Pumps that support Matter (or can be connected via Modbus)

## Goals

This project has two aims:
1. Provide a cheap and easy way to monitor and record the performance of the heating system in your home.
2. Allow an AI to access the data and provide insights and reports allowing you to tweak the heating for better performance.

## Getting Started

To run the project, you'll need a **Waveshare ESP32-S3-ETH** dev board, which costs around USD$20. 

To power the board, you'll need a USB-C power supply. A wall charger is the easiest. For networking, just use an ethernet cable to connect the device to your router.

To record data, an SD card is required, but the application will work perfectly well without one. A system with a heat pump, 10 rooms and 10 radiators will use about 1GB a year. The data is all saved in CSV, so you can copy it off at any time. 

Once the board is plugged in and powered up, just visit http://heating-monitor.local in your browser.

## Adding your first sensor

There are two approaches you can take to adding a sensor.

1. First, commission the device with your iOS Home or Google Home, then share it and commission it from the Devices tab.

2. Commission the device using the Matter Controller Companion app (MCC) from the App Store.

## Supported Room Temperature Sensors

By using the Matter smart home protocol, this application supports anything that supports Matter. I have tested with 10 IKEA TIMMERFLOTTE sensors (GBP£5 each). 

## Why not Home Assistant?

This project doesn't replace Home Assistant, or any other like it. I wanted to creating something that was lower touch and that highlighted Matter.
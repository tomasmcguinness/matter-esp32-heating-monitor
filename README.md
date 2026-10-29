# Matter Heating Monitor

The Heating Monitor is a Matter based application designed to aggregate all information about your home heating system. At present, it supports monitoring room temperatures, radiator temperatures and can integrate with any Heat Pumps that support Matter (or can be connected via Modbus)

## Goals

This project has two aims:
1. Provide a cheap and easy way to monitor and record the heating system in your home.
2. Allow an AI to access the data and provide insights and reports allowing you to tweak the heating for better performance.

## Supported Temperature Sensors

By using the Matter smart home protocol, this application supports anything that supports Matter. I have tested with 10 IKEA TIMMERFLOTTE sensors (GBP£5 each). 

## Running the application

This firmware is designed for the **Waveshare ESP32-S3-ETH** board (16MB flash, octal PSRAM). They cost about USD$20.

To record data, an SD card is required, but the application will work perfectly well without one. A system with a heat pump, 10 rooms and 10 radiators will use about 1GB a year. The data is all saved in CSV, so you can copy it off at any time. 

## Why not Home Assistant?

This project doesn't replace Home Assistant, or any other like it. I wanted to creating something that was lower touch and that highlighted Matter.
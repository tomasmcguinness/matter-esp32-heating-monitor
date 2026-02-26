import csv
import requests
import json

radiatorIds = {}

with open ('radiators.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        response = requests.post('http://192.168.1.105/api/radiators', json={
            "name": row[0],
            "mqttName": row[1],
            "type": int(row[2]),
            "output": int(row[3]),
            "flowSensorNodeId": int(row[4],0),
            "flowSensorEndpointId": row[5],
            "returnSensorNodeId": int(row[6],0),
            "returnSensorEndpointId": row[7]
        })

        # Store the radiatorID against the mqttName (dictionary) for later use.
        response_dict = json.loads(response.text)

        radiatorIds[response_dict['mqttName']] = response_dict['radiatorId']

with open ('rooms.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        r = requests.post('http://192.168.1.105/api/rooms', json={
            "name": row[0],
            "targetTemperature": int(row[1]) * 100,
            "temperatureSensorNodeId": int(row[2]),
            "temperatureSensorEndpointId": int(row[3]),
            "heatLossPerDegree": int(row[4]),
            "radiatorIds": [radiatorIds[row[5]]]
        })

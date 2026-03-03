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
            "flowSensorEndpointId": int(row[5]),
            "returnSensorNodeId": int(row[6],0),
            "returnSensorEndpointId": int(row[7])
        })

        # Store the radiatorID against the mqttName (dictionary) for later use.
        response_dict = json.loads(response.text)

        radiatorIds[row[1]] = response_dict['radiatorId']

with open ('rooms.csv', 'r') as file:
    reader = csv.reader(file)
    
    for row in reader:
        r = requests.post('http://192.168.1.105/api/rooms', json={
            "name": row[0],
            "mqttName": row[1],
            "targetTemperature": int(row[2]) * 100,
            "temperatureSensorNodeId": int(row[3],0),
            "temperatureSensorEndpointId": int(row[4]),
            "heatLossPerDegree": int(row[5]),
            "radiatorIds": [radiatorIds[row[6]]]
        })

import csv
import requests

with open ('radiators.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        r = requests.post('http://192.168.1.105/api/radiators', json={
            "name": row[0],
            "mqttName": row[1],
            "type": int(row[2]),
            "output": int(row[3]),
            "flowSensorNodeId": int(row[4],0),
            "flowSensorEndpointId": row[5],
            "returnSensorNodeId": int(row[6],0),
            "returnSensorEndpointId": row[7]
        })
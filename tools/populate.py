import csv
import json
import httplib, urllib

with open ('radiators.csv', 'r') as file:
    reader = csv.reader(file)
    for row in reader:
        data = {}
        data['key'] = 'value'
        json_data = json.dumps(data)
        print(json_data)

        r = requests.post('http://192.168.1.1.105/api/radiators', json={
  "Id": 78912,
  "Customer": "Jason Sweet",
  "Quantity": 1,
  "Price": 18.00
})


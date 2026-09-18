// One emitter being edited on the Add Room or Edit Room form. `radiatorId` is only set for a
// radiator that already exists on the device; the sensors are held as "nodeId|endpointId".
//
export type Emitter = {
  key: number;
  radiatorId?: number;
  name: string;
  mqttName: string;
  type: number;
  output: string;
  flowSensor: string;
  returnSensor: string;
};

// The body for POST /api/radiators, which PUT /api/radiators/:id takes as well.
//
export function emitterToRadiatorJson(emitter: Emitter): string {
  const flowSensorNodeId = parseInt(emitter.flowSensor.split('|')[0]);
  const flowSensorEndpointId = parseInt(emitter.flowSensor.split('|')[1]);

  const returnSensorNodeId = parseInt(emitter.returnSensor.split('|')[0]);
  const returnSensorEndpointId = parseInt(emitter.returnSensor.split('|')[1]);

  return JSON.stringify({
    name: emitter.name,
    mqttName: emitter.mqttName.toLowerCase(),
    type: emitter.type,
    output: parseInt(emitter.output),
    flowSensorNodeId,
    flowSensorEndpointId,
    returnSensorNodeId,
    returnSensorEndpointId
  });
}

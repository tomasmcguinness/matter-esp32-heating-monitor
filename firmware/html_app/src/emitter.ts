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

// The radiator `type` the firmware reserves for an underfloor heating manifold
// (EMITTER_TYPE_UFH_MANIFOLD in radiator_manager.h). A manifold is added on the Emitters tab, never
// in a room's emitter list: rooms take a percentage of its output instead.
//
export const UFH_MANIFOLD_TYPE = 100;

export function isUfhManifold(emitter: { type: number }): boolean {
  return emitter.type === UFH_MANIFOLD_TYPE;
}

// A room's percentage of each UFH manifold's output, keyed by the manifold's radiator id.
export type UfhShares = Record<number, number>;

// The ufhShares body PUT/POST /api/rooms takes: only shares above 0%.
export function ufhSharesToJson(shares: UfhShares): { emitterId: number; pct: number }[] {
  return Object.entries(shares)
    .filter(([, pct]) => pct > 0)
    .map(([emitterId, pct]) => ({ emitterId: parseInt(emitterId), pct }));
}

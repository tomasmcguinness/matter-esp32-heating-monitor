import { useRef, useState } from "react";
import { NavLink, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";

type Emitter = {
  key: number;
  name: string;
  mqttName: string;
  type: number;
  output: string;
  flowSensor: string;
  returnSensor: string;
};

function AddRoom() {

  const navigate = useNavigate();

  const [name, setName] = useState<string | undefined>(undefined);
  const [mqttName, setMqttName] = useState<string | undefined>(undefined);
  const [targetTemperature, setTargetTemperature] = useState<number | undefined>(undefined);
  const [temperatureSensor, setTemperatureSensor] = useState<string | undefined>(undefined);

  const [emitters, setEmitters] = useState<Emitter[]>([]);
  const [saving, setSaving] = useState(false);

  const nextEmitterKey = useRef(1);

  function addEmitter() {
    setEmitters([...emitters, {
      key: nextEmitterKey.current++,
      name: '',
      mqttName: '',
      type: 10,
      output: '',
      flowSensor: '',
      returnSensor: ''
    }]);
  }

  function updateEmitter(key: number, changes: Partial<Emitter>) {
    setEmitters(emitters.map(e => e.key === key ? { ...e, ...changes } : e));
  }

  function removeEmitter(key: number) {
    setEmitters(emitters.filter(e => e.key !== key));
  }

  async function handleSubmit(e: any) {
    e.preventDefault();

    setSaving(true);

    try {
      // The radiators are created first, so their IDs can be sent with the room
      // and be assigned to it as it is created.
      //
      var radiatorIds: number[] = [];

      for (const emitter of emitters) {

        var flowSensorNodeId = parseInt(emitter.flowSensor.split('|')[0]);
        var flowSensorEndpointId = parseInt(emitter.flowSensor.split('|')[1]);

        var returnSensorNodeId = parseInt(emitter.returnSensor.split('|')[0]);
        var returnSensorEndpointId = parseInt(emitter.returnSensor.split('|')[1]);

        var radiatorJson = JSON.stringify({
          name: emitter.name,
          mqttName: emitter.mqttName.toLowerCase(),
          type: emitter.type,
          output: parseInt(emitter.output),
          flowSensorNodeId,
          flowSensorEndpointId,
          returnSensorNodeId,
          returnSensorEndpointId
        });

        var radiatorResponse = await fetch('/api/radiators', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: radiatorJson });

        if (!radiatorResponse.ok) {
          alert(`Failed to add the emitter ${emitter.name}. The room has not been created. Any emitters already added can be found on the Radiators page.`);
          return;
        }

        var radiator = await radiatorResponse.json();
        radiatorIds.push(radiator.radiatorId);
      }

      var temperatureSensorNodeId = parseInt(temperatureSensor!.split('|')[0]);
      var temperatureSensorEndpointId = parseInt(temperatureSensor!.split('|')[1]);

      var roomJson = JSON.stringify({
        name,
        mqttName: mqttName?.toLowerCase(),
        targetTemperature: targetTemperature! * 100,
        temperatureSensorNodeId,
        temperatureSensorEndpointId,
        radiatorIds
      });

      var roomResponse = await fetch('/api/rooms', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: roomJson });

      if (!roomResponse.ok) {
        alert("Failed to add the room. Any emitters that were added can be found on the Radiators page.");
        return;
      }

      var room = await roomResponse.json();
      navigate(`/rooms/${room.roomId}`);
    }
    finally {
      setSaving(false);
    }
  }

  let emitterCards = emitters.map((emitter, index) => {
    return (
      <div className="card mb-3" key={emitter.key}>
        <div className="card-header">Emitter {index + 1}</div>
        <div className="card-body">
          <div className="row">
            <div className="col mb-3">
              <label htmlFor={`emitterName-${emitter.key}`} className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
              <input type="text" maxLength={50} className="form-control" id={`emitterName-${emitter.key}`} placeholder="Office Radiator" required={true} value={emitter.name} onChange={(e) => updateEmitter(emitter.key, { name: e.target.value })} />
            </div>
            <div className="col mb-3">
              <label htmlFor={`emitterType-${emitter.key}`} className="form-label">Type <span style={{ 'color': 'red' }}>*</span></label>
              <select className="form-control" id={`emitterType-${emitter.key}`} required={true} value={emitter.type} onChange={(e) => updateEmitter(emitter.key, { type: parseInt(e.target.value) })}>
                <option value="0">Designer</option>
                <option value="1">Towel</option>
                <option value="2">Column</option>
                <option value="10">Type 10 (P1)</option>
                <option value="11">Type 11 (K1)</option>
                <option value="20">Type 20</option>
                <option value="21">Type 21 (P+)</option>
                <option value="22">Type 22 (K2)</option>
                <option value="33">Type 33 (K3)</option>
                <option value="44">Type 44 (K4)</option>
              </select>
            </div>
          </div>
          <div className="row">
            <div className="col mb-3">
              <label htmlFor={`emitterOutput-${emitter.key}`} className="form-label">Output @ Δ50 <span style={{ 'color': 'red' }}>*</span></label>
              <input type="number" className="form-control" id={`emitterOutput-${emitter.key}`} placeholder="600" required={true} value={emitter.output} onChange={(e) => updateEmitter(emitter.key, { output: e.target.value })} />
            </div>
            <div className="col mb-3">
              <label htmlFor={`emitterMqttName-${emitter.key}`} className="form-label">MQTT Name</label>
              <input type="text" maxLength={20} className="form-control" id={`emitterMqttName-${emitter.key}`} placeholder="office_radiator" required={false} value={emitter.mqttName} onChange={(e) => updateEmitter(emitter.key, { mqttName: e.target.value })} />
            </div>
          </div>
          <div className="mb-3">
            <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={true} id={`emitterFlowSensor-${emitter.key}`} selectedSensor={emitter.flowSensor} onSelectedSensorChange={(e) => updateEmitter(emitter.key, { flowSensor: e })} />
          </div>
          <div className="mb-3">
            <SensorSelect deviceType={770} title="Return Temperature Sensor" required={true} id={`emitterReturnSensor-${emitter.key}`} selectedSensor={emitter.returnSensor} onSelectedSensorChange={(e) => updateEmitter(emitter.key, { returnSensor: e })} />
          </div>
          <button type="button" className="btn btn-danger btn-sm action-button" onClick={() => removeEmitter(emitter.key)}>Remove</button>
        </div>
      </div>
    );
  });

  return (
    <>
      <h1>Add Room</h1>
      <hr />
      <form method="post" onSubmit={handleSubmit}>
        <div className="row">

          <div className="col mb-3">
            <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
            <input type="text" name="name" maxLength={20} className="form-control" id="name" placeholder="Office" required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
          </div>
          <div className="col mb-3">
            <label htmlFor="targetTemperature" className="form-label">Target Temperature <span style={{ 'color': 'red' }}>*</span></label>
            <input type="number" name="targetTemperature" maxLength={20} className="form-control" id="targetTemperature" placeholder="20" required={true} value={targetTemperature || ''} onChange={(e) => setTargetTemperature(parseInt(e.target.value))} />
          </div>
          <div className="col mb-3">
            <label htmlFor="mqttName" className="form-label">MQTT Name</label>
            <input type="text" name="mqttName" maxLength={20} className="form-control" id="mqttName" placeholder="office" required={false} value={mqttName || ''} onChange={(e) => setMqttName(e.target.value)} />
          </div>
        </div>

        <div className="mb-3">
          <SensorSelect deviceType={770} title="Room Temperature Sensor" required={true} selectedSensor={temperatureSensor || ''} onSelectedSensorChange={(e) => setTemperatureSensor(e)} />
        </div>

        <h3>Emitters <button type="button" className="btn btn-primary action-button" onClick={addEmitter}>Add Emitter</button></h3>
        <hr />

        {emitters.length === 0 && <div className="alert alert-info">This room has no emitters. Add one, or assign radiators to the room later.</div>}
        {emitterCards}

        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }} disabled={saving}>{saving ? 'Adding Room...' : 'Add Room'}</button>
        <NavLink className="btn btn-cancel" to="/rooms">Cancel</NavLink>
      </form>
    </>
  )
}

export default AddRoom;

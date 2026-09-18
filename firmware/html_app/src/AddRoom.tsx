import { useRef, useState } from "react";
import { NavLink, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";
import EmitterCard from "./EmitterCard"
import { emitterToRadiatorJson, type Emitter } from "./emitter";

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

        var radiatorJson = emitterToRadiatorJson(emitter);

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

  let emitterCards = emitters.map((emitter, index) => (
    <EmitterCard key={emitter.key} emitter={emitter} index={index} onChange={(changes) => updateEmitter(emitter.key, changes)} onRemove={() => removeEmitter(emitter.key)} />
  ));

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
          <SensorSelect deviceType={770} title="Room Temperature Sensor" id="temperatureSensor" required={true} selectedSensor={temperatureSensor || ''} onSelectedSensorChange={(e) => setTemperatureSensor(e)} />
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

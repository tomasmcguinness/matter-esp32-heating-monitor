import { useEffect, useRef, useState } from "react"
import { NavLink, useParams, useNavigate } from "react-router"
import SensorSelect from "./SensorSelect"
import EmitterCard from "./EmitterCard"
import { emitterToRadiatorJson, type Emitter } from "./emitter"

function EditRoom() {

  const { roomId } = useParams();

  const navigate = useNavigate();

  const [name, setName] = useState<string>('');
  const [targetTemperature, setTargetTemperature] = useState<number>(0);
  const [heatLossPerDegree, setHeatLossPerDegree] = useState<number>(0);
  const [temperatureSensor, setTemperatureSensor] = useState<string>('0|0');

  const [emitters, setEmitters] = useState<Emitter[]>([]);
  const [saving, setSaving] = useState(false);

  // The radiators the room had when it was loaded. Any that are no longer on the form when it is
  // saved have been removed by the user, and are deleted.
  const originalRadiatorIds = useRef<number[]>([]);

  const nextEmitterKey = useRef(1);

  useEffect(() => {
    const fetchRoom = async () => {
      const response = await fetch(`/api/rooms/${roomId}`);

      if (!response.ok) {
        return;
      }

      const data = await response.json();
      setName(data.name);
      setTargetTemperature(data.targetTemperature / 100);
      setHeatLossPerDegree(data.heatLossPerDegree);
      setTemperatureSensor(`${data.temperatureSensorNodeId}|${data.temperatureSensorEndpointId}`);

      // The room only summarises its radiators, so fetch each one for the fields the form edits.
      const radiatorIds: number[] = data.radiators.map((r: { radiatorId: number }) => r.radiatorId);

      const loaded: Emitter[] = [];

      for (const radiatorId of radiatorIds) {
        const radiatorResponse = await fetch(`/api/radiators/${radiatorId}`);

        if (!radiatorResponse.ok) {
          continue;
        }

        const radiator = await radiatorResponse.json();

        loaded.push({
          key: nextEmitterKey.current++,
          radiatorId: radiator.radiatorId,
          name: radiator.name ?? '',
          mqttName: radiator.mqttName ?? '',
          type: radiator.type,
          output: `${radiator.output}`,
          flowSensor: `${radiator.flowSensorNodeId}|${radiator.flowSensorEndpointId}`,
          returnSensor: `${radiator.returnSensorNodeId}|${radiator.returnSensorEndpointId}`
        });
      }

      originalRadiatorIds.current = loaded.map(e => e.radiatorId!);
      setEmitters(loaded);
    };

    fetchRoom();
  }, [roomId]);

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

  async function saveRoom(e: React.FormEvent<HTMLFormElement>) {
    e.preventDefault();

    setSaving(true);

    try {
      // Radiators are saved first so new ones have IDs to send with the room. Removed radiators
      // are only deleted once the room no longer refers to them, so a failure part way through
      // never loses one.
      //
      const radiatorIds: number[] = [];

      for (const emitter of emitters) {
        const body = emitterToRadiatorJson(emitter);

        if (emitter.radiatorId !== undefined) {
          const response = await fetch(`/api/radiators/${emitter.radiatorId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body });

          if (!response.ok) {
            alert(`Failed to update the emitter ${emitter.name}. The room has not been saved.`);
            return;
          }

          radiatorIds.push(emitter.radiatorId);
        } else {
          const response = await fetch('/api/radiators', { method: "POST", headers: { 'Content-Type': 'application/json' }, body });

          if (!response.ok) {
            alert(`Failed to add the emitter ${emitter.name}. The room has not been saved. Any emitters already added can be found on the Radiators page.`);
            return;
          }

          const radiator = await response.json();
          radiatorIds.push(radiator.radiatorId);
        }
      }

      const temperatureSensorNodeId = parseInt(temperatureSensor.split('|')[0]);
      const temperatureSensorEndpointId = parseInt(temperatureSensor.split('|')[1]);

      const roomJson = JSON.stringify({
        name,
        targetTemperature: targetTemperature * 100,
        heatLossPerDegree,
        radiatorIds,
        temperatureSensorNodeId,
        temperatureSensorEndpointId,
      });

      const roomResponse = await fetch(`/api/rooms/${roomId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: roomJson });

      if (!roomResponse.ok) {
        alert("Failed to update the room. Any emitters that were added can be found on the Radiators page.");
        return;
      }

      const removedRadiatorIds = originalRadiatorIds.current.filter(id => !radiatorIds.includes(id));

      for (const radiatorId of removedRadiatorIds) {
        const response = await fetch(`/api/radiators/${radiatorId}`, { method: "DELETE" });

        if (!response.ok) {
          alert(`Failed to delete a removed emitter. It can still be found on the Radiators page.`);
        }
      }

      navigate(`/rooms/${roomId}`);
    }
    finally {
      setSaving(false);
    }
  }

  const emitterCards = emitters.map((emitter, index) => (
    <EmitterCard key={emitter.key} emitter={emitter} index={index} onChange={(changes) => updateEmitter(emitter.key, changes)} onRemove={() => removeEmitter(emitter.key)} />
  ));

  return (
    <>
      <h1>Update {name}</h1>
      <hr />
      <form method="post" onSubmit={saveRoom}>
        <div className="mb-3">
          <label htmlFor="name" className="form-label">Name<span style={{ 'color': 'red' }}>*</span></label>
          <input type="string" name="name" maxLength={20} className="form-control" id="name" placeholder="Room Name e.g. Office" required={true} value={name} onChange={(e) => setName(e.target.value)} />
        </div>
        <div className="mb-3">
          <label htmlFor="targetTemperature" className="form-label">Target Temperature <span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="targetTemperature" maxLength={20} className="form-control" id="targetTemperature" placeholder="20" required={true} value={targetTemperature || ''} onChange={(e) => setTargetTemperature(parseInt(e.target.value))} />
        </div>
        <div className="mb-3">
          <label htmlFor="heatLoss" className="form-label">Heat Loss Per W/°C<span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="heatLoss" maxLength={20} className="form-control" id="heatLoss" placeholder="25" required={true} value={heatLossPerDegree || ''} onChange={(e) => setHeatLossPerDegree(parseInt(e.target.value))} />
        </div>
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Room Temperature Sensor" required={true} id="temperatureSensor" selectedSensor={temperatureSensor} onSelectedSensorChange={(e) => setTemperatureSensor(e)} />
        </div>

        <h3>Emitters <button type="button" className="btn btn-primary action-button" onClick={addEmitter}>Add Emitter</button></h3>
        <hr />

        {emitters.length === 0 && <div className="alert alert-info">This room has no emitters.</div>}
        {emitterCards}

        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }} disabled={saving}>{saving ? 'Saving...' : 'Save'}</button>
        <NavLink className="btn btn-danger" to={`/rooms/${roomId}`}>Cancel</NavLink>
      </form>
    </>
  )
}

export default EditRoom;

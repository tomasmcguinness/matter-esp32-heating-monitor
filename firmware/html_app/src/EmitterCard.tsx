import SensorSelect from "./SensorSelect";
import type { Emitter } from "./emitter";

type EmitterCardProps = {
  emitter: Emitter;
  index: number;
  onChange: (changes: Partial<Emitter>) => void;
  onRemove: () => void;
};

function EmitterCard({ emitter, index, onChange, onRemove }: EmitterCardProps) {
  return (
    <div className="card mb-3">
      <div className="card-header">Emitter {index + 1}</div>
      <div className="card-body">
        <div className="row">
          <div className="col mb-3">
            <label htmlFor={`emitterName-${emitter.key}`} className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
            <input type="text" maxLength={50} className="form-control" id={`emitterName-${emitter.key}`} placeholder="Office Radiator" required={true} value={emitter.name} onChange={(e) => onChange({ name: e.target.value })} />
          </div>
          <div className="col mb-3">
            <label htmlFor={`emitterType-${emitter.key}`} className="form-label">Type <span style={{ 'color': 'red' }}>*</span></label>
            <select className="form-control" id={`emitterType-${emitter.key}`} required={true} value={emitter.type} onChange={(e) => onChange({ type: parseInt(e.target.value) })}>
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
            <input type="number" className="form-control" id={`emitterOutput-${emitter.key}`} placeholder="600" required={true} value={emitter.output} onChange={(e) => onChange({ output: e.target.value })} />
          </div>
          <div className="col mb-3">
            <label htmlFor={`emitterMqttName-${emitter.key}`} className="form-label">MQTT Name</label>
            <input type="text" maxLength={20} className="form-control" id={`emitterMqttName-${emitter.key}`} placeholder="office_radiator" required={false} value={emitter.mqttName} onChange={(e) => onChange({ mqttName: e.target.value })} />
          </div>
        </div>
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={true} id={`emitterFlowSensor-${emitter.key}`} selectedSensor={emitter.flowSensor} onSelectedSensorChange={(e) => onChange({ flowSensor: e })} />
        </div>
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Return Temperature Sensor" required={true} id={`emitterReturnSensor-${emitter.key}`} selectedSensor={emitter.returnSensor} onSelectedSensorChange={(e) => onChange({ returnSensor: e })} />
        </div>
        <button type="button" className="btn btn-danger btn-sm action-button" onClick={onRemove}>Remove</button>
      </div>
    </div>
  );
}

export default EmitterCard;

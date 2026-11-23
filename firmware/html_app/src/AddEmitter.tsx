import { useState } from "react";
import { NavLink, useNavigate, useSearchParams } from "react-router";
import SensorSelect from "./SensorSelect";
import { UFH_MANIFOLD_TYPE } from "./emitter";

// Adds a radiator, or a UFH manifold with ?kind=ufh. Both are radiator records on the device; a
// manifold has the reserved type and no rating. Its loops are calibrated on its Edit page once it
// exists, during a heating run, so a new manifold goes straight there.
//
function AddEmitter() {

  let navigate = useNavigate();

  const [searchParams] = useSearchParams();
  const isManifold = searchParams.get("kind") === "ufh";
  const noun = isManifold ? "UFH Manifold" : "Radiator";

  const [name, setName] = useState<string | undefined>(undefined);
  const [mqttName, setMqttName] = useState<string | undefined>(undefined);
  const [type, setType] = useState(10);
  const [output, setOutput] = useState<number | undefined>(undefined);
  const [flowSensor, setFlowSensor] = useState<string | undefined>(undefined);
  const [returnSensor, setReturnSensor] = useState<string | undefined>(undefined);

  function handleSubmit(e: any) {
    e.preventDefault();

    var flowSensorNodeId = parseInt(flowSensor!.split('|')[0]);
    var flowSensorEndpointId = parseInt(flowSensor!.split('|')[1]);

    var returnSensorNodeId = parseInt(returnSensor!.split('|')[0]);
    var returnSensorEndpointId = parseInt(returnSensor!.split('|')[1]);

    var object: any = {
      name,
      mqttName: mqttName?.toLowerCase() ?? '',
      type: isManifold ? UFH_MANIFOLD_TYPE : type,
      output: output ?? 0,
      flowSensorNodeId,
      flowSensorEndpointId,
      returnSensorNodeId,
      returnSensorEndpointId
    };

    var json = JSON.stringify(object);

    fetch('/api/radiators', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: json }).then(async r => {
      if (r.ok && isManifold) {
        const { radiatorId } = await r.json();
        navigate(`/emitters/${radiatorId}/edit`);
      } else if (r.ok) {
        navigate("/emitters");
      } else {
        alert(`Failed to add ${noun.toLowerCase()}`);
      }
    });
  }

  return (
    <>
      <h1>Add {noun}</h1>
      <hr />
      <form method="post" onSubmit={handleSubmit}>
        <div className="mb-3">
          <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="name" maxLength={50} className="form-control" id="name" placeholder={isManifold ? "Ground floor UFH" : "Office"} required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
        </div>
        <div className="mb-3">
          <label htmlFor="mqttName" className="form-label">MQTT Name</label>
          <input type="text" name="mqttName" maxLength={20} className="form-control" id="mqttName" placeholder={isManifold ? "ground_floor_ufh" : "office"} required={false} value={mqttName || ''} onChange={(e) => setMqttName(e.target.value)} />
        </div>
        {!isManifold && <div className="mb-3">
          <label htmlFor="type" className="form-label">Type <span style={{ 'color': 'red' }}>*</span></label>
          <select name="type" className="form-control" id="type" required={true} value={type} onChange={(e) => setType(parseInt(e.target.value))}>
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
        </div>}
        {isManifold
          ? <div className="alert alert-info">Once it is added, enter its loops' flows on its Edit page during a heating run. Until then it has no output.</div>
          : <div className="mb-3">
            <label htmlFor="output" className="form-label">Output @ Δ50 <span style={{ 'color': 'red' }}>*</span></label>
            <input type="number" name="output" className="form-control" id="output" placeholder="600" required={true} value={output || ''} onChange={(e) => setOutput(parseInt(e.target.value))} />
          </div>}
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Flow Temperature Sensor" id="flowSensor" required={true} selectedSensor={flowSensor || ''} onSelectedSensorChange={(e) => setFlowSensor(e)} />
        </div>
        <div className="mb-3">
          <SensorSelect deviceType={770} title="Return Temperature Sensor" id="returnSensor" required={true} selectedSensor={returnSensor || ''} onSelectedSensorChange={(e) => setReturnSensor(e)} />
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add {noun}</button>
        <NavLink className="btn btn-danger" to="/emitters">Cancel</NavLink>
      </form>
    </>
  )
}

export default AddEmitter

import { useEffect, useState } from "react";
import { NavLink, useNavigate } from "react-router";

function AddRadiator() {

  let navigate = useNavigate();
  
  const [name, setName] = useState<string | undefined>(undefined);
  const [type, setType] = useState(10);
  const [output, setOutput] = useState<number | undefined>(undefined);
  const [flowSensor, setFlowSensor] = useState<string | undefined>(undefined);
  const [returnSensor, setReturnSensor] = useState<string | undefined>(undefined);

  const [sensors, setSensors] = useState<[]>([]);

  useEffect(() => {
    fetch('/api/sensors').then(response => response.json()).then(data => setSensors(data));
  }, []);

  function handleSubmit(e: any) {
    e.preventDefault();

    var flowSensorNodeId = parseInt(flowSensor!.split('|')[0]);
    var flowSensorEndpointId = parseInt(flowSensor!.split('|')[1]);

    var returnSensorNodeId = parseInt(returnSensor!.split('|')[0]);
    var returnSensorEndpointId = parseInt(returnSensor!.split('|')[1]);

    var object: any = {
      name,
      type,
      output,
      flowSensorNodeId,
      flowSensorEndpointId,
      returnSensorNodeId,
      returnSensorEndpointId
    };

    var json = JSON.stringify(object);

    fetch('/api/radiators', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate("/radiators");
      } else {
        alert("Failed to add radiator");
      }
    });
  }

  let sensorOptions = sensors.map((s: any) => {
    var key = `${s.nodeId}|${s.endpointId}`;
    return <option key={key} value={key}>{s.vendorName}/{s.productName} (0x{s.nodeId} - 0x{s.endpointId})</option>;
  });

  return (
    <>
      <h1>Add Radiator</h1>
      <hr />
      <form method="post" onSubmit={handleSubmit}>
        <div className="mb-3">
          <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="name" maxLength={20} className="form-control" id="name" placeholder="Office" required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
        </div>
        <div className="mb-3">
          <label htmlFor="type" className="form-label">Type <span style={{ 'color': 'red' }}>*</span></label>
          <select name="type" className="form-control" id="type" required={true} value={type} onChange={(e) => setType(parseInt(e.target.value))}>
            <option value="10">Type 10 (P1)</option>
            <option value="11">Type 11 (K1)</option>
            <option value="20">Type 20</option>
            <option value="21">Type 21 (P+)</option>
            <option value="22">Type 22 (K2)</option>
            <option value="33">Type 33 (K3)</option>
            <option value="44">Type 44 (K4)</option>
          </select>
        </div>
        <div className="mb-3">
          <label htmlFor="output" className="form-label">Output @ Δ50 <span style={{ 'color': 'red' }}>*</span></label>
          <input type="number" name="output" className="form-control" id="output" placeholder="600" required={true} value={output || ''} onChange={(e) => setOutput(parseInt(e.target.value))} />
        </div>
        <div className="mb-3">
          <label htmlFor="flowSensor" className="form-label">Flow Sensor <span style={{ 'color': 'red' }}>*</span></label>
          <select name="flowSensor" className="form-control" id="flowSensor" value={flowSensor || ''} onChange={(e) => setFlowSensor(e.target.value)} required={true}>
            <option></option>
            {sensorOptions}
          </select>
        </div>
        <div className="mb-3">
          <label htmlFor="returnSensor" className="form-label">Return Sensor <span style={{ 'color': 'red' }}>*</span></label>
          <select name="returnSensor" className="form-control" id="returnSensor" value={returnSensor || ''} onChange={(e) => setReturnSensor(e.target.value)} required={true}>
            <option></option>
            {sensorOptions}
          </select>
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Radiator</button>
        <NavLink className="btn btn-default" to="/radiators">Back</NavLink>
      </form>
    </>
  )
}

export default AddRadiator

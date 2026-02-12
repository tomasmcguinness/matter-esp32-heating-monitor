import { useEffect, useState } from "react";
import { NavLink, useParams, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";

function EditRadiator() {

  let { radiatorId } = useParams();

  let navigate = useNavigate();

  const [name, setName] = useState<string>('');
  const [mqttName, setMqttName] = useState<string>('');
  const [type, setType] = useState(10);
  const [output, setOutput] = useState<number>(0);
  const [flowSensor, setFlowSensor] = useState<string>('0|0');
  const [returnSensor, setReturnSensor] = useState<string>('0|0');

  useEffect(() => {
    fetch('/api/sensors').then(response => response.json()).then(_ => {
      fetch(`/api/radiators/${radiatorId}`).then(response => response.json()).then(data => {
        setName(data.name);
        setMqttName(data.mqttName);
        setType(data.type);
        setOutput(data.output);
        setFlowSensor(`${data.flowSensorNodeId}|${data.flowSensorEndpointId}`);
        setReturnSensor(`${data.returnSensorNodeId}|${data.returnSensorEndpointId}`);
      });
    });
  }, []);

  function saveRadiator(e: any) {
    e.preventDefault();

    var flowSensorNodeId = parseInt(flowSensor!.split('|')[0]);
    var flowSensorEndpointId = parseInt(flowSensor!.split('|')[1]);

    var returnSensorNodeId = parseInt(returnSensor!.split('|')[0]);
    var returnSensorEndpointId = parseInt(returnSensor!.split('|')[1]);

    var object: any = {
      name,
      mqttName,
      type,
      output,
      flowSensorNodeId,
      flowSensorEndpointId,
      returnSensorNodeId,
      returnSensorEndpointId
    };
    var json = JSON.stringify(object);

    fetch(`/api/radiators/${radiatorId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate(`/radiators/${radiatorId}`);
      } else {
        alert("Failed to update radiator");
      }
    });
  }

  return (
    <>
      <h1>Update Radiator {radiatorId}</h1>
      <hr />
      <div className="mb-3">
        <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
        <input type="text" name="name" maxLength={20} className="form-control" id="name" placeholder="Office" required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
      </div>
       <div className="mb-3">
          <label htmlFor="mqttName" className="form-label">MQTT Name</label>
          <input type="text" name="mqttName" maxLength={20} className="form-control" id="mqttName" placeholder="office" required={false} value={mqttName || ''} onChange={(e) => setMqttName(e.target.value)} />
        </div>
      <div className="mb-3">
        <label htmlFor="type" className="form-label">Type <span style={{ 'color': 'red' }}>*</span></label>
        <select name="type" className="form-control" id="type" required={true} value={type} onChange={(e) => setType(parseInt(e.target.value))}>
          <option value="0">Designer</option>
          <option value="1">Towel</option>
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
        <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={true} selectedSensor={flowSensor || ''} onSelectedSensorChange={(e) => setFlowSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Return Temperature Sensor" required={true} selectedSensor={returnSensor || ''} onSelectedSensorChange={(e) => setReturnSensor(e)} />
      </div>
      <button type="submit" className="btn btn-primary" onClick={saveRadiator} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/radiators/${radiatorId}`}>Cancel</NavLink>
    </>
  )
}

export default EditRadiator

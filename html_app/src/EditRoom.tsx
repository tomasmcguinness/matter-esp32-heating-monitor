import { useEffect, useState } from "react";
import { NavLink, useParams } from "react-router";

function EditRadiator() {

   let { radiatorId } = useParams();
   
  const [name, setName] = useState<string | undefined>(undefined);
  const [type, setType] = useState(10);
  const [output, setOutput] = useState<number | undefined>(undefined);

  useEffect(() => {
    fetch('/api/sensors').then(response => response.json());
  }, []);

  function handleSubmit(e: any) {
    e.preventDefault();

    var object: any = {
      name,
      type,
      output
    };
    var json = JSON.stringify(object);

    fetch(`/api/radiators/${radiatorId}`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json });
  }

  return (
    <>
      <h1>Update Radiator {radiatorId}</h1>
      <hr />
      <form onSubmit={handleSubmit}>
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
          <select name="flowSensor" className="form-control" id="flowSensor">
          </select>
        </div>
        <div className="mb-3">
          <label htmlFor="returnSensor" className="form-label">Return Sensor <span style={{ 'color': 'red' }}>*</span></label>
          <select name="returnSensor" className="form-control" id="returnSensor">          </select>
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Radiator</button>
        <NavLink className="btn btn-default" to="/radiators">Back</NavLink>
      </form>
    </>
  )
}

export default EditRadiator

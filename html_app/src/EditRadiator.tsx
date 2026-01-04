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

  function saveRadiator(e: any) {
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
      <div className="mb-3">
        <label htmlFor="output" className="form-label">Output @ Δ50 <span style={{ 'color': 'red' }}>*</span></label>
        <input type="number" name="output" className="form-control" id="output" placeholder="600" required={true} value={output || ''} onChange={(e) => setOutput(parseInt(e.target.value))} />
      </div>
      <button type="submit" className="btn btn-primary" onClick={saveRadiator} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to="/radiators">Cancel</NavLink>
    </>
  )
}

export default EditRadiator

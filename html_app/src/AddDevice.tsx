import { useState } from "react";
import { NavLink } from "react-router";

function AddDevice() {

  const [inUse, setInUse] = useState<string | undefined>(undefined);
  const [setupCode, setSetupCode] = useState<string | undefined>(undefined);

  function handleSubmit(e: any) {
    e.preventDefault();

    var object: any = {
      inUse: Boolean(inUse),
      setupCode,
    };

    var json = JSON.stringify(object);

    fetch('/api/nodes', { method: "POST", headers: { 'Content-Type': 'application/json' }, body: json });
  }

  return (
    <>
      <h1>Add Device</h1>
      <hr />
      <form method="post" onSubmit={handleSubmit}>
        <div className="mb-3">
          <label htmlFor="inUse" className="form-label">Device In Use? <span style={{ 'color': 'red' }}>*</span></label>
          <select name="inUse" className="form-control" id="inUse" required={true} value={inUse ? "true" : "false"} onChange={(e) => setInUse(e.target.value)}>
            <option></option>
            <option value="false">No, it's new</option>
            <option value="true">Yes, it's in use</option>
          </select>
        </div>
        <div className="mb-3">
          <label htmlFor="setupCode" className="form-label">Manual Setup Code <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="setupCode" className="form-control" id="setupCode" placeholder="1111-111-1111" required={true} value={setupCode || ''} onChange={(e) => setSetupCode(e.target.value)} />
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Device</button>
        <NavLink className="btn btn-default" to="/devices">Back</NavLink>
      </form>
    </>
  )
}

export default AddDevice

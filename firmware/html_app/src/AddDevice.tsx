import { useState } from "react";
import { NavLink } from "react-router";

function AddDevice() {

  const [setupCode, setSetupCode] = useState<string | undefined>(undefined);

  function handleSubmit(e: any) {
    e.preventDefault();

    var object: any = {
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
        <div className="alert alert-primary" role="alert">
          <h4 className="alert-heading">On Network Commissioning</h4>
          <p>Only devices that are already on the network can be commissioned using this method. Use the companion app for new devices.</p>
        </div>
        <div className="mb-3">
          <label htmlFor="setupCode" className="form-label">Setup Code <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="setupCode" className="form-control" id="setupCode" placeholder="1111-111-1111" required={true} value={setupCode || ''} onChange={(e) => setSetupCode(e.target.value)} />
          <div className="form-text">An 11-digit manual pairing code, or the <code>MT:</code> payload from the device's QR code.</div>
        </div>
        <button type="submit" className="btn btn-primary" style={{ 'marginRight': '5px' }}>Add Device</button>
        <NavLink className="btn btn-default" to="/devices">Back</NavLink>
      </form>
    </>
  )
}

export default AddDevice

import { useEffect, useState } from "react";
import { NavLink, useParams, useNavigate } from "react-router";

function EditDevice() {

  const { nodeId } = useParams();

  const navigate = useNavigate();

  const [name, setName] = useState<string | undefined>(undefined);

  useEffect(() => {
    const fetchDevice = async () => {
      var response = await fetch(`/api/nodes/${nodeId}`);

      if (response.ok) {
        let data = await response.json();
        setName(data.name);
      }
    };

    fetchDevice();
  }, []);

  function save(e: any) {
    e.preventDefault();

    var object: any = {
      name
    };
    var json = JSON.stringify(object);

    fetch(`/api/nodes/${nodeId}/update`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate(`/devices/${nodeId}`);
      } else {
        alert("Failed to update home");
      }
    });
  }

  return (
    <>
      <h1>Edit Device</h1>
      <hr />
      <div className="mb-3">
          <label htmlFor="name" className="form-label">Name <span style={{ 'color': 'red' }}>*</span></label>
          <input type="text" name="name" maxLength={20} className="form-control" id="name" placeholder="Office" required={true} value={name || ''} onChange={(e) => setName(e.target.value)} />
        </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/devices/${nodeId}`}>Cancel</NavLink>
    </>
  )
}

export default EditDevice;

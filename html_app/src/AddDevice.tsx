import { NavLink } from "react-router";

function Devices() {

  function handleSubmit(e: any) {
    e.preventDefault();

    const form = e.target;
    const formData = new FormData(form);

    var object: any = {};
    formData.forEach((value, key) => object[key] = value);
    var json = JSON.stringify(object);

    fetch('/nodes', { method: form.method, headers: { 'Content-Type': 'application/json' }, body: json  });
  }

  return (
    <>
      <h1>Add Device</h1>
      <form method="post" onSubmit={handleSubmit}>
        <div className="mb-3">
          <label htmlFor="setupCode" className="form-label">Manual Setup Code</label>
          <input type="text" name="setupCode" className="form-control" id="setupCode" placeholder="1111-111-1111" required={true} />
        </div>
        <button type="submit" className="btn btn-primary" style={{'marginRight':'5px'}}>Add Device</button>
        <NavLink className="btn btn-default" to="/devices">Back</NavLink>
      </form>
    </>
  )
}

export default Devices

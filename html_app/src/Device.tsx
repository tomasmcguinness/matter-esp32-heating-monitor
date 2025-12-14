import { NavLink, useParams } from "react-router"

function Device() {

  let { nodeId } = useParams();

  const removeNode = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this node?");

    if (confirm) {
      await fetch(`/api/nodes/${nodeId}`, { method: 'DELETE' });
    }
  }

  return (
    <>
      <h1>Device - {nodeId}</h1>
      <hr />
      <div className="alert alert-info" role="alert">
        <h4 className="alert-heading">Device Details</h4>
        <p>Nothing happening on this page yet, it's just here so a device can be removed from the Heating Monitor.</p>
      </div>
      <button className="btn btn-danger" onClick={removeNode} style={{ 'marginRight': '5px' }}>Remove Node</button>
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

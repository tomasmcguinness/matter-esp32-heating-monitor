import { NavLink, useParams } from "react-router"

function Device() {

  let { nodeId } = useParams();

  const refreshNodeDetails = async () => {
    await fetch(`/nodes/${nodeId}/refresh`, { method: 'POST' });
  }

  const removeNode = async () => {
    await fetch(`/nodes/${nodeId}`, { method: 'DELETE' });
  }

  return (
    <>
      <h1>Device - {nodeId}</h1>
      <hr />
      <button className="btn btn-danger" onClick={removeNode}>Remove Node</button>
      <button className="btn btn-primary" onClick={refreshNodeDetails}>Refresh Details</button>
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

import { NavLink, useParams } from "react-router"

function Device() {

  let { nodeId } = useParams();

  const removeNode = async () => {
    await fetch(`/nodes/${nodeId}`, { method: 'DELETE' });
  }

  return (
    <>
      <h1>Device - {nodeId}</h1>
      <hr />
      <button className="btn btn-danger" onClick={removeNode} style={{'marginRight':'5px'}}>Remove Node</button>
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

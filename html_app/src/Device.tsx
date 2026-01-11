import { NavLink, useParams } from "react-router"

function Device() {

  let { nodeId } = useParams();

  const removeNode = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this node?");

    if (confirm) {
      await fetch(`/api/nodes/${nodeId}`, { method: 'DELETE' });
    }
  }

  const interviewNode = async () => {
    await fetch(`/api/nodes/${nodeId}/interview`, { method: 'PUT' });
  }

  const identifyNode = async () => {
    await fetch(`/api/nodes/${nodeId}/identify`, { method: 'PUT' });
  }

  return (
    <>
      <h1>Device - {nodeId}
        <button className="btn btn-danger action-button" onClick={removeNode} style={{ 'marginRight': '5px' }}>
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" className="bi bi-trash3" viewBox="0 0 16 16">
            <path d="M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5M11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5zm-7.487 1a.5.5 0 0 1 .528.47l.5 8.5a.5.5 0 0 1-.998.06L5 5.03a.5.5 0 0 1 .47-.53Zm5.058 0a.5.5 0 0 1 .47.53l-.5 8.5a.5.5 0 1 1-.998-.06l.5-8.5a.5.5 0 0 1 .528-.47M8 4.5a.5.5 0 0 1 .5.5v8.5a.5.5 0 0 1-1 0V5a.5.5 0 0 1 .5-.5" />
          </svg>
        </button>
        <button className="btn btn-primary action-button" onClick={interviewNode} style={{ 'marginRight': '5px' }}>Interview</button>
      </h1>
      <hr />
      <div className="alert alert-info" role="alert">
        <h4 className="alert-heading">Device Details</h4>
        <p>Nothing happening on this page yet, it's just here so a device can be removed from the Heating Monitor.</p>
      </div>
      <button className="btn btn-primary" onClick={identifyNode} style={{ 'marginRight': '5px' }}>Identify</button>
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

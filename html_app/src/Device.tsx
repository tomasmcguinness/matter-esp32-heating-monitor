import { NavLink, useNavigate, useParams } from "react-router"

function Device() {

  let { nodeId } = useParams();

  let navigate = useNavigate();

  const removeNode = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this node?");

    if (confirm) {
      await fetch(`/api/nodes/${nodeId}`, { method: 'DELETE' }).then(_ => navigate('/devices'));;
    }
  }

  const interviewNode = async () => {
    const  confirmed = confirm("Are you sure you want to interview this device again?");

    if(confirmed) {
      await fetch(`/api/nodes/${nodeId}/interview`, { method: 'PUT' });
    }
  }

  const identifyNode = async () => {
    await fetch(`/api/nodes/${nodeId}/identify`, { method: 'PUT' });
  }

  const subscribeToNode = async () => {
    await fetch(`/api/nodes/${nodeId}/subscribe`, { method: 'PUT' });
  }

  return (
    <>
      <h1>Device - {(nodeId as any)?.toString(64).toUpperCase()}
        <NavLink className="btn btn-primary action-button" to={`/devices/${nodeId}/edit`}>Edit</NavLink>
        <button className="btn btn-danger action-button" onClick={removeNode} style={{ 'marginRight': '5px' }}>
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" className="bi bi-trash3" viewBox="0 0 16 16">
            <path d="M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5M11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5zm-7.487 1a.5.5 0 0 1 .528.47l.5 8.5a.5.5 0 0 1-.998.06L5 5.03a.5.5 0 0 1 .47-.53Zm5.058 0a.5.5 0 0 1 .47.53l-.5 8.5a.5.5 0 1 1-.998-.06l.5-8.5a.5.5 0 0 1 .528-.47M8 4.5a.5.5 0 0 1 .5.5v8.5a.5.5 0 0 1-1 0V5a.5.5 0 0 1 .5-.5" />
          </svg>
        </button>
      </h1>
      <hr />
      <div className="alert alert-info" role="alert">
        <h4 className="alert-heading">Identify</h4>
        <p>If you're not sure which device is which, you can ask a device to identiy itself. This might blink an LED or play a sound.</p>
        <button className="btn btn-primary" onClick={identifyNode} style={{ 'marginRight': '5px' }}>Identify</button>
      </div>
      <div className="alert alert-danger" role="alert">
        <h4 className="alert-heading">Interview</h4>
        <p>If a device has changed since it was added, you can perform an Interview. This may break your room & radiator sensor setup if endpoints change.</p>
        <button className="btn btn-danger" onClick={interviewNode} style={{ 'marginRight': '5px' }}>Interview</button>
      </div>
       <div className="alert alert-danger" role="alert">
        <h4 className="alert-heading">Subscribe</h4>
        <p>A development tool to reinstate subscriptions.</p>
        <button className="btn btn-danger" onClick={subscribeToNode} style={{ 'marginRight': '5px' }}>Subscribe</button>
      </div>
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

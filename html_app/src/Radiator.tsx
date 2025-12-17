import { NavLink, useNavigate, useParams } from "react-router"

function Radiator() {

  let navigate = useNavigate();

  let { radiatorId } = useParams();

  const removeRadiator = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this radiator?");

    if (confirm) {
      await fetch(`/api/radiators/${radiatorId}`, { method: 'DELETE' }).then(_ => navigate('/radiators'));
    }
  }

  return (
    <>
      <h1>Radiator {radiatorId}</h1>
      <hr />
      <div className="alert alert-info" role="alert">
        <h4 className="alert-heading">Radiator Details</h4>
        <p>Nothing happening on this page yet, it's just here so a radiator can be removed from the Heating Monitor.</p>
      </div>
      <button className="btn btn-danger" onClick={removeRadiator} style={{ 'marginRight': '5px' }}>Remove</button>
      <NavLink className="btn btn-default" to="/radiators">Back</NavLink>
    </>
  )
}

export default Radiator

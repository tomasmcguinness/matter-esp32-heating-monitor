import { NavLink, useParams } from "react-router"

function Device() {

   let { nodeId } = useParams();

  return (
    <>
      <h1>Device - {nodeId}</h1>
      <hr />
      <NavLink className="btn btn-default" to="/devices">Back</NavLink>
    </>
  )
}

export default Device

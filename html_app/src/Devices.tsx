import { NavLink } from "react-router"

function Devices() {

  return (
    <>
    <h1>Devices!</h1>
    <NavLink className="nav-link" to="/devices/add">Add Device</NavLink>
    </>
  )
}

export default Devices

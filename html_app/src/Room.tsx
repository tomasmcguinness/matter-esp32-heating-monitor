import { NavLink, useNavigate, useParams } from "react-router"

function Room() {

  let navigate = useNavigate();

  let { roomId } = useParams();

  const removeRoom = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this room?");

    if (confirm) {
      await fetch(`/api/rooms/${roomId}`, { method: 'DELETE' }).then(_ => navigate('/rooms'));
    }
  }

  return (
    <>
      <h1>Room {roomId}</h1>
      <hr />
      <button className="btn btn-danger" onClick={removeRoom} style={{ 'marginRight': '5px' }}>Remove</button>
      <NavLink className="btn btn-default" to="/rooms">Back</NavLink>
    </>
  )
}

export default Room;

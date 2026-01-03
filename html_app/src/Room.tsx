import { useEffect, useState } from "react";
import { NavLink, useNavigate, useParams } from "react-router"
import Temperature from "./Temperature";

function Room() {

  let navigate = useNavigate();

  let { roomId } = useParams();

  let [room, setRoom] = useState<any>(null);

  const removeRoom = async () => {
    let confirm: boolean = window.confirm("Are you sure you want to remove this room?");

    if (confirm) {
      await fetch(`/api/rooms/${roomId}`, { method: 'DELETE' }).then(_ => navigate('/rooms'));
    }
  }

  useEffect(() => {
    const fetchRoom = async () => {
      var response = await fetch(`/api/rooms/${roomId}`);

      if (response.ok) {
        let data = await response.json();
        setRoom(data);
      }
    };

    fetchRoom();
  }, [roomId]);

  if (!room) {
    return <span>Loading...</span>;
  }

  let radiatorRows = room.radiators.map((r: any) => {
    return (<tr key={r.radiatorId} onClick={() => navigate(`/radiators/${r.radiatorId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{r.radiatorId}</td>
      <td>{r.name}</td>
      <td>{r.type}</td>
      <td><Temperature>{r.flowTemp}</Temperature></td>
      <td><Temperature>{r.returnTemp}</Temperature></td>
      <td>TBD</td>
    </tr>);
  });

  return (
    <>
      <h1>{room.name} <NavLink className="btn btn-primary action-button" to={`/rooms/${roomId}/edit`}>Edit</NavLink></h1>
      <hr />

      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><Temperature>{room.temperature}</Temperature></p>
          </div>
        </div>
      </div>

      <div className="card" style={{ marginBottom: '5px' }}>
        <div className="card-header">
          Radiators
        </div>
        <div className="card-body">
          <table className="table table-striped table-bordered">
            <thead>
              <tr>
                <th>ID</th>
                <th>Name</th>
                <th>Type</th>
                <th>Flow</th>
                <th>Return</th>
                <th>Output</th>
              </tr>
            </thead>
            <tbody>
              {radiatorRows}
            </tbody>
          </table>
        </div>
      </div>

      <button className="btn btn-danger" onClick={removeRoom} style={{ 'marginRight': '5px' }}>Remove</button>
      <NavLink className="btn btn-default" to="/rooms">Back</NavLink>
    </>
  )
}

export default Room;

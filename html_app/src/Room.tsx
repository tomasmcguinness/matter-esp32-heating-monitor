import { useEffect, useState } from "react";
import { NavLink, useNavigate, useParams } from "react-router"
import Temperature from "./Temperature";
import Power from "./Power";

function Room() {

  let navigate = useNavigate();

  let { roomId } = useParams();

  let [room, setRoom] = useState<any>(null);

  const deleteRoom = async () => {
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
      <td><Power>{r.currentOutput}</Power></td>
    </tr>);
  });

  return (
    <>
      <h1>{room.name} 
        <NavLink className="btn btn-primary action-button" to={`/rooms/${roomId}/edit`}>Edit</NavLink>
        <button className="btn btn-danger action-button" onClick={deleteRoom} style={{ 'marginRight': '5px' }}>
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" className="bi bi-trash3" viewBox="0 0 16 16">
            <path d="M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5M11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5zm-7.487 1a.5.5 0 0 1 .528.47l.5 8.5a.5.5 0 0 1-.998.06L5 5.03a.5.5 0 0 1 .47-.53Zm5.058 0a.5.5 0 0 1 .47.53l-.5 8.5a.5.5 0 1 1-.998-.06l.5-8.5a.5.5 0 0 1 .528-.47M8 4.5a.5.5 0 0 1 .5.5v8.5a.5.5 0 0 1-1 0V5a.5.5 0 0 1 .5-.5" />
          </svg>
        </button>
      </h1>
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
        <div className="card">
          <div className="card-header">
            Heat Loss W/°C
          </div>
          <div className="card-body">
            <p className="card-title"><Power>{room.heatLossPerDegree}</Power></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Heat Loss W
          </div>
          <div className="card-body">
            <p className="card-title"><Power>{room.heatLoss}</Power></p>
          </div>
        </div>
      </div>

      <div className="card" style={{ marginBottom: '5px' }}>
        <div className="card-header">
          Radiators
        </div>
        <div className="card-body">
          {radiatorRows.length == 0 && <div className="alert alert-info">No radiators have been assigned to this room</div>}
          {radiatorRows.length > 0 &&
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
          }
          </div>
      </div>

      <NavLink className="btn btn-default" to="/rooms">Back</NavLink>
    </>
  )
}

export default Room;

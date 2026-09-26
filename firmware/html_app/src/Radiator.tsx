import { useEffect, useState } from "react";
import { NavLink, useNavigate, useParams } from "react-router"
import Temperature from "./Temperature";
import Power from "./Power";
import RadiatorTodayChart from "./RadiatorTodayChart";

// GET /api/radiators/:id. Temperatures are 0.01 degC. The room fields are null when the radiator
// has not been put in a room.
type RadiatorDetail = {
  radiatorId: number;
  name: string;
  output: number;
  flowTemp: number;
  returnTemp: number;
  meanWaterTemperature: number;
  currentOutput: number;
  roomId: number | null;
  roomName: string | null;
  roomTemperature: number | null;
};

function Radiator() {

  const navigate = useNavigate();

  const { radiatorId } = useParams();

  const [radiator, setRadiator] = useState<RadiatorDetail | null>(null);

  const removeRadiator = async () => {
    const confirm: boolean = window.confirm("Are you sure you want to remove this radiator?");

    if (confirm) {
      await fetch(`/api/radiators/${radiatorId}`, { method: 'DELETE' }).then(() => navigate('/radiators'));
    }
  }

  useEffect(() => {
    const fetchRadiator = async () => {
      const response = await fetch(`/api/radiators/${radiatorId}`);

      if (response.ok) {
        const data: RadiatorDetail = await response.json();
        setRadiator(data);
      }
    };

    fetchRadiator();
  }, [radiatorId]);

  if (!radiator) {
    return <span>Loading...</span>;
  }

  return (
    <>
      <h1>{radiator.name}
        <NavLink className="btn btn-primary action-button" to={`/radiators/${radiatorId}/edit`}>Edit</NavLink>
        <button className="btn btn-danger action-button" onClick={removeRadiator} style={{ 'marginRight': '5px' }}>
          <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" className="bi bi-trash3" viewBox="0 0 16 16">
            <path d="M6.5 1h3a.5.5 0 0 1 .5.5v1H6v-1a.5.5 0 0 1 .5-.5M11 2.5v-1A1.5 1.5 0 0 0 9.5 0h-3A1.5 1.5 0 0 0 5 1.5v1H1.5a.5.5 0 0 0 0 1h.538l.853 10.66A2 2 0 0 0 4.885 16h6.23a2 2 0 0 0 1.994-1.84l.853-10.66h.538a.5.5 0 0 0 0-1zm1.958 1-.846 10.58a1 1 0 0 1-.997.92h-6.23a1 1 0 0 1-.997-.92L3.042 3.5zm-7.487 1a.5.5 0 0 1 .528.47l.5 8.5a.5.5 0 0 1-.998.06L5 5.03a.5.5 0 0 1 .47-.53Zm5.058 0a.5.5 0 0 1 .47.53l-.5 8.5a.5.5 0 1 1-.998-.06l.5-8.5a.5.5 0 0 1 .528-.47M8 4.5a.5.5 0 0 1 .5.5v8.5a.5.5 0 0 1-1 0V5a.5.5 0 0 1 .5-.5" />
          </svg>
        </button>
      </h1>
      <hr />

      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Flow
          </div>
          <div className="card-body">
            <h3 className="card-title"><Temperature>{radiator.flowTemp}</Temperature></h3>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Return
          </div>
          <div className="card-body">
            <h3 className="card-title"><Temperature>{radiator.returnTemp}</Temperature></h3>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Mean Water Temperature
          </div>
          <div className="card-body">
            <h3 className="card-title"><Temperature>{radiator.meanWaterTemperature}</Temperature></h3>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Output
          </div>
          <div className="card-body">
            <h3 className="card-title"><Power>{radiator.currentOutput}</Power></h3>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            {radiator.roomId === null ? 'Room' : <NavLink to={`/rooms/${radiator.roomId}`}>{radiator.roomName}</NavLink>}
          </div>
          <div className="card-body">
            {radiator.roomId === null
              ? <span className="text-muted">Not in a room</span>
              : <h3 className="card-title"><Temperature>{radiator.roomTemperature}</Temperature></h3>}
          </div>
        </div>
      </div>

      <RadiatorTodayChart
        radiatorId={radiator.radiatorId}
        name={radiator.name}
        ratedW={radiator.output}
        roomId={radiator.roomId}
      />

      <NavLink className="btn btn-default" to="/radiators">Back</NavLink>
    </>
  )
}

export default Radiator

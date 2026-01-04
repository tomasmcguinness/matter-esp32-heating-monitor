import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';

function Radiators() {

  let navigate = useNavigate();

  let [radiatorList, setRadiatorList] = useState<any>([]);

  const {subscribe, unsubscribe} = useContext(WebSocketContext);

  useEffect(() => {

    subscribe("radiator", (message: any) => {

      const updatedRadiatorList = [...radiatorList];
      const radiator = updatedRadiatorList.find(a => a.radiatorId === message.radiatorId);

      if (radiator) {

        if (message.hasOwnProperty("flowTemp")) {
          radiator.flowTemp = message.flowTemp;
        } else {
          radiator.returnTemp = message.returnTemp;
        }

        setRadiatorList(updatedRadiatorList);
      } else {
        console.log('Could not find a radiator with id ' + message.radiatorId);
      }

    })

    return () => {
      unsubscribe("radiator")
    }
  }, [subscribe, unsubscribe])

  useEffect(() => {
    const fetchRadiators = async () => {
      var response = await fetch("/api/radiators");

      if (response.ok) {
        let data = await response.json();
        setRadiatorList(data);
      }
    };

    fetchRadiators();
  }, []);

  let radiators = radiatorList.map((n: any) => {
    return (<tr key={n.radiatorId} onClick={() => navigate(`/radiators/${n.radiatorId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.radiatorId}</td>
      <td>{n.name}</td>
      <td>{n.type}</td>
      <td>{n.output}</td>
      <td><Temperature>{n.flowTemp}</Temperature></td>
      <td><Temperature>{n.returnTemp}</Temperature></td>
      <td>{n.currentOutput}</td>
    </tr>);
  });

  return (
    <>
      <h1>Radiators</h1>
      <hr />
      {radiators.length === 0 && <div className="alert alert-info">There are no radiators. Add one!</div>}
      {radiators.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th style={{ width: 'auto' }}>ID</th>
            <th>Name</th>
            <th>Type</th>
            <th>@ΔT 50°C</th>
            <th>Flow</th>
            <th>Return</th>
            <th>Current</th>
          </tr>
        </thead>
        <tbody>
          {radiators}
        </tbody>
      </table>
      }
      <NavLink className="btn btn-primary" to="/radiators/add">Add Radiator</NavLink>
    </>
  )
}

export default Radiators

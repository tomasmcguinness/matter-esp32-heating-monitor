import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';
import Power from "./Power.tsx";
import RadiatorType from "./RadiatorType.tsx";

function Radiators() {

  let navigate = useNavigate();

  let [radiatorList, setRadiatorList] = useState<any>([]);

  const {subscribe, unsubscribe} = useContext(WebSocketContext);

  useEffect(() => {

    console.log("Subscribing to channel radiator");

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

  let radiators = radiatorList.sort((a: any, b: any) => a.radiatorId > b.radiatorId ? 1 : -1).map((n: any) => {
    return (<tr key={n.radiatorId} onClick={() => navigate(`/radiators/${n.radiatorId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.name}</td>
      <td><RadiatorType>{n.type}</RadiatorType></td>
      <td><Temperature>{n.flowTemp}</Temperature></td>
      <td><Temperature>{n.returnTemp}</Temperature></td>
      <td><Temperature>{Math.abs(n.flowTemp - n.returnTemp)}</Temperature></td>
      <td><Power>{n.currentOutput}</Power></td>
    </tr>);
  });

  return (
    <>
      <h1>Radiators <NavLink className="btn btn-primary action-button" to="/radiators/add">Add Radiator</NavLink> </h1>
      <hr />
      {radiators.length === 0 && <div className="alert alert-info">There are no radiators. Add one!</div>}
      {radiators.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>Name</th>
            <th>Type</th>
            <th>Flow</th>
            <th>Return</th>
            <th>ΔT</th>
            <th>Output</th>
          </tr>
        </thead>
        <tbody>
          {radiators}
        </tbody>
      </table>
      }
    </>
  )
}

export default Radiators

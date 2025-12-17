import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import useWebSocket from 'react-use-websocket';

function Radiators() {

  let navigate = useNavigate();

  let [radiatorList, setRadiatorList] = useState<any>([]);

  const socketUrl = 'ws://192.168.1.104/ws';

  const {
    sendJsonMessage,
    lastJsonMessage,
  } = useWebSocket(socketUrl, {
    onOpen: () => { sendJsonMessage({ radiators: true }); console.log('opened'); },
    shouldReconnect: (_) => true,
    share: true
  });

  useEffect(() => {
    console.log("Web socket data has changed...." + lastJsonMessage);

    if (!lastJsonMessage) {
      return;
    }

    const updatedRadiatorList = [...radiatorList];
    const radiator = updatedRadiatorList.find(a => a.radiatorId === (lastJsonMessage as any).radiatorId);

    if (radiator) {

      if (lastJsonMessage.hasOwnProperty("flowTemp")) {
        radiator.flowTemp = (lastJsonMessage as any).flowTemp;
      } else {
        radiator.returnTemp = (lastJsonMessage as any).returnTemp;
      }

      setRadiatorList(updatedRadiatorList);
    } else {
      console.log('Could not find a radiator with id ' + (lastJsonMessage as any).radiatorId);
    }
  }, [lastJsonMessage]);

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
    let flowTemp: string = "-";

    if(n.flowTemp) {
      flowTemp = (n.flowTemp/100).toFixed(1) + "°C";
    }

    let returnTemp: string = "-";

    if(n.returnTemp) {
      returnTemp = (n.returnTemp/100).toFixed(1) + "°C";
    }

    return (<tr key={n.radiatorId} onClick={() => navigate(`/radiators/${n.radiatorId}`)} style={{ 'cursor': 'pointer' }}><td>{n.radiatorId}</td><td>{n.name}</td><td>{n.type}</td><td>{n.output}</td><td>{flowTemp}</td><td>{returnTemp}</td></tr>);
  });

  return (
    <>
      <h1>Radiators</h1>
      <hr />
      <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th style={{ width: 'auto' }}>ID</th>
            <th>Name</th>
            <th>Type</th>
            <th>Output</th>
            <th>Flow</th>
            <th>Return</th>
          </tr>
        </thead>
        <tbody>
          {radiators}
        </tbody>
      </table>
      <NavLink className="btn btn-primary" to="/radiators/add">Add Radiator</NavLink>
    </>
  )
}

export default Radiators

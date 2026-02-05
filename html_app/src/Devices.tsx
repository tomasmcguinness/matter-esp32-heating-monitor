import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import PowerSource from "./PowerSource"
import CheckMark from "./CheckMark";
import Temperature from "./Temperature";

function Devices() {

  let navigate = useNavigate();

  let [nodeList, setNodeList] = useState<any>([]);

  useEffect(() => {
    const fetchNodes = async () => {
      var response = await fetch("/api/nodes");

      if (response.ok) {
        let data = await response.json();
        setNodeList(data);
      }
    };

    fetchNodes();
  }, []);

  let nodes = nodeList.sort((a: any, b: any) => a.nodeId > b.nodeId ? 1 : -1).map((n: any) => {

    let endpoints = n.endpoints.sort((a: any, b: any) => a.endpointId > b.endpointId ? 1 : -1).map((endpoint: any) => {

      const deviceTypes = endpoint.deviceTypes.map((dt: number) => {

        var name = dt.toString();

        switch (dt) {
          case 777:
            name = "Heat Pump";
            break;
          case 1296:
            name = "Electrical Sensor";
            break;
          case 17:
            name = "Power Source";
            break;
          case 19:
            name = "Bridged Device";
            break;
          case 14:
            name = "Aggregator";
            break;
          case 15:
            name = "Generic Switch";
            break;
          case 266:
            name = "On/Off Plug-in Unit";
            break;
          case 769:
            name = "Thermostat";
            break;
          case 770:
            name = "Temperature Sensor";
            break;
          case 773:
            name = "Pressure Sensor";
            break;
          case 775:
            name = "Humidity Sensor";
            break;
          case 1293:
            name = "Device Energy Manager";
            break;
          case 117:
            name = "Dishwasher";
            break;
          case 269:
            name = "Extended Color Light";
            break;
        }

        return (<span key={dt} className="badge bg-primary" style={{ marginRight: '5px' }}>{name}</span>)
      });

      return (<tr>
        <td>{endpoint.endpointId}</td>
        <td>{endpoint.endpointName}</td>
        <td>{deviceTypes}</td>
        <td><Temperature>{endpoint.measuredValue}</Temperature></td>
        <td><PowerSource powerSource={endpoint.powerSource} /></td>
        </tr>)
    });

    return ([<tr key={n.nodeId} onClick={() => navigate(`/devices/${n.nodeId}`)} style={{ 'cursor': 'pointer' }}>
      <td>0x{n.nodeId.toString(16).toUpperCase()}</td>
      <td>{n.nodeName}</td>
      <td>{n.vendorName}</td>
      <td>{n.productName}</td>
      <td><PowerSource powerSource={n.powerSource} /></td>
      <td>{n.hasSubscription ? <CheckMark /> : <></>}</td>
    </tr>,
    <tr>
      <td colSpan={6}>
        <table className="table">
          <thead>
            <tr>
              <th>NodeID</th>
              <th>Name</th>
              <th>Devices</th>
              <th>Measured Value</th>
              <th/>
            </tr>
          </thead>
          <tbody>
            {endpoints}
          </tbody>
        </table>
      </td>
    </tr>]);
  });

  return (
    <>
      <h1>Devices <NavLink className="btn btn-primary action-button" to="/devices/add">Add Device</NavLink></h1>
      <hr />
      {nodes.length === 0 && <div className="alert alert-info">There are no devices. Add one!</div>}
      {nodes.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>Endpoint</th>
            <th>Name</th>
            <th>Vendor</th>
            <th>Product</th>
            <th>Power</th>
            <th>Sub</th>
          </tr>
        </thead>
        <tbody>
          {nodes}
        </tbody>
      </table >}
    </>
  )
}

export default Devices

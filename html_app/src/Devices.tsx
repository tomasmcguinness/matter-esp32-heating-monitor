import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"

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

    let endpoints = n.endpoints.map((endpoint: any) => {

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
          case 15:
            name = "Generic Switch";
            break;
          case 769:
            name = "Thermostat";
            break;
          case 770:
            name = "Temperature Sensor";
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

      return (<tr><td>{endpoint.endpointId}</td><td></td><td>{deviceTypes}</td></tr>)
    });

    return ([<tr key={n.nodeId} onClick={() => navigate(`/devices/${n.nodeId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.nodeId.toString(16)}</td>
      <td>{n.vendorName}</td>
      <td>{n.productName}</td>
      <td>{n.nodeLabel}</td>
    </tr>,


    <tr>
      <td colSpan={4}>
        <table className="table">
          <thead>
            <tr>
              <td>ID</td>
              <td>Label</td>
              <td>Devices</td>
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
      <h1>Devices</h1>
      <hr />
      {nodes.length === 0 && <div className="alert alert-info">There are no devices. Add one!</div>}
      {nodes.length > 0 && <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>ID</th>
            <th>Vendor</th>
            <th>Product</th>
            <th>Label</th>
          </tr>
        </thead>
        <tbody>
          {nodes}
        </tbody>
      </table >}
      <NavLink className="btn btn-primary" to="/devices/add">Add Device</NavLink>
    </>
  )
}

export default Devices

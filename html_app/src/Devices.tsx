import { useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
function Devices() {

  let navigate = useNavigate();

  let [nodeList, setNodeList] = useState<any>([]);

  useEffect(() => {
    const fetchNodes = async () => {
      var response = await fetch("/nodes");

      if (response.ok) {
        let data = await response.json();
        setNodeList(data);
      }
    };

    fetchNodes();
  }, []);

  let nodes = nodeList.map((n: any) => <tr key={n.nodeId} onClick={() => navigate(`/devices/${n.nodeId}`)} style={{'cursor':'pointer'}}><td>{n.nodeId.toString(16)}</td></tr>);

  return (
    <>
      <h1>Devices</h1>
      <hr />
      <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th>Node ID</th>
          </tr>
        </thead>
        <tbody>
          {nodes}
        </tbody>
      </table>
      <NavLink className="btn btn-primary" to="/devices/add">Add Device</NavLink>
    </>
  )
}

export default Devices

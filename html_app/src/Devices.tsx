import { useEffect, useState } from "react"
import { NavLink } from "react-router"

function Devices() {

  var [nodeList, setNodeList] = useState<any>([]);

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

  let nodes = nodeList.map((n: any) => <tr key={n.nodeId}><td>{n.nodeId}</td></tr>);

  return (
    <>
      <h1>Devices</h1>
      <hr />
      <table>
        {nodes}
      </table>
      <NavLink className="btn btn-primary" to="/devices/add">Add Device</NavLink>
    </>
  )
}

export default Devices

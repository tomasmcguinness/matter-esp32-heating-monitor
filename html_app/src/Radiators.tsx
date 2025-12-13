import { useEffect, useState } from "react"
import { NavLink } from "react-router"

function Radiators() {

  let [radiatorList, setRadiatorList] = useState<any>([]);

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
    return (<tr key={n.radiatorId}><td>{n.radiatorId}</td><td>{n.name}</td><td>{n.type}</td><td>{n.output}</td></tr>);
  });

  return (
    <>
      <h1>Radiators</h1>
      <hr />
      <table className="table table-striped table-bordered">
        <thead>
          <tr>
            <th style={{width:'auto'}}>ID</th>
            <th>Name</th>
            <th>Type</th>
            <th>Output</th>
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

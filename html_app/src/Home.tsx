import { NavLink } from "react-router"
import Temperature from "./Temperature"
import { useEffect, useState } from "react";

function Home() {

  let [outdoorTemperature, setOutdoorTemperature] = useState<number | undefined>(undefined);

  useEffect(() => {
      const fetchRoom = async () => {
        var response = await fetch(`/api/home`);
  
        if (response.ok) {
          let data = await response.json();
          setOutdoorTemperature(data.outdoorTemperature);
        }
      };
  
      fetchRoom();
    }, []);

  return (
    <>
    <h1>Home <NavLink className="btn btn-primary action-button" to={`/edit`}>Edit</NavLink></h1>
    <hr />
    <p>Welcome to the Heating Monitor web interface.</p>
    <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Outside Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><Temperature>{outdoorTemperature}</Temperature></p>
          </div>
        </div>
      </div>
    </>
  )
}

export default Home

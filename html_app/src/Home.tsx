import { NavLink } from "react-router"
import Temperature from "./Temperature"
import { useEffect, useState } from "react";
import FlowRate from "./FlowRate";

function Home() {

  let [outdoorTemperature, setOutdoorTemperature] = useState<number | undefined>(undefined);
  let [heatSourceFlowTemperature, setHeatSourceFlowTemperature] = useState<number | undefined>(undefined);
  let [heatSourceReturnTemperature, setHeatSourceReturnTemperature] = useState<number | undefined>(undefined);
  let [heatSourceFlowRate, setHeatSourceFlowRate] = useState<number | undefined>(undefined);
  let [heatSourceOutput, setHeatSourceOutput] = useState<number | undefined>(undefined);
  let [totalPredictedHeatLoss, setTotalPredictedHeatLoss] = useState<number | undefined>(undefined);
  let [totalEstimatedHeatLoss, setTotalEstimatedHeatLoss] = useState<number | undefined>(undefined);
  let [radiatorCount, setRadiatorCount] = useState<number | undefined>(undefined);
  let [totalRadiatorOutput, setTotalRadiatorOutput] = useState<number | undefined>(undefined);

  useEffect(() => {
    const fetchRoom = async () => {
      var response = await fetch(`/api/home`);

      if (response.ok) {
        let data = await response.json();
        setOutdoorTemperature(data.outdoorTemperature);
        setHeatSourceFlowTemperature(data.heatSourceFlowTemperature);
        setHeatSourceReturnTemperature(data.heatSourceReturnTemperature);
        setHeatSourceFlowRate(data.heatSourceFlowRate);
        setHeatSourceOutput(data.heatSourceOutput);

        setTotalPredictedHeatLoss(data.predictedHeatLoss);
        setTotalEstimatedHeatLoss(data.estimatedHeatLoss);
        setRadiatorCount(data.radiatorCount);
        setTotalRadiatorOutput(data.totalRadiatorOutput);
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
            <p className="card-title"><h3><Temperature>{outdoorTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Predicted Heat Loss
          </div>
          <div className="card-body">
            <p className="card-title"><h3>{totalPredictedHeatLoss}W</h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Estimated Heat Loss
          </div>
          <div className="card-body">
            <p className="card-title"><h3>{totalEstimatedHeatLoss}W</h3></p>
          </div>
        </div>
      </div>
      <h4 style={{marginTop: '20px'}}>Heat Source</h4>
      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Flow Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Temperature>{heatSourceFlowTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Return Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Temperature>{heatSourceReturnTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Flow Rate
          </div>
          <div className="card-body">
            <p className="card-title"><h3><FlowRate>{heatSourceFlowRate}</FlowRate></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Output
          </div>
          <div className="card-body">
            <p className="card-title"><h3>{heatSourceOutput}W</h3></p>
          </div>
        </div>
      </div>
      <h4 style={{marginTop: '20px'}}>Distribution</h4>
      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            # Radiators
          </div>
          <div className="card-body">
            <p className="card-title"><h3>{radiatorCount}</h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Radiator Output
          </div>
          <div className="card-body">
            <p className="card-title"><h3>{totalRadiatorOutput}W</h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            UFH Output
          </div>
          <div className="card-body">
            <p className="card-title">TBD</p>
          </div>
        </div>
      </div>
    </>
  )
}

export default Home

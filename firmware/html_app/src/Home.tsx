import { NavLink } from "react-router"
import Temperature from "./Temperature"
import { useCallback, useContext, useEffect, useState } from "react";
import FlowRate from "./FlowRate";
import Power from "./Power";
import Voltage from "./Voltage";
import Current from "./Current";
import ElectricalPower from "./ElectricalPower";
import { WebSocketContext } from "./WSContext.jsx";

function Home() {

  let [outdoorTemperature, setOutdoorTemperature] = useState<number | undefined>(undefined);
  let [heatSourceFlowTemperature, setHeatSourceFlowTemperature] = useState<number | undefined>(undefined);
  let [heatSourceReturnTemperature, setHeatSourceReturnTemperature] = useState<number | undefined>(undefined);
  let [heatSourceFlowRate, setHeatSourceFlowRate] = useState<number | undefined>(undefined);
  let [heatSourceOutput, setHeatSourceOutput] = useState<number | undefined>(undefined);
  let [totalPredictedHeatLoss, setTotalPredictedHeatLoss] = useState<number | undefined>(undefined);
  let [totalMeasuredHeatLoss, setTotalMeasuredHeatLoss] = useState<number | undefined>(undefined);
  let [radiatorCount, setRadiatorCount] = useState<number | undefined>(undefined);
  let [totalRadiatorOutput, setTotalRadiatorOutput] = useState<number | undefined>(undefined);
  let [heatMeterNodeId, setHeatMeterNodeId] = useState<number | undefined>(undefined);
  let [electricalMeterNodeId, setElectricalMeterNodeId] = useState<number | undefined>(undefined);
  let [electricalVoltage, setElectricalVoltage] = useState<number | null | undefined>(undefined);
  let [electricalCurrent, setElectricalCurrent] = useState<number | null | undefined>(undefined);
  let [electricalPower, setElectricalPower] = useState<number | null | undefined>(undefined);

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  // The firmware builds the "home" websocket payload from the same function as GET /api/home, so the
  // initial fetch and every push can share one applier.
  //
  const applyHome = useCallback((data: any) => {
    setOutdoorTemperature(data.outdoorTemperature);
    setHeatSourceFlowTemperature(data.heatSourceFlowTemperature);
    setHeatSourceReturnTemperature(data.heatSourceReturnTemperature);
    setHeatSourceFlowRate(data.heatSourceFlowRate);
    setHeatSourceOutput(data.heatSourceOutput);

    setTotalPredictedHeatLoss(data.predictedHeatLossAtCurrentTemperature);
    setTotalMeasuredHeatLoss(data.measuredHeatLossAtCurrentTemperature);
    setRadiatorCount(data.radiatorCount);
    setTotalRadiatorOutput(data.totalRadiatorOutput);

    setHeatMeterNodeId(data.heatMeterNodeId);

    setElectricalMeterNodeId(data.electricalMeterNodeId);
    setElectricalVoltage(data.electricalVoltage);
    setElectricalCurrent(data.electricalCurrent);
    setElectricalPower(data.electricalPower);
  }, []);

  // Still fetched on mount, so the page has data straight away rather than waiting for whatever
  // sensor reports next.
  //
  useEffect(() => {
    const fetchHome = async () => {
      var response = await fetch(`/api/home`);

      if (response.ok) {
        applyHome(await response.json());
      }
    };

    fetchHome();
  }, [applyHome]);

  useEffect(() => {
    subscribe("home", applyHome);

    return () => {
      unsubscribe("home")
    }
  }, [subscribe, unsubscribe, applyHome]);

  return (
    <>
      <h1>Home <NavLink className="btn btn-primary action-button" to={`/edit`}>Edit</NavLink></h1>
      <hr />
      <h4 style={{marginTop: '20px'}}>Weather</h4>
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
            <p className="card-title"><h3><Power>{totalPredictedHeatLoss}</Power></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Measured Heat Loss
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Power>{totalMeasuredHeatLoss}</Power></h3></p>
          </div>
        </div>
      </div>
      {/* Node 0 is the firmware's "no meter selected". With one picked, every figure below is read
          straight off it -- including Output, which is the meter's own power reading rather than the
          figure derived from the three individual sensors. */}
      <h4 style={{marginTop: '20px'}}>Heat Meter {!!heatMeterNodeId && <small className="text-muted" style={{ fontSize: '0.6em' }}>direct from meter</small>}</h4>
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
            <p className="card-title"><h3>N/A</h3></p>
          </div>
        </div>
      </div>
      {/* Node 0 is the firmware's "no meter selected", so the section only appears once one is picked. */}
      {!!electricalMeterNodeId && <>
        <h4 style={{marginTop: '20px'}}>Electricity</h4>
        <div className="card-group" style={{ marginBottom: '5px' }}>
          <div className="card">
            <div className="card-header">
              Voltage
            </div>
            <div className="card-body">
              <p className="card-title"><h3><Voltage>{electricalVoltage}</Voltage></h3></p>
            </div>
          </div>
          <div className="card">
            <div className="card-header">
              Current
            </div>
            <div className="card-body">
              <p className="card-title"><h3><Current>{electricalCurrent}</Current></h3></p>
            </div>
          </div>
          <div className="card">
            <div className="card-header">
              Power
            </div>
            <div className="card-body">
              <p className="card-title"><h3><ElectricalPower>{electricalPower}</ElectricalPower></h3></p>
            </div>
          </div>
        </div>
      </>}
    </>
  )
}

export default Home

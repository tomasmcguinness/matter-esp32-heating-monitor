import { NavLink } from "react-router"
import Temperature from "./Temperature"
import { useCallback, useContext, useEffect, useState } from "react";
import FlowRate from "./FlowRate";
// import Power from "./Power";
import Voltage from "./Voltage";
import Current from "./Current";
import ElectricalPower from "./ElectricalPower";
import Cop from "./Cop";
import SubscriptionPill from "./SubscriptionPill";
import { WebSocketContext } from "./WSContext.jsx";

function Home() {

  let [averageInternalTemperature, setAverageInternalTemperature] = useState<number | null | undefined>(undefined);
  let [cop, setCop] = useState<number | null | undefined>(undefined);
  let [outdoorTemperature, setOutdoorTemperature] = useState<number | null | undefined>(undefined);
  let [outdoorTemperatureSensorNodeId, setOutdoorTemperatureSensorNodeId] = useState<number | undefined>(undefined);
  let [outdoorTemperatureSensorSubscription, setOutdoorTemperatureSensorSubscription] = useState<string | undefined>(undefined);
  let [heatMeterNodeId, setHeatMeterNodeId] = useState<number | undefined>(undefined);
  let [heatMeterSubscription, setHeatMeterSubscription] = useState<string | undefined>(undefined);
  let [heatMeterFlowTemperature, setHeatMeterFlowTemperature] = useState<number | null | undefined>(undefined);
  let [heatMeterReturnTemperature, setHeatMeterReturnTemperature] = useState<number | null | undefined>(undefined);
  let [heatMeterFlowRate, setHeatMeterFlowRate] = useState<number | null | undefined>(undefined);
  let [heatMeterPower, setHeatMeterPower] = useState<number | null | undefined>(undefined);
  // let [totalPredictedHeatLoss, setTotalPredictedHeatLoss] = useState<number | undefined>(undefined);
  // let [totalMeasuredHeatLoss, setTotalMeasuredHeatLoss] = useState<number | undefined>(undefined);
  let [radiatorCount, setRadiatorCount] = useState<number | undefined>(undefined);
  let [totalRadiatorOutput, setTotalRadiatorOutput] = useState<number | undefined>(undefined);
  let [electricalMeterNodeId, setElectricalMeterNodeId] = useState<number | undefined>(undefined);
  let [electricalMeterSubscription, setElectricalMeterSubscription] = useState<string | undefined>(undefined);
  let [electricalVoltage, setElectricalVoltage] = useState<number | null | undefined>(undefined);
  let [electricalCurrent, setElectricalCurrent] = useState<number | null | undefined>(undefined);
  let [electricalPower, setElectricalPower] = useState<number | null | undefined>(undefined);

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  // The firmware builds the "home" websocket payload from the same function as GET /api/home, so the
  // initial fetch and every push can share one applier.
  //
  const applyHome = useCallback((data: any) => {
    setAverageInternalTemperature(data.averageInternalTemperature);
    setCop(data.cop);

    setOutdoorTemperature(data.outdoorTemperature);
    setOutdoorTemperatureSensorNodeId(data.outdoorTemperatureSensorNodeId);
    setOutdoorTemperatureSensorSubscription(data.outdoorTemperatureSensorSubscription);

    setHeatMeterNodeId(data.heatMeterNodeId);
    setHeatMeterSubscription(data.heatMeterSubscription);
    setHeatMeterFlowTemperature(data.heatMeterFlowTemperature);
    setHeatMeterReturnTemperature(data.heatMeterReturnTemperature);
    setHeatMeterFlowRate(data.heatMeterFlowRate);
    setHeatMeterPower(data.heatMeterPower);

    // setTotalPredictedHeatLoss(data.predictedHeatLossAtCurrentTemperature);
    // setTotalMeasuredHeatLoss(data.measuredHeatLossAtCurrentTemperature);
    setRadiatorCount(data.radiatorCount);
    setTotalRadiatorOutput(data.totalRadiatorOutput);

    setElectricalMeterNodeId(data.electricalMeterNodeId);
    setElectricalMeterSubscription(data.electricalMeterSubscription);
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
      {/* Node 0 is the firmware's "nothing selected". Sections check for it exactly, rather than for
          falsy, so the alert doesn't flash up before the first fetch has returned. */}
      <div className="row">
        {/* Both of these are derived by the firmware rather than read from one device, so this
            heading carries no subscription pill. */}
        <div className="col-md-6">
          <h4 style={{marginTop: '20px'}}>Indoors</h4>
          <div className="card-group" style={{ marginBottom: '5px' }}>
            <div className="card">
              <div className="card-header">
                Average Temperature
              </div>
              <div className="card-body">
                <p className="card-title"><h3><Temperature>{averageInternalTemperature}</Temperature></h3></p>
              </div>
            </div>
            <div className="card">
              <div className="card-header">
                COP
              </div>
              <div className="card-body">
                <p className="card-title"><h3><Cop>{cop}</Cop></h3></p>
              </div>
            </div>
        {/* <div className="card">
          <div className="card-header">
            Predicted Heat Loss
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Power>{totalPredictedHeatLoss}</Power></h3></p>
          </div>
        </div> */}
        {/* <div className="card">
          <div className="card-header">
            Measured Heat Loss
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Power>{totalMeasuredHeatLoss}</Power></h3></p>
          </div>
        </div> */}
          </div>
        </div>
        <div className="col-md-6">
          <h4 className="d-flex justify-content-between align-items-center" style={{marginTop: '20px'}}>Outdoors<SubscriptionPill state={outdoorTemperatureSensorSubscription} /></h4>
          <div className="card-group" style={{ marginBottom: '5px' }}>
            <div className="card">
              <div className="card-header">
                Temperature
              </div>
              <div className="card-body">
                {outdoorTemperatureSensorNodeId === 0 ?
                  <span>No outdoor temperature sensor is configured. <NavLink to={`/edit`}>Configure one</NavLink>.</span> :
                  <p className="card-title"><h3><Temperature>{outdoorTemperature}</Temperature></h3></p>}
              </div>
            </div>
            <div className="card">
              <div className="card-header">
                Forecast
              </div>
              <div className="card-body">
              </div>
            </div>
          </div>
        </div>
      </div>
      <h4 className="d-flex justify-content-between align-items-center" style={{marginTop: '20px'}}>Heat Meter<SubscriptionPill state={heatMeterSubscription} /></h4>
      {heatMeterNodeId === 0 ?
        <div className="alert alert-info">No heat meter is configured. <NavLink to={`/edit`}>Configure one</NavLink>.</div> :
      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Flow Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Temperature>{heatMeterFlowTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Return Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Temperature>{heatMeterReturnTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Flow Rate
          </div>
          <div className="card-body">
            <p className="card-title"><h3><FlowRate>{heatMeterFlowRate}</FlowRate></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header">
            Output
          </div>
          <div className="card-body">
            {/* The meter reports power in mW, which is what ElectricalPower formats. */}
            <p className="card-title"><h3><ElectricalPower>{heatMeterPower}</ElectricalPower></h3></p>
          </div>
        </div>
      </div>}
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
      <h4 className="d-flex justify-content-between align-items-center" style={{marginTop: '20px'}}>Electricity<SubscriptionPill state={electricalMeterSubscription} /></h4>
      {electricalMeterNodeId === 0 ?
        <div className="alert alert-info">No electrical meter is configured. <NavLink to={`/edit`}>Configure one</NavLink>.</div> :
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
        </div>}
    </>
  )
}

export default Home

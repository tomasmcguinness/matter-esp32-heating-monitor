import { NavLink } from "react-router"
import Temperature from "./Temperature"
import { useCallback, useEffect, useState } from "react";
import FlowRate from "./FlowRate";
// import Power from "./Power";
import Voltage from "./Voltage";
import Current from "./Current";
import ElectricalPower from "./ElectricalPower";
import Cop from "./Cop";
import HourlyCost, { type CommodityPrice } from "./HourlyCost";
import SubscriptionPill from "./SubscriptionPill";
import HeatPump, { type HeatPumpData } from "./HeatPump";
import PageHeader from "./PageHeader";
import { useHome } from "./HomeContext";

// The underfloor heating's probes, and what they say about the circuit. The implied flow rate is
// the derived output over the measured delta, so it is only as good as the radiator model the
// output comes from: an implausible figure is a sign that model is off, not the circuit.
//
function UnderfloorHeating({ flow, ret, output }: { flow: number | null | undefined; ret: number | null | undefined; output: number | null | undefined }) {
  const deltaC = flow != null && ret != null ? Math.abs(flow - ret) / 100 : null;
  const impliedLph = output != null && output > 0 && deltaC != null && deltaC >= 0.5 ? Math.round(output / (1.163 * deltaC)) : null;

  return (
    <div className="card-group" style={{ marginBottom: '5px' }}>
      <div className="card">
        <div className="card-header">UFH Flow</div>
        <div className="card-body"><p className="card-title"><h3><Temperature>{flow}</Temperature></h3></p></div>
      </div>
      <div className="card">
        <div className="card-header">UFH Return</div>
        <div className="card-body"><p className="card-title"><h3><Temperature>{ret}</Temperature></h3></p></div>
      </div>
      <div className="card">
        <div className="card-header">UFH &Delta;T</div>
        <div className="card-body"><p className="card-title"><h3>{deltaC == null ? '-' : `${deltaC.toFixed(1)}°C`}</h3></p></div>
      </div>
      <div className="card">
        <div className="card-header">UFH Implied Flow</div>
        <div className="card-body"><p className="card-title"><h3>{impliedLph == null ? '-' : `${impliedLph} l/h`}</h3></p></div>
      </div>
    </div>
  );
}

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
  const [ufhOutput, setUfhOutput] = useState<number | null | undefined>(undefined);
  const [ufhFlowSensorNodeId, setUfhFlowSensorNodeId] = useState<number | undefined>(undefined);
  const [ufhFlowTemperature, setUfhFlowTemperature] = useState<number | null | undefined>(undefined);
  const [ufhReturnTemperature, setUfhReturnTemperature] = useState<number | null | undefined>(undefined);
  let [heatPumpNodeId, setHeatPumpNodeId] = useState<number | undefined>(undefined);
  let [heatPumpSubscription, setHeatPumpSubscription] = useState<string | undefined>(undefined);
  let [heatPump, setHeatPump] = useState<HeatPumpData | null | undefined>(undefined);
  let [electricalMeterNodeId, setElectricalMeterNodeId] = useState<number | undefined>(undefined);
  let [electricalMeterSubscription, setElectricalMeterSubscription] = useState<string | undefined>(undefined);
  let [electricalVoltage, setElectricalVoltage] = useState<number | null | undefined>(undefined);
  let [electricalCurrent, setElectricalCurrent] = useState<number | null | undefined>(undefined);
  let [electricalPower, setElectricalPower] = useState<number | null | undefined>(undefined);
  let [commodityPriceNodeId, setCommodityPriceNodeId] = useState<number | undefined>(undefined);
  let [commodityPriceSubscription, setCommodityPriceSubscription] = useState<string | undefined>(undefined);
  let [commodityPrice, setCommodityPrice] = useState<CommodityPrice | null | undefined>(undefined);

  const home = useHome();

  // HomeContext keeps the latest GET /api/home payload, replaced by every "home" push, so the page
  // applies whichever arrived last.
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
    setUfhOutput(data.ufhOutput);
    setUfhFlowSensorNodeId(data.ufhFlowSensorNodeId);
    setUfhFlowTemperature(data.ufhFlowTemperature);
    setUfhReturnTemperature(data.ufhReturnTemperature);

    setHeatPumpNodeId(data.heatPumpNodeId);
    setHeatPumpSubscription(data.heatPumpSubscription);
    setHeatPump(data.heatPump);

    setElectricalMeterNodeId(data.electricalMeterNodeId);
    setElectricalMeterSubscription(data.electricalMeterSubscription);
    setElectricalVoltage(data.electricalVoltage);
    setElectricalCurrent(data.electricalCurrent);
    setElectricalPower(data.electricalPower);

    setCommodityPriceNodeId(data.commodityPriceNodeId);
    setCommodityPriceSubscription(data.commodityPriceSubscription);
    setCommodityPrice(data.commodityPrice);
  }, []);

  useEffect(() => {
    if (home) {
      applyHome(home);
    }
  }, [home, applyHome]);

  return (
    <>
      <PageHeader title="Home">
        <NavLink className="btn btn-primary" to={`/edit`}>Edit</NavLink>
      </PageHeader>
      {/* Node 0 is the firmware's "nothing selected". Sections check for it exactly, rather than for
          falsy, so the alert doesn't flash up before the first fetch has returned. */}
      <h4 style={{marginTop: '20px'}}>Summary</h4>
      {/* The figures here come from different places, so the subscription pills are on the cards that
          depend on a single device, rather than on the heading. */}
      <div className="card-group" style={{ marginBottom: '5px' }}>
        <div className="card">
          <div className="card-header">
            Average Indoor Temperature
          </div>
          <div className="card-body">
            <p className="card-title"><h3><Temperature>{averageInternalTemperature}</Temperature></h3></p>
          </div>
        </div>
        <div className="card">
          <div className="card-header d-flex justify-content-between align-items-center">
            Outdoor Temperature<SubscriptionPill state={outdoorTemperatureSensorSubscription} />
          </div>
          <div className="card-body">
            {outdoorTemperatureSensorNodeId === 0 ?
              <span>No outdoor temperature sensor is configured. <NavLink to={`/edit`}>Configure one</NavLink>.</span> :
              <p className="card-title"><h3><Temperature>{outdoorTemperature}</Temperature></h3></p>}
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
        <div className="card">
          <div className="card-header d-flex justify-content-between align-items-center">
            Approx Hourly Running Cost<SubscriptionPill state={commodityPriceSubscription} />
          </div>
          <div className="card-body">
            {commodityPriceNodeId === 0 ?
              <span>No commodity price is configured. <NavLink to={`/edit`}>Configure one</NavLink>.</span> :
              <p className="card-title"><h3><HourlyCost electricalPower={electricalPower} commodityPrice={commodityPrice} /></h3></p>}
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
      {/* Unlike the other sections this one has no "configure one" alert: a heat pump is optional,
          so the section is only there once one has been picked. */}
      {!!heatPumpNodeId && <HeatPump heatPump={heatPump} subscription={heatPumpSubscription} />}
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
            {/* The heat meter less the radiators, so a dash while either is missing -- see
                update_ufh_output() in the firmware. */}
            <p className="card-title"><h3>{ufhOutput == null ? '-' : `${ufhOutput}W`}</h3></p>
          </div>
        </div>
      </div>
      {!!ufhFlowSensorNodeId && <UnderfloorHeating flow={ufhFlowTemperature} ret={ufhReturnTemperature} output={ufhOutput} />}
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

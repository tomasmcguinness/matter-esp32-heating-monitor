import { useEffect, useState } from "react";
import { NavLink, useNavigate } from "react-router";
import SensorSelect from "./SensorSelect";

function EditHome() {

  const navigate = useNavigate();

  const [outdoorTemperatureSensor, setOutdoorTemperatureSensor] = useState<string | undefined>(undefined);
  const [flowTemperatureSensor, setFlowTemperatureSensor] = useState<string | undefined>(undefined);
  const [returnTemperatureSensor, setReturnTemperatureSensor] = useState<string | undefined>(undefined);
  const [flowRateSensor, setFlowRateSensor] = useState<string | undefined>(undefined);
  const [electricalMeter, setElectricalMeter] = useState<string | undefined>(undefined);
  const [heatMeter, setHeatMeter] = useState<string | undefined>(undefined);
  const [heatPump, setHeatPump] = useState<string | undefined>(undefined);
  const [commodityPrice, setCommodityPrice] = useState<string | undefined>(undefined);
  const [ufhFlowSensor, setUfhFlowSensor] = useState<string | undefined>(undefined);
  const [ufhReturnSensor, setUfhReturnSensor] = useState<string | undefined>(undefined);
  // Every room, with its share of the underfloor heating as typed (a string, so the box can be
  // cleared while editing). Rooms without a share are listed too, at "".
  const [ufhShares, setUfhShares] = useState<{ roomId: number; name: string; pct: string }[]>([]);
  const [loggingInterval, setLoggingInterval] = useState<number>(5);
  const [activeInterval, setActiveInterval] = useState<number | undefined>(undefined);

  useEffect(() => {
    Promise.all([
      fetch('/api/home').then(response => response.json()),
      fetch('/api/rooms').then(response => response.json()),
    // The home is left loosely typed, as it was before the rooms were fetched alongside it.
    ]).then(([data, rooms]: [Awaited<ReturnType<Response['json']>>, { roomId: number; name: string }[]]) => {
      setUfhFlowSensor(`${data.ufhFlowSensorNodeId}|${data.ufhFlowSensorEndpointId}`);
      setUfhReturnSensor(`${data.ufhReturnSensorNodeId}|${data.ufhReturnSensorEndpointId}`);
      const shares = new Map<number, number>((data.ufhShares ?? []).map((s: { roomId: number; pct: number }) => [s.roomId, s.pct]));
      setUfhShares(rooms.map((room) => ({ roomId: room.roomId, name: room.name, pct: shares.has(room.roomId) ? String(shares.get(room.roomId)) : '' })));
      setOutdoorTemperatureSensor(`${data.outdoorTemperatureSensorNodeId}|${data.outdoorTemperatureSensorEndpointId}`);
      setFlowTemperatureSensor(`${data.heatSourceFlowTempSensorNodeId}|${data.heatSourceFlowTempSensorEndpointId}`);
      setReturnTemperatureSensor(`${data.heatSourceReturnTempSensorNodeId}|${data.heatSourceReturnTempSensorEndpointId}`);
      setFlowRateSensor(`${data.heatSourceFlowRateSensorNodeId}|${data.heatSourceFlowRateSensorEndpointId}`);
      setElectricalMeter(`${data.electricalMeterNodeId}|${data.electricalMeterEndpointId}`);
      setHeatMeter(`${data.heatMeterNodeId}|${data.heatMeterEndpointId}`);
      setHeatPump(`${data.heatPumpNodeId}|${data.heatPumpEndpointId}`);
      setCommodityPrice(`${data.commodityPriceNodeId}|${data.commodityPriceEndpointId}`);
      setLoggingInterval(data.loggingIntervalSeconds ?? 5);
      setActiveInterval(data.loggingIntervalActiveSeconds);
    });
  }, []);

  const ufhShareTotal = ufhShares.reduce((total, share) => total + (parseInt(share.pct) || 0), 0);

  const setUfhShare = (roomId: number, pct: string) =>
    setUfhShares((shares) => shares.map((share) => (share.roomId === roomId ? { ...share, pct } : share)));

  const swapUfhSensors = () => {
    setUfhFlowSensor(ufhReturnSensor);
    setUfhReturnSensor(ufhFlowSensor);
  };

  const save = (e: any) => {
    e.preventDefault();

    if (ufhShareTotal > 100) {
      alert("The underfloor heating's room shares add up to more than 100%.");
      return;
    }

    // Every selection is optional, so a cleared select gives NaN on both halves -- which serialises
    // as null and is rejected by the firmware. The firmware treats node 0 as "nothing selected",
    // which is what an unset sensor loads back as, so fall back to that.
    //
    const nodeId = (sensor: string | undefined) => parseInt(sensor?.split('|')[0] ?? '') || 0;
    const endpointId = (sensor: string | undefined) => parseInt(sensor?.split('|')[1] ?? '') || 0;

    var outdoorTemperatureSensorNodeId = nodeId(outdoorTemperatureSensor);
    var outdoorTemperatureSensorEndpointId = endpointId(outdoorTemperatureSensor);

    var flowTemperatureSensorNodeId = nodeId(flowTemperatureSensor);
    var flowTemperatureSensorEndpointId = endpointId(flowTemperatureSensor);

    var returnTemperatureSensorNodeId = nodeId(returnTemperatureSensor);
    var returnTemperatureSensorEndpointId = endpointId(returnTemperatureSensor);

    var flowRateSensorNodeId = nodeId(flowRateSensor);
    var flowRateSensorEndpointId = endpointId(flowRateSensor);

    var electricalMeterNodeId = nodeId(electricalMeter);
    var electricalMeterEndpointId = endpointId(electricalMeter);

    var heatMeterNodeId = nodeId(heatMeter);
    var heatMeterEndpointId = endpointId(heatMeter);

    var heatPumpNodeId = nodeId(heatPump);
    var heatPumpEndpointId = endpointId(heatPump);

    var commodityPriceNodeId = nodeId(commodityPrice);
    var commodityPriceEndpointId = endpointId(commodityPrice);

    const ufhFlowSensorNodeId = nodeId(ufhFlowSensor);
    const ufhFlowSensorEndpointId = endpointId(ufhFlowSensor);

    const ufhReturnSensorNodeId = nodeId(ufhReturnSensor);
    const ufhReturnSensorEndpointId = endpointId(ufhReturnSensor);

    // Only the rooms with a share; the firmware drops 0% anyway.
    const ufhSharesJson = ufhShares
      .map((share) => ({ roomId: share.roomId, pct: parseInt(share.pct) || 0 }))
      .filter((share) => share.pct > 0);

    var object: any = {
      outdoorTemperatureSensorNodeId,
      outdoorTemperatureSensorEndpointId,
      flowTemperatureSensorNodeId,
      flowTemperatureSensorEndpointId,
      returnTemperatureSensorNodeId,
      returnTemperatureSensorEndpointId,
      flowRateSensorNodeId,
      flowRateSensorEndpointId,
      electricalMeterNodeId,
      electricalMeterEndpointId,
      heatMeterNodeId,
      heatMeterEndpointId,
      heatPumpNodeId,
      heatPumpEndpointId,
      commodityPriceNodeId,
      commodityPriceEndpointId,
      ufhFlowSensorNodeId,
      ufhFlowSensorEndpointId,
      ufhReturnSensorNodeId,
      ufhReturnSensorEndpointId,
      ufhShares: ufhSharesJson,
      loggingIntervalSeconds: loggingInterval
    };
    var json = JSON.stringify(object);

    fetch(`/api/home`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate('/');
      } else {
        // The firmware says what it refused, e.g. shares over 100%.
        r.text().then((reason) => alert(`Failed to update home: ${reason}`));
      }
    });
  }

  return (
    <>
      <h1>Edit Home</h1>
      <hr />
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Outdoor Temperature Sensor" required={false} id="outdoorTemperatureSensor" selectedSensor={outdoorTemperatureSensor} onSelectedSensorChange={(e: string) => setOutdoorTemperatureSensor(e)} />
      </div>
      {/* <div className="mb-3">
        <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={false} id="flowTemperatureSensor" selectedSensor={flowTemperatureSensor} onSelectedSensorChange={(e: string) => setFlowTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Return Temperature Sensor" required={false} id="returnTemperatureSensor" selectedSensor={returnTemperatureSensor} onSelectedSensorChange={(e: string) => setReturnTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={774} title="Flow Rate Sensor" required={false} id="flowRateSensor" selectedSensor={flowRateSensor} onSelectedSensorChange={(e: string) => setFlowRateSensor(e)} />
      </div> */}
      <div className="mb-3">
        {/* The M-Bus adapter's manufacturer-specific heat meter device type. Selecting one makes it
            the source for the whole Heat Meter section, in place of the three sensors above. */}
        <SensorSelect deviceType={0xFFF10001} title="Heat Meter" required={false} id="heatMeter" selectedSensor={heatMeter} onSelectedSensorChange={(e: string) => setHeatMeter(e)} />
      </div>
      <div className="mb-3">
        {/* Selecting one adds a read-only Heat Pump section to the Home page, showing the heat pump's
            power along with the temperature sensors, heating loops and hot water tank on its node. */}
        <SensorSelect deviceType={777} title="Heat Pump" required={false} id="heatPump" selectedSensor={heatPump} onSelectedSensorChange={(e: string) => setHeatPump(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={1296} title="Electricity Meter" required={false} id="electricalMeter" selectedSensor={electricalMeter} onSelectedSensorChange={(e: string) => setElectricalMeter(e)} />
      </div>
      <div className="mb-3">
        {/* Together with the Electricity Meter, this gives the Home page its running cost. */}
        <SensorSelect deviceType={1299} title="Commodity Price" required={false} id="commodityPrice" selectedSensor={commodityPrice} onSelectedSensorChange={(e: string) => setCommodityPrice(e)} />
      </div>
      <h4 style={{ marginTop: '20px' }}>Underfloor Heating</h4>
      <p className="form-text">
        The underfloor heating's output is not measured directly: it is what the Heat Meter shows
        that the radiators do not account for. The probes are recorded, and stop the output being
        counted while the circuit shows less than 0.5&deg;C across them.
      </p>
      <div className="row g-2 mb-3">
        <div className="col-12 col-md">
          <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={false} id="ufhFlowSensor" selectedSensor={ufhFlowSensor} onSelectedSensorChange={(e: string) => setUfhFlowSensor(e)} />
        </div>
        <div className="col-12 col-md-auto d-flex justify-content-center align-items-end">
          <button type="button" className="btn btn-outline-secondary" title="Swap flow and return sensors" aria-label="Swap flow and return sensors" onClick={swapUfhSensors}>
            <i className="bi bi-arrow-left-right d-none d-md-inline"></i>
            <i className="bi bi-arrow-down-up d-md-none"></i>
          </button>
        </div>
        <div className="col-12 col-md">
          <SensorSelect deviceType={770} title="Return Temperature Sensor" required={false} id="ufhReturnSensor" selectedSensor={ufhReturnSensor} onSelectedSensorChange={(e: string) => setUfhReturnSensor(e)} />
        </div>
      </div>
      <div className="mb-3">
        <label className="form-label">Share of the underfloor heating per room (%)</label>
        {ufhShares.length === 0 && <div className="form-text">Add rooms to split the underfloor heating between them.</div>}
        {ufhShares.map((share) => (
          <div className="input-group mb-1" key={share.roomId}>
            <span className="input-group-text" style={{ minWidth: '12em' }}>{share.name}</span>
            <input
              type="number"
              min={0}
              max={100}
              className="form-control"
              aria-label={`${share.name} share of the underfloor heating`}
              value={share.pct}
              placeholder="0"
              onChange={(e) => setUfhShare(share.roomId, e.target.value)}
            />
            <span className="input-group-text">%</span>
          </div>
        ))}
        <div className={`form-text${ufhShareTotal > 100 ? ' text-danger' : ''}`}>
          {ufhShareTotal}% allocated. Each room adds its share to the heat it is credited with when
          its heat loss is measured. Less than 100% leaves the rest unallocated.
        </div>
      </div>
      <div className="mb-3">
        <label className="form-label" htmlFor="loggingInterval">History sampling interval (seconds)</label>
        <input
          id="loggingInterval"
          type="number"
          min={1}
          max={3600}
          className="form-control"
          value={loggingInterval}
          onChange={(e) => setLoggingInterval(parseInt(e.target.value) || 5)}
        />
        <div className="form-text">
          How often every sensor is recorded to the SD card. A change takes effect at
          midnight: a day's file has one interval baked into its header, which is what lets
          the device seek straight to a time instead of scanning.
          {activeInterval !== undefined && activeInterval !== loggingInterval && (
            <> Today is still being recorded every {activeInterval}s.</>
          )}
        </div>
      </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/`}>Cancel</NavLink>
    </>
  )
}

export default EditHome;

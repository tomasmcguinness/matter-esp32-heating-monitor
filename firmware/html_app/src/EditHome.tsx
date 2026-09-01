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

  useEffect(() => {
    fetch('/api/home').then(response => response.json()).then(data => {
      setOutdoorTemperatureSensor(`${data.outdoorTemperatureSensorNodeId}|${data.outdoorTemperatureSensorEndpointId}`);
      setFlowTemperatureSensor(`${data.heatSourceFlowTemperatureSensorNodeId}|${data.heatSourceFlowTemperatureSensorEndpointId}`);
      setReturnTemperatureSensor(`${data.heatSourceReturnTemperatureSensorNodeId}|${data.heatSourceReturnTemperatureSensorEndpointId}`);
      setFlowRateSensor(`${data.heatSourceFlowRateSensorNodeId}|${data.heatSourceFlowRateSensorEndpointId}`);
      setElectricalMeter(`${data.electricalMeterNodeId}|${data.electricalMeterEndpointId}`);
      setHeatMeter(`${data.heatMeterNodeId}|${data.heatMeterEndpointId}`);
    });
  }, []);

  const save = (e: any) => {
    e.preventDefault();

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
      heatMeterEndpointId
    };
    var json = JSON.stringify(object);

    fetch(`/api/home`, { method: "PUT", headers: { 'Content-Type': 'application/json' }, body: json }).then(r => {
      if (r.ok) {
        navigate('/');
      } else {
        alert("Failed to update home");
      }
    });
  }

  return (
    <>
      <h1>Edit Home</h1>
      <hr />
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Outdoor Temperature Sensor" required={false} selectedSensor={outdoorTemperatureSensor} onSelectedSensorChange={(e: string) => setOutdoorTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Flow Temperature Sensor" required={false} selectedSensor={flowTemperatureSensor} onSelectedSensorChange={(e: string) => setFlowTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={770} title="Return Temperature Sensor" required={false} selectedSensor={returnTemperatureSensor} onSelectedSensorChange={(e: string) => setReturnTemperatureSensor(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={774} title="Flow Rate Sensor" required={false} selectedSensor={flowRateSensor} onSelectedSensorChange={(e: string) => setFlowRateSensor(e)} />
      </div>
      <div className="mb-3">
        {/* The M-Bus adapter's manufacturer-specific heat meter device type. Selecting one makes it
            the source for the whole Heat Meter section, in place of the three sensors above. */}
        <SensorSelect deviceType={0xFFF10001} title="Heat Meter" required={false} selectedSensor={heatMeter} onSelectedSensorChange={(e: string) => setHeatMeter(e)} />
      </div>
      <div className="mb-3">
        <SensorSelect deviceType={1296} title="Electricity Meter" required={false} selectedSensor={electricalMeter} onSelectedSensorChange={(e: string) => setElectricalMeter(e)} />
      </div>
      <button className="btn btn-primary" onClick={save} style={{ 'marginRight': '5px' }}>Save</button>
      <NavLink className="btn btn-danger" to={`/`}>Cancel</NavLink>
    </>
  )
}

export default EditHome;

import type { ReactNode } from "react";
import Temperature from "./Temperature";
import ElectricalPower from "./ElectricalPower";
import SubscriptionPill from "./SubscriptionPill";

// The "heatPump" object of GET /api/home -- see add_heat_pump_json in main/app_main.cpp. Values are
// as the Matter clusters report them: mW, 0.01 degC, and the raw enums and bitmaps decoded below.
//
type HeatPumpTemperature = {
  endpointId: number;
  name: string | null;
  temperature: number | null;
};

type HeatPumpZone = {
  endpointId: number;
  name: string | null;
  waterHeater: boolean;
  localTemperature: number | null;
  heatingSetpoint: number | null;
  systemMode: number | null;
  runningState: number | null;
  heatDemand?: number | null;
  boostState?: number | null;
  waterHeaterMode?: number | null;
  waterHeaterModeLabel?: string | null;
};

export type HeatPumpData = {
  power: number | null;
  temperatures: HeatPumpTemperature[];
  zones: HeatPumpZone[];
};

// Thermostat SystemModeEnum.
const systemModes: Record<number, string> = {
  0: "Off",
  1: "Auto",
  3: "Cool",
  4: "Heat",
  5: "Emergency Heat",
  6: "Precooling",
  7: "Fan Only",
  8: "Dry",
  9: "Sleep",
};

const systemMode = (mode: number | null) => mode === null ? "-" : systemModes[mode] ?? `Mode ${mode}`;

// Thermostat RelayStateBitmap: bit 0 is heat, bit 1 is cool. The rest are fan and second stages.
const runningState = (state: number | null) => {
  if (state === null) {
    return "-";
  }

  if (state & 0x01) {
    return "Heating";
  }

  if (state & 0x02) {
    return "Cooling";
  }

  return "Idle";
};

// WaterHeaterHeatSourceBitmap: which sources are heating the tank right now.
const heatSources = ["Immersion 1", "Immersion 2", "Heat Pump", "Boiler", "Other"];

const heatDemand = (demand: number | null | undefined) => {
  if (demand === null || demand === undefined) {
    return "-";
  }

  const active = heatSources.filter((_, bit) => demand & (1 << bit));

  return active.length ? active.join(", ") : "None";
};

// BoostStateEnum.
const boostState = (state: number | null | undefined) => {
  if (state === null || state === undefined) {
    return "-";
  }

  return state === 1 ? "Active" : "Inactive";
};

// WaterHeaterMode CurrentMode. The values are the device's own, so the label comes from its
// SupportedModes; the number is only shown for a mode that list leaves out.
const waterHeaterMode = (zone: HeatPumpZone) => {
  if (zone.waterHeaterModeLabel) {
    return zone.waterHeaterModeLabel;
  }

  return zone.waterHeaterMode === null || zone.waterHeaterMode === undefined ? "-" : `Mode ${zone.waterHeaterMode}`;
};

function Card({ title, children }: { title: string, children: ReactNode }) {
  return (
    <div className="card">
      <div className="card-header">
        {title}
      </div>
      <div className="card-body">
        <p className="card-title"><h3>{children}</h3></p>
      </div>
    </div>
  );
}

// Read only: the monitor shows what the heat pump reports and never writes to it.
//
function HeatPump({ heatPump, subscription }: { heatPump: HeatPumpData | null | undefined, subscription: string | undefined }) {
  return (
    <>
      <h4 className="d-flex justify-content-between align-items-center" style={{ marginTop: '20px' }}>Heat Pump<SubscriptionPill state={subscription} /></h4>
      <div className="card-group" style={{ marginBottom: '5px' }}>
        <Card title="Power"><ElectricalPower>{heatPump?.power}</ElectricalPower></Card>
        {heatPump?.temperatures.map(t =>
          <Card key={t.endpointId} title={`${t.name ?? `Endpoint ${t.endpointId}`} Temperature`}><Temperature>{t.temperature}</Temperature></Card>
        )}
      </div>
      {heatPump?.zones.map(zone =>
        <div key={zone.endpointId}>
          <h6 style={{ marginTop: '15px' }}>{zone.name ?? (zone.waterHeater ? "Hot Water" : `Heating (endpoint ${zone.endpointId})`)}</h6>
          <div className="card-group" style={{ marginBottom: '5px' }}>
            <Card title="Temperature"><Temperature>{zone.localTemperature}</Temperature></Card>
            <Card title="Target"><Temperature>{zone.heatingSetpoint}</Temperature></Card>
            <Card title="Mode">{systemMode(zone.systemMode)}</Card>
            <Card title="State">{runningState(zone.runningState)}</Card>
            {zone.waterHeater && <Card title="Operation">{waterHeaterMode(zone)}</Card>}
            {zone.waterHeater && <Card title="Boost">{boostState(zone.boostState)}</Card>}
            {zone.waterHeater && <Card title="Heated By">{heatDemand(zone.heatDemand)}</Card>}
          </div>
        </div>
      )}
    </>
  );
}

export default HeatPump;

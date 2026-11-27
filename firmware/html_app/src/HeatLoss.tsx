import Temperature from "./Temperature";

// The house's total heat loss right now, against the current outdoor temperature, beside what the
// heat meter is putting in.
//
// Both totals are the firmware's (update_home() in calculations_manager.cpp): for each room, its
// heat loss per °C times (its current temperature - the outdoor temperature), summed. Predicted uses
// the rooms' configured W/°C; measured uses the W/°C each room's heat input implies. A room adds
// nothing without a temperature reading, and to the measured total also without a measured figure,
// so each card says how many rooms it covers. Without an outdoor temperature there is no figure at
// all.

type HeatLossRoom = {
  currentTemperature?: number | null;
  predictedHeatLoss?: number | null;
  measuredHeatLoss?: number | null;
};

type HeatLossProps = {
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  home: any;
  rooms: HeatLossRoom[];
};

// The rooms' W/°C, summed over the same rooms as the kW figure beside it, so the two always cover
// the same part of the house.
function perDegree(rooms: HeatLossRoom[], key: "predictedHeatLoss" | "measuredHeatLoss") {
  return rooms.reduce((sum, r) => sum + (r[key] ?? 0), 0);
}

function kilowatts(watts: number) {
  return `${(watts / 1000).toFixed(1)} kW`;
}

function HeatLoss({ home, rooms }: HeatLossProps) {
  const outdoor = home.outdoorTemperature;
  const hasOutdoor = outdoor !== null && outdoor !== undefined;

  const withTemperature = rooms.filter((r) => r.currentTemperature !== null && r.currentTemperature !== undefined);
  const withMeasured = withTemperature.filter((r) => r.measuredHeatLoss !== null && r.measuredHeatLoss !== undefined && r.measuredHeatLoss > 0);

  const predicted = hasOutdoor && withTemperature.length > 0 ? home.predictedHeatLossAtCurrentTemperature as number : null;
  const measured = hasOutdoor && withMeasured.length > 0 ? home.measuredHeatLossAtCurrentTemperature as number : null;
  const meter = home.heatMeterPower === null || home.heatMeterPower === undefined ? null : home.heatMeterPower / 1000;

  const coverage = (n: number) => `${n} of ${rooms.length} ${rooms.length === 1 ? "room" : "rooms"}`;

  return (
    <div className="card-group" style={{ marginBottom: "15px" }}>
      <div className="card">
        <div className="card-header">Predicted heat loss</div>
        <div className="card-body">
          <h3 className="card-title">
            {predicted === null ? "-" : <>{kilowatts(predicted)} <span className="text-body-secondary fs-5">({Math.round(perDegree(withTemperature, "predictedHeatLoss"))} W/°C)</span></>}
          </h3>
          <p className="card-text text-body-secondary small">
            @ <Temperature>{outdoor}</Temperature> outside{predicted !== null && <>, {coverage(withTemperature.length)}</>}
          </p>
        </div>
      </div>
      <div className="card">
        <div className="card-header">Measured heat loss</div>
        <div className="card-body">
          <h3 className="card-title">
            {measured === null ? "-" : <>{kilowatts(measured)} <span className="text-body-secondary fs-5">({Math.round(perDegree(withMeasured, "measuredHeatLoss"))} W/°C)</span></>}
          </h3>
          <p className="card-text text-body-secondary small">
            @ <Temperature>{outdoor}</Temperature> outside{measured !== null && <>, {coverage(withMeasured.length)}</>}
          </p>
        </div>
      </div>
      <div className="card">
        <div className="card-header">Heat meter</div>
        <div className="card-body">
          <h3 className="card-title">{meter === null ? "-" : kilowatts(meter)}</h3>
          <p className="card-text text-body-secondary small">Heat going in now</p>
        </div>
      </div>
    </div>
  );
}

export default HeatLoss;

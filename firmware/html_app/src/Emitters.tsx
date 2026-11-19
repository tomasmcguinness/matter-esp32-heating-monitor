import { useContext, useEffect, useState } from "react"
import { NavLink, useNavigate } from "react-router"
import Temperature from "./Temperature.tsx";
import { WebSocketContext } from './WSContext.jsx';
import Power from "./Power.tsx";
import RadiatorType from "./RadiatorType.tsx";
import PageHeader from "./PageHeader";
import { isUfhManifold } from "./emitter.ts";

// The Emitters tab: every radiator and UFH manifold. Both are radiator records on the device
// (GET /api/radiators); a manifold is the one with the reserved type, and its output is its share
// of the heat meter's residual rather than the dT50 formula.
//
function Emitters() {

  let navigate = useNavigate();

  let [emitterList, setEmitterList] = useState<any>([]);

  const {subscribe, unsubscribe} = useContext(WebSocketContext);

  useEffect(() => {

    subscribe("radiator", (message: any) => {

      setEmitterList((current: any[]) => current.map(emitter => {
        if (emitter.radiatorId !== message.radiatorId) {
          return emitter;
        }

        const updated = { ...emitter };

        // Either or both, and null when the reading has gone -- a sensor that loses its
        // subscription clears whichever of the two it supplied.
        if (message.hasOwnProperty("flowTemp")) {
          updated.flowTemp = message.flowTemp;
        }

        if (message.hasOwnProperty("returnTemp")) {
          updated.returnTemp = message.returnTemp;
        }

        if (message.hasOwnProperty("currentOutput")) {
          updated.currentOutput = message.currentOutput;
        }

        return updated;
      }));
    })

    return () => {
      unsubscribe("radiator")
    }
  }, [subscribe, unsubscribe])

  useEffect(() => {
    const fetchEmitters = async () => {
      var response = await fetch("/api/radiators");

      if (response.ok) {
        let data = await response.json();
        setEmitterList(data);
      }
    };

    fetchEmitters();
  }, []);

  const row = (n: any) => (
    <tr key={n.radiatorId} onClick={() => navigate(`/emitters/${n.radiatorId}`)} style={{ 'cursor': 'pointer' }}>
      <td>{n.name}</td>
      <td><RadiatorType>{n.type}</RadiatorType></td>
      <td><Power>{n.output}</Power></td>
      <td><Temperature>{n.flowTemp}</Temperature></td>
      <td><Temperature>{n.returnTemp}</Temperature></td>
      <td><Temperature>{n.flowTemp == null || n.returnTemp == null ? null : Math.abs(n.flowTemp - n.returnTemp)}</Temperature></td>
      <td><Temperature>{n.meanWaterTemperature}</Temperature></td>
      <td><Power>{n.currentOutput}</Power></td>
    </tr>
  );

  const sorted = [...emitterList].sort((a: any, b: any) => a.radiatorId > b.radiatorId ? 1 : -1);
  const radiators = sorted.filter(e => !isUfhManifold(e));
  const manifolds = sorted.filter(e => isUfhManifold(e));

  const header = (ratingTitle: string) => (
    <thead>
      <tr>
        <th>Name</th>
        <th>Type</th>
        <th>{ratingTitle}</th>
        <th>Flow</th>
        <th>Return</th>
        <th>ΔT</th>
        <th>MWT</th>
        <th>Output</th>
      </tr>
    </thead>
  );

  return (
    <>
      <PageHeader title="Emitters">
        <NavLink className="btn btn-primary action-button" to="/emitters/add?kind=ufh">Add UFH Manifold</NavLink>
        <NavLink className="btn btn-primary action-button" to="/emitters/add" style={{ 'marginRight': '5px' }}>Add Radiator</NavLink>
      </PageHeader>

      <h2>Radiators</h2>
      {radiators.length === 0 && <div className="alert alert-info">There are no radiators. Add one, or add them with a room.</div>}
      {radiators.length > 0 && <table className="table table-striped table-bordered">
        {header("ΔT 50°")}
        <tbody>
          {radiators.map(row)}
        </tbody>
      </table>}

      <h2>Underfloor Heating</h2>
      {manifolds.length === 0 && <div className="alert alert-info">There are no UFH manifolds. Add one, then give each room it heats a share of it on the room's Edit page.</div>}
      {manifolds.length > 0 && <>
        <table className="table table-striped table-bordered">
          {header("Rated")}
          <tbody>
            {manifolds.map(row)}
          </tbody>
        </table>
        <p className="text-muted small">
          A manifold has no flow meter, so its output is what the heat meter delivers beyond the radiators' modelled
          output, split between the manifolds that are circulating (flow and return at least 0.5° apart) in proportion
          to their rated output.
        </p>
      </>}
    </>
  )
}

export default Emitters

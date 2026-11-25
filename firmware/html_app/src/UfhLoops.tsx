import { useEffect, useState } from "react";
import { UFH_MAX_LOOPS, formatLpm, formatShare, formatWhen } from "./emitter";
import type { UfhCalibration } from "./emitter";
import UfhFlowCurve from "./UfhFlowCurve";

// A UFH manifold's calibration points (TASK-42.2). Each point is what every loop's flow gauge reads,
// saved with the heat meter's flow at that moment. With points at different heat meter flows, the
// manifold's flow is interpolated between them instead of only scaled from one.
//
// Saving sends the gauges to PUT /api/radiators/:id/loops; the device records the heat meter's flow
// and refuses the point unless the heat pump is space heating, so the reading is the flow the
// manifold was fed while the gauges were read. A point within 3% of an existing one's heat meter flow
// replaces it. Every point has the same number of loops: to change it, delete the points first.
// Whether a point can be taken now is polled, so the button enables itself when a heating run starts.
//
function UfhLoops({ radiatorId, initial }: { radiatorId: string; initial: UfhCalibration }) {

  const [calibration, setCalibration] = useState<UfhCalibration>(initial);
  const [flows, setFlows] = useState<string[]>(Array(Math.max(initial.loopCount, 1)).fill(''));
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState<{ kind: 'success' | 'danger'; text: string } | null>(null);

  const hasPoints = calibration.points.length > 0;

  useEffect(() => {
    const timer = setInterval(async () => {
      const response = await fetch(`/api/radiators/${radiatorId}`);

      if (response.ok) {
        const data = await response.json();

        if (data.ufh) {
          setCalibration(data.ufh);
        }
      }
    }, 10000);

    return () => clearInterval(timer);
  }, [radiatorId]);

  function setLoopCount(count: number) {
    if (hasPoints || isNaN(count) || count < 1 || count > UFH_MAX_LOOPS) {
      return;
    }

    setFlows(current => current.length >= count
      ? current.slice(0, count)
      : [...current, ...Array(count - current.length).fill('')]);
  }

  function setFlow(index: number, value: string) {
    setFlows(current => current.map((f, i) => i === index ? value : f));
  }

  const parsed = flows.map(f => parseFloat(f));
  const valid = parsed.every(f => !isNaN(f) && f >= 0.1 && f <= 99.9);
  const totalLpm = parsed.reduce((sum, f) => sum + (isNaN(f) ? 0 : f), 0);

  // The most recent point's readings, as placeholders: a hint at what the gauges read last time, not
  // values that could be saved without being read again.
  const latest = [...calibration.points].sort((a, b) => (b.calibratedAt ?? 0) - (a.calibratedAt ?? 0))[0];

  // The point this would replace: one within 3% of the heat meter's flow now.
  const hmNow = calibration.heatMeterFlowLph;
  const replaces = hmNow == null ? undefined
    : calibration.points.find(p => Math.abs(p.refFlowLph - hmNow) * 100 <= p.refFlowLph * 3);
  const full = !replaces && calibration.points.length >= calibration.maxPoints;

  async function send(method: 'PUT' | 'DELETE', path: string, body?: string) {
    setBusy(true);
    setMessage(null);

    const response = await fetch(`/api/radiators/${radiatorId}${path}`, {
      method,
      headers: body ? { 'Content-Type': 'application/json' } : undefined,
      body
    });

    let ok = false;

    if (response.ok) {
      const data = await response.json();
      setCalibration(data.ufh);
      ok = true;
    } else {
      const reason = await response.text();
      setMessage({ kind: 'danger', text: reason || 'The calibration could not be changed.' });
    }

    setBusy(false);
    return ok;
  }

  async function addPoint() {
    const at = hmNow;

    if (await send('PUT', '/loops', JSON.stringify({ loops: parsed }))) {
      setMessage({ kind: 'success', text: `${replaces ? 'Replaced the' : 'Added a'} point at a heat meter flow of ${at} l/h.` });
      setFlows(Array(flows.length).fill(''));
    }
  }

  async function deletePoint(index: number) {
    const point = calibration.points[index];

    if (window.confirm(`Delete the calibration point taken at a heat meter flow of ${point.refFlowLph} l/h?`)) {
      await send('DELETE', `/loops/${index}`);
    }
  }

  async function deleteAll() {
    if (window.confirm('Delete every calibration point? The manifold will have no output until a new one is taken.')) {
      if (await send('DELETE', '/loops')) {
        setFlows(['']);
      }
    }
  }

  return (
    <>
      <div className="card mb-3">
        <div className="card-header">Calibration points</div>
        <div className="card-body">
          <p className="form-text mt-0">
            During a heating run, read each loop's flow gauge on the manifold and add them as a point. The heat meter's flow at
            that moment is saved with it. Take points at different heat meter flows: between them the manifold's flow is
            interpolated, and outside them it is scaled in proportion from the nearest point. Its output is that flow times the
            drop across its probes.
          </p>

          <div className="mb-3" style={{ 'maxWidth': '12rem' }}>
            <label htmlFor="loopCount" className="form-label">Number of loops</label>
            <input type="number" id="loopCount" className="form-control" min={1} max={UFH_MAX_LOOPS} value={flows.length} disabled={hasPoints} onChange={(e) => setLoopCount(parseInt(e.target.value))} />
            {hasPoints && <div className="form-text">Every point has {calibration.loopCount} loops. Delete the points to change it.</div>}
          </div>

          <div className="row g-2 mb-3">
            {flows.map((flow, i) => (
              <div className="col-6 col-sm-4 col-md-3" key={i}>
                <label htmlFor={`loop${i}`} className="form-label">Loop {i + 1} (l/min)</label>
                <input type="number" id={`loop${i}`} className="form-control" min={0.1} max={99.9} step={0.1}
                  placeholder={latest ? latest.loops[i]?.toFixed(1) : '2.5'} value={flow} onChange={(e) => setFlow(i, e.target.value)} />
              </div>
            ))}
          </div>

          <p className="mb-2">Total: <strong>{totalLpm.toFixed(1)} l/min</strong> &middot; Heat meter now: <strong>{hmNow == null ? '-' : `${hmNow} l/h`}</strong></p>

          {!calibration.canCalibrate && calibration.calibrationBlocker &&
            <div className="alert alert-warning">{calibration.calibrationBlocker}</div>}

          {calibration.canCalibrate && replaces &&
            <div className="alert alert-info">This will replace the point taken at {replaces.refFlowLph} l/h, which is within 3% of the heat meter's flow now.</div>}

          {calibration.canCalibrate && full &&
            <div className="alert alert-warning">There are already {calibration.maxPoints} points. Delete one to add another.</div>}

          {message && <div className={`alert alert-${message.kind}`}>{message.text}</div>}

          <button type="button" className="btn btn-primary" disabled={!valid || !calibration.canCalibrate || full || busy} onClick={addPoint}>
            {busy ? 'Saving…' : replaces ? 'Replace calibration point' : 'Add calibration point'}
          </button>

          {hasPoints && <>
            <table className="table table-sm table-bordered mt-3 mb-1">
              <thead>
                <tr>
                  <th>Heat meter</th>
                  <th>Manifold</th>
                  <th>Share</th>
                  <th>Loops (l/min)</th>
                  <th>Taken</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                {calibration.points.map((p, i) => (
                  <tr key={`${p.refFlowLph}-${p.calibratedAt}`}>
                    <td>{p.refFlowLph} l/h</td>
                    <td>{formatLpm(p.totalLph)}</td>
                    <td>{formatShare(p.totalLph, p.refFlowLph)}</td>
                    <td>{p.loops.map(f => f.toFixed(1)).join(', ')}</td>
                    <td className="small">{formatWhen(p.calibratedAt)}</td>
                    <td>
                      <button type="button" className="btn btn-sm btn-outline-danger" disabled={busy} onClick={() => deletePoint(i)} title="Delete this point" aria-label="Delete this point">
                        <svg xmlns="http://www.w3.org/2000/svg" width="14" height="14" fill="currentColor" viewBox="0 0 16 16" aria-hidden="true">
                          <path d="M2.146 2.854a.5.5 0 1 1 .708-.708L8 7.293l5.146-5.147a.5.5 0 0 1 .708.708L8.707 8l5.147 5.146a.5.5 0 0 1-.708.708L8 8.707l-5.146 5.147a.5.5 0 0 1-.708-.708L7.293 8z" />
                        </svg>
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
            <p className="form-text mb-0">
              The share column shows whether the floor takes the same part of the flow at every heat meter flow.{' '}
              <button type="button" className="btn btn-link btn-sm p-0 align-baseline text-danger" disabled={busy} onClick={deleteAll}>Delete all points</button>
            </p>
          </>}
        </div>
      </div>

      <UfhFlowCurve ufh={calibration} />
    </>
  );
}

export default UfhLoops;

import { useEffect, useState } from "react";
import { UFH_MAX_LOOPS, formatLpm } from "./emitter";
import type { UfhCalibration } from "./emitter";

// A UFH manifold's calibration: how many loops it has and what each one's flow gauge reads. Saving
// sends them to PUT /api/radiators/:id/loops, and the device records the heat meter's flow at that
// moment alongside them. From then on the manifold's flow moves in proportion to the heat meter's.
//
// The device only accepts a calibration while the heat pump is space heating, so the reading it
// records is the flow the manifold was fed while the gauges were read. Whether it can be taken now
// is polled, so the button enables itself when a heating run starts.
//
function UfhLoops({ radiatorId, initial }: { radiatorId: string; initial: UfhCalibration }) {

  const [calibration, setCalibration] = useState<UfhCalibration>(initial);
  const [flows, setFlows] = useState<string[]>(initial.loops.length > 0 ? initial.loops.map(f => f.toString()) : ['']);
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState<{ kind: 'success' | 'danger'; text: string } | null>(null);

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
    if (isNaN(count) || count < 1 || count > UFH_MAX_LOOPS) {
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

  async function save() {
    setSaving(true);
    setMessage(null);

    const response = await fetch(`/api/radiators/${radiatorId}/loops`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ loops: parsed })
    });

    if (response.ok) {
      const data = await response.json();
      setCalibration(data.ufh);
      setMessage({ kind: 'success', text: `Saved against a heat meter flow of ${formatLpm(data.ufh.refFlowLph)}.` });
    } else {
      const reason = await response.text();
      setMessage({ kind: 'danger', text: reason || 'The calibration could not be saved.' });
    }

    setSaving(false);
  }

  const calibratedAt = calibration.calibratedAt ? new Date(calibration.calibratedAt * 1000).toLocaleString() : null;

  return (
    <div className="card mb-3">
      <div className="card-header">Loops</div>
      <div className="card-body">
        <p className="form-text mt-0">
          Read each loop's flow gauge on the manifold during a heating run and enter it here. The heat meter's flow at the
          moment you save is recorded with them, and the manifold's flow is then taken to move in proportion to it: a 5%
          rise at the heat meter is a 5% rise at the manifold. Its output is that flow times the drop across its probes.
        </p>

        <div className="mb-3" style={{ 'maxWidth': '12rem' }}>
          <label htmlFor="loopCount" className="form-label">Number of loops</label>
          <input type="number" id="loopCount" className="form-control" min={1} max={UFH_MAX_LOOPS} value={flows.length} onChange={(e) => setLoopCount(parseInt(e.target.value))} />
        </div>

        <div className="row g-2 mb-3">
          {flows.map((flow, i) => (
            <div className="col-6 col-sm-4 col-md-3" key={i}>
              <label htmlFor={`loop${i}`} className="form-label">Loop {i + 1} (l/min)</label>
              <input type="number" id={`loop${i}`} className="form-control" min={0.1} max={99.9} step={0.1} placeholder="2.5" value={flow} onChange={(e) => setFlow(i, e.target.value)} />
            </div>
          ))}
        </div>

        <p className="mb-2">Total: <strong>{totalLpm.toFixed(1)} l/min</strong> &middot; Heat meter now: <strong>{formatLpm(calibration.heatMeterFlowLph)}</strong></p>

        {!calibration.canCalibrate && calibration.calibrationBlocker &&
          <div className="alert alert-warning">{calibration.calibrationBlocker}</div>}

        {message && <div className={`alert alert-${message.kind}`}>{message.text}</div>}

        <button type="button" className="btn btn-primary" disabled={!valid || !calibration.canCalibrate || saving} onClick={save}>
          {saving ? 'Saving…' : 'Save calibration'}
        </button>

        {calibration.refFlowLph != null &&
          <p className="form-text mb-0 mt-2">
            Last calibrated with {calibration.loops.length} loop{calibration.loops.length === 1 ? '' : 's'} totalling {calibration.loops.reduce((s, f) => s + f, 0).toFixed(1)} l/min
            against a heat meter flow of {formatLpm(calibration.refFlowLph)}{calibratedAt ? `, ${calibratedAt}` : ''}.
          </p>}
      </div>
    </div>
  );
}

export default UfhLoops;

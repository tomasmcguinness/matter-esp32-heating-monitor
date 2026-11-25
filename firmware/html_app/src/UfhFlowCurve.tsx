import { useEffect, useRef } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import { formatLpm, formatShare } from "./emitter";
import type { UfhCalibration } from "./emitter";

// The manifold's predicted flow against the heat meter's, from its calibration points (TASK-42.2).
//
// The curve is the device's own (`ufh.predicted`, worked out by the ufh_flow_at() that the output
// uses), so what is drawn here is what the figures are. Between points the line is solid: there it
// is interpolated from readings. Below the lowest point and above the highest it is dashed: there
// it is only scaled in proportion from the end point. The points are dots, and a vertical line
// marks the heat meter's flow now.
//
const SOLID = "#0d6efd";
const NOW = "#dc3545";

function UfhFlowCurve({ ufh }: { ufh: UfhCalibration }) {
  const holder = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);

  const predicted = ufh.predicted;
  const now = ufh.heatMeterFlowLph;

  useEffect(() => {
    if (!holder.current || predicted.length === 0) {
      return;
    }

    const x = predicted.map(p => p.heatMeterFlowLph);
    const lpm = predicted.map(p => p.manifoldFlowLph / 60);

    // A stretch is solid when both its ends are within the points; a point belongs to both
    // stretches so the solid and dashed lines meet there.
    const solid = predicted.map((p, i) => (!p.extrapolated ? lpm[i] : null));
    const dashed = predicted.map((p, i) => (p.extrapolated || p.point ? lpm[i] : null));
    const dots = predicted.map((p, i) => (p.point ? lpm[i] : null));

    const drawNow = (u: uPlot) => {
      if (now == null) {
        return;
      }

      const left = u.valToPos(now, "x", true);

      if (left < u.bbox.left || left > u.bbox.left + u.bbox.width) {
        return;
      }

      const ctx = u.ctx;
      ctx.save();
      ctx.strokeStyle = NOW;
      ctx.lineWidth = 1.5 * devicePixelRatio;
      ctx.setLineDash([4 * devicePixelRatio, 3 * devicePixelRatio]);
      ctx.beginPath();
      ctx.moveTo(left, u.bbox.top);
      ctx.lineTo(left, u.bbox.top + u.bbox.height);
      ctx.stroke();
      ctx.restore();
    };

    const build = () => {
      plot.current?.destroy();
      plot.current = new uPlot(
        {
          width: holder.current!.clientWidth,
          height: 240,
          scales: { x: { time: false } },
          series: [
            { label: "Heat meter", value: (_u, v) => (v == null ? "--" : `${v} l/h`) },
            {
              label: "Interpolated",
              stroke: SOLID,
              width: 2,
              spanGaps: false,
              value: (_u, v) => (v == null ? "--" : `${v.toFixed(1)} l/min`),
            },
            {
              label: "Extrapolated",
              stroke: SOLID,
              width: 1.5,
              dash: [6, 4],
              spanGaps: false,
              value: (_u, v) => (v == null ? "--" : `${v.toFixed(1)} l/min`),
            },
            {
              label: "Calibration point",
              stroke: SOLID,
              fill: SOLID,
              width: 0,
              paths: () => null,
              points: { show: true, size: 9, fill: SOLID, stroke: "#fff" },
              value: (_u, v) => (v == null ? "--" : `${v.toFixed(1)} l/min`),
            },
          ],
          axes: [
            { label: "Heat meter flow (l/h)" },
            { label: "Manifold flow (l/min)" },
          ],
          hooks: { draw: [drawNow] },
        },
        [x, solid, dashed, dots],
        holder.current!
      );
    };

    build();

    const observer = new ResizeObserver(() => build());
    observer.observe(holder.current);

    return () => {
      observer.disconnect();
      plot.current?.destroy();
      plot.current = null;
    };
  }, [predicted, now]);

  if (predicted.length === 0) {
    return null;
  }

  // The table: each point, plus a few round heat meter flows across the range, so the curve can be
  // read off without hovering. Every row is an entry of the device's own curve.
  const step = Math.max(1, Math.floor(predicted.length / 4));
  const rows = predicted.filter((p, i) => p.point || (p.heatMeterFlowLph > 0 && i % step === 0));

  return (
    <div className="card mb-3">
      <div className="card-header">Predicted flow</div>
      <div className="card-body">
        <div ref={holder} />
        <p className="form-text mt-1">
          Solid between calibration points, where the flow is interpolated; dashed outside them, where it is only scaled in
          proportion from the nearest point. <span style={{ 'color': NOW }}>The red line</span> is the heat meter's flow now
          {now != null && ufh.currentFlowLph != null ? <> ({now} l/h, giving {formatLpm(ufh.currentFlowLph)})</> : null}.
        </p>
        <table className="table table-sm table-bordered mb-0">
          <thead>
            <tr>
              <th>Heat meter</th>
              <th>Manifold</th>
              <th>Share</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {rows.map(p => (
              <tr key={`${p.heatMeterFlowLph}-${p.point}`}>
                <td>{p.heatMeterFlowLph} l/h</td>
                <td>{formatLpm(p.manifoldFlowLph)}</td>
                <td>{formatShare(p.manifoldFlowLph, p.heatMeterFlowLph)}</td>
                <td className="text-muted small">{p.point ? 'calibration point' : p.extrapolated ? 'extrapolated' : 'interpolated'}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

export default UfhFlowCurve;

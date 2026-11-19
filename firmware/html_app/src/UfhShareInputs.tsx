import { useEffect, useState } from "react";
import { NavLink } from "react-router";
import { isUfhManifold, type UfhShares } from "./emitter";

type Manifold = { radiatorId: number; name: string };

// The "Underfloor heating" section of Add Room and Edit Room: one % input per manifold, each
// showing how much of that manifold the other rooms already take. The device refuses a save that
// takes a manifold past 100%, and so does the form, before it is sent.
//
function UfhShareInputs({ roomId, shares, onChange }: {
  roomId?: number;
  shares: UfhShares;
  onChange: (shares: UfhShares) => void;
}) {
  const [manifolds, setManifolds] = useState<Manifold[] | null>(null);
  const [takenElsewhere, setTakenElsewhere] = useState<Record<number, number>>({});

  useEffect(() => {
    let cancelled = false;

    // One after the other: the device serves HTTP from a single task.
    const load = async () => {
      const radiatorsResponse = await fetch("/api/radiators");
      const radiators = radiatorsResponse.ok ? await radiatorsResponse.json() : [];

      const roomsResponse = await fetch("/api/rooms");
      const rooms = roomsResponse.ok ? await roomsResponse.json() : [];

      if (cancelled) {
        return;
      }

      const taken: Record<number, number> = {};

      for (const room of rooms) {
        if (room.roomId === roomId) {
          continue;
        }

        for (const share of room.ufhShares ?? []) {
          taken[share.emitterId] = (taken[share.emitterId] ?? 0) + share.pct;
        }
      }

      setTakenElsewhere(taken);
      setManifolds(radiators.filter(isUfhManifold).sort((a: Manifold, b: Manifold) => a.radiatorId - b.radiatorId));
    };

    load();

    return () => { cancelled = true; };
  }, [roomId]);

  if (manifolds === null || manifolds.length === 0) {
    // No manifolds, no section: most rooms in most houses only have radiators.
    return null;
  }

  return (
    <>
      <h3>Underfloor Heating</h3>
      <hr />
      <p className="text-muted">
        The share of each manifold's output that heats this room. Leave it at 0 if the manifold doesn't serve it.
      </p>
      {manifolds.map(manifold => {
        const elsewhere = takenElsewhere[manifold.radiatorId] ?? 0;
        const available = Math.max(0, 100 - elsewhere);
        const value = shares[manifold.radiatorId] ?? 0;

        return (
          <div className="mb-3" key={manifold.radiatorId}>
            <label htmlFor={`ufh-${manifold.radiatorId}`} className="form-label">
              <NavLink to={`/emitters/${manifold.radiatorId}`}>{manifold.name}</NavLink> (%)
            </label>
            <input type="number" min={0} max={available} step={1} className="form-control" id={`ufh-${manifold.radiatorId}`}
              value={value || ''} placeholder="0"
              onChange={(e) => {
                const pct = e.target.value === '' ? 0 : Math.max(0, Math.min(100, parseInt(e.target.value)));
                onChange({ ...shares, [manifold.radiatorId]: Number.isNaN(pct) ? 0 : pct });
              }} />
            <div className="form-text">
              {elsewhere > 0 ? `Other rooms take ${elsewhere}%, so up to ${available}% is left.` : 'No other room has a share of it.'}
            </div>
          </div>
        );
      })}
    </>
  );
}

export default UfhShareInputs;

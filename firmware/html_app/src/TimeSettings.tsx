import { useEffect, useState } from "react";

interface TimeConfig {
  zone: string;
  ntp_server: string;
  active_zone: string;
  zones: string[];
}

function TimeSettings() {
  const [config, setConfig] = useState<TimeConfig | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);
  const [saved, setSaved] = useState(false);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    const fetchConfig = async () => {
      try {
        const response = await fetch("/api/time/config");

        if (response.ok) {
          setConfig(await response.json());
        } else {
          setError(`The device returned ${response.status}.`);
        }
      } catch {
        setError("Could not reach the device.");
      }
    };

    fetchConfig();
  }, []);

  const update = (changes: Partial<TimeConfig>) => {
    setSaved(false);
    setConfig((current) => (current ? { ...current, ...changes } : current));
  };

  const save = async (e: React.FormEvent) => {
    e.preventDefault();

    if (!config) {
      return;
    }

    setBusy(true);
    setError(undefined);
    setSaved(false);

    try {
      const response = await fetch("/api/time/config", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ zone: config.zone, ntp_server: config.ntp_server }),
      });

      if (response.ok) {
        setConfig(await response.json());
        setSaved(true);
      } else {
        setError(await response.text());
      }
    } catch {
      setError("Could not reach the device.");
    }

    setBusy(false);
  };

  return (
    <>
      <h2>Time</h2>
      <p>
        History is filed by local day, so the time zone decides where one day's charts end and
        the next begin. Both settings take effect when the device next restarts.
      </p>

      {error && <div className="alert alert-danger">{error}</div>}
      {saved && <div className="alert alert-success">Saved. Restart the device to apply it.</div>}

      {config && (
        <form onSubmit={save} style={{ maxWidth: "520px" }}>
          <div className="mb-3">
            <label className="form-label" htmlFor="timeZone">Time zone</label>
            <select
              className="form-control"
              id="timeZone"
              value={config.zone}
              onChange={(e) => update({ zone: e.target.value })}
            >
              {config.zones.map((zone) => (
                <option key={zone} value={zone}>{zone.replace(/_/g, " ")}</option>
              ))}
            </select>
            {config.zone !== config.active_zone && (
              <div className="form-text">
                Running on {config.active_zone.replace(/_/g, " ")} until the device restarts.
              </div>
            )}
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="ntpServer">NTP server</label>
            <input
              className="form-control"
              type="text"
              id="ntpServer"
              placeholder="pool.ntp.org"
              maxLength={63}
              value={config.ntp_server}
              onChange={(e) => update({ ntp_server: e.target.value })}
            />
          </div>

          <div className="mb-3">
            <button className="btn btn-primary" type="submit" disabled={busy}>Save</button>
          </div>
        </form>
      )}
    </>
  );
}

export default TimeSettings;

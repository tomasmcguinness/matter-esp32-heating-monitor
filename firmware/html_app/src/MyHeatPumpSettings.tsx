import { useEffect, useState } from "react";

interface MyHeatPumpConfig {
  enabled: boolean;
  base_url: string;
  key_set: boolean;
  interval_s: number;
}

interface MyHeatPumpStatus {
  last_success: number | null;
  last_error: { time: number; http_status: number; response: string } | null;
  posts_ok: number;
  posts_failed: number;
  last_payload_bytes: number;
}

const STATUS_REFRESH_MS = 10000;

function formatTime(ts: number | null): string {
  return ts ? new Date(ts * 1000).toLocaleString() : "Never";
}

async function loadStatus(): Promise<MyHeatPumpStatus | undefined> {
  try {
    const response = await fetch("/api/myheatpump/status");

    return response.ok ? await response.json() : undefined;
  } catch {
    return undefined;
  }
}

function MyHeatPumpSettings() {
  const [config, setConfig] = useState<MyHeatPumpConfig | undefined>(undefined);
  const [apiKey, setApiKey] = useState("");
  const [clearKey, setClearKey] = useState(false);
  const [status, setStatus] = useState<MyHeatPumpStatus | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);
  const [saved, setSaved] = useState(false);
  const [busy, setBusy] = useState(false);

  const refreshStatus = () => {
    loadStatus().then((loaded) => {
      if (loaded) {
        setStatus(loaded);
      }
    });
  };

  useEffect(() => {
    const fetchConfig = async () => {
      try {
        const response = await fetch("/api/myheatpump/config");

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
    refreshStatus();

    const timer = setInterval(refreshStatus, STATUS_REFRESH_MS);
    return () => clearInterval(timer);
  }, []);

  const update = (changes: Partial<MyHeatPumpConfig>) => {
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
      const response = await fetch("/api/myheatpump/config", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        // An empty key tells the device to keep the one it has.
        body: JSON.stringify({
          enabled: config.enabled,
          base_url: config.base_url.trim(),
          api_key: apiKey,
          clear_key: clearKey && apiKey === "",
          interval_s: config.interval_s,
        }),
      });

      if (response.ok) {
        setConfig(await response.json());
        setApiKey("");
        setClearKey(false);
        setSaved(true);
        refreshStatus();
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
      <h2>MyHeatPump</h2>
      <p>
        Posts the current status &mdash; system readings, rooms, radiators and underfloor manifolds
        &mdash; to a MyHeatPump dashboard server's <code>/api/status</code>. Only the latest status
        is sent; a post that fails is not retried until the next interval.
      </p>

      {error && <div className="alert alert-danger">{error}</div>}
      {saved && <div className="alert alert-success">Saved.</div>}

      {config && (
        <form onSubmit={save} style={{ maxWidth: "520px" }}>
          <div className="form-check form-switch mb-3">
            <input
              className="form-check-input"
              type="checkbox"
              id="myheatpumpEnabled"
              checked={config.enabled}
              onChange={(e) => update({ enabled: e.target.checked })}
            />
            <label className="form-check-label" htmlFor="myheatpumpEnabled">Enabled</label>
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="myheatpumpUrl">Server URL</label>
            <input
              className="form-control"
              type="url"
              id="myheatpumpUrl"
              placeholder="http://192.168.1.20:5080"
              maxLength={127}
              value={config.base_url}
              onChange={(e) => update({ base_url: e.target.value })}
            />
            <div className="form-text">Nothing is sent while this is empty.</div>
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="myheatpumpKey">API key (optional)</label>
            <input
              className="form-control"
              type="password"
              id="myheatpumpKey"
              autoComplete="off"
              maxLength={128}
              placeholder={config.key_set ? "unchanged" : ""}
              value={apiKey}
              onChange={(e) => { setSaved(false); setApiKey(e.target.value); }}
            />
            {config.key_set && (
              <div className="form-check mt-1">
                <input
                  className="form-check-input"
                  type="checkbox"
                  id="myheatpumpClearKey"
                  checked={clearKey}
                  disabled={apiKey !== ""}
                  onChange={(e) => { setSaved(false); setClearKey(e.target.checked); }}
                />
                <label className="form-check-label" htmlFor="myheatpumpClearKey">Remove the stored key</label>
              </div>
            )}
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="myheatpumpInterval">Interval (seconds)</label>
            <input
              className="form-control"
              type="number"
              id="myheatpumpInterval"
              required
              min={10}
              max={3600}
              value={config.interval_s}
              onChange={(e) => update({ interval_s: Number(e.target.value) })}
            />
          </div>

          <div className="mb-3">
            <button className="btn btn-primary" type="submit" disabled={busy}>Save</button>
          </div>
        </form>
      )}

      {status && (
        <table className="table table-bordered" style={{ maxWidth: "520px" }}>
          <tbody>
            <tr>
              <th style={{ width: "140px" }}>Last success</th>
              <td>{formatTime(status.last_success)}</td>
            </tr>
            <tr>
              <th>Last error</th>
              <td>
                {status.last_error
                  ? <>
                      {formatTime(status.last_error.time)}<br />
                      <code>
                        {status.last_error.http_status ? `HTTP ${status.last_error.http_status}` : "No response"}
                        {status.last_error.response && `: ${status.last_error.response}`}
                      </code>
                    </>
                  : "None"}
              </td>
            </tr>
            <tr>
              <th>Posts</th>
              <td>{status.posts_ok} OK, {status.posts_failed} failed</td>
            </tr>
            <tr>
              <th>Last payload</th>
              <td>{status.last_payload_bytes ? `${status.last_payload_bytes} bytes` : "None yet"}</td>
            </tr>
          </tbody>
        </table>
      )}
    </>
  );
}

export default MyHeatPumpSettings;

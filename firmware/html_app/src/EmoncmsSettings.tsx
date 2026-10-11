import { useEffect, useState } from "react";

interface EmoncmsConfig {
  enabled: boolean;
  base_url: string;
  key_set: boolean;
  node: string;
  interval_s: number;
}

interface EmoncmsResult {
  success: boolean;
  http_status: number;
  response: string;
}

interface EmoncmsStatus {
  last_success: number | null;
  last_error: (EmoncmsResult & { time: number }) | null;
  posts_ok: number;
  posts_failed: number;
  dropped: number;
  buffered: number;
  buffer_capacity: number;
  inputs: string[];
  last_payload_time: number | null;
  last_payload: Record<string, number> | null;
}

const STATUS_REFRESH_MS = 10000;

function formatTime(ts: number | null): string {
  return ts ? new Date(ts * 1000).toLocaleString() : "Never";
}

// A transport failure has no HTTP status, and the response then names the error instead.
function describeResult(result: EmoncmsResult): string {
  const status = result.http_status ? `HTTP ${result.http_status}` : "No response";

  return result.response ? `${status}: ${result.response}` : status;
}

// The status is refreshed on a timer, so a failed fetch is simply picked up next time.
async function loadStatus(): Promise<EmoncmsStatus | undefined> {
  try {
    const response = await fetch("/api/emoncms/status");

    return response.ok ? await response.json() : undefined;
  } catch {
    return undefined;
  }
}

function EmoncmsSettings() {
  const [config, setConfig] = useState<EmoncmsConfig | undefined>(undefined);
  const [apiKey, setApiKey] = useState("");
  const [status, setStatus] = useState<EmoncmsStatus | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);
  const [saved, setSaved] = useState(false);
  const [busy, setBusy] = useState(false);
  const [testResult, setTestResult] = useState<EmoncmsResult | undefined>(undefined);

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
        const response = await fetch("/api/emoncms/config");

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

  const update = (changes: Partial<EmoncmsConfig>) => {
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
      const response = await fetch("/api/emoncms/config", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        // An empty key tells the device to keep the one it has.
        body: JSON.stringify({
          enabled: config.enabled,
          base_url: config.base_url,
          api_key: apiKey,
          node: config.node,
          interval_s: config.interval_s,
        }),
      });

      if (response.ok) {
        setConfig(await response.json());
        setApiKey("");
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

  // Sends what is saved on the device, not what is typed into the form.
  const sendTest = async () => {
    setBusy(true);
    setTestResult(undefined);

    try {
      const response = await fetch("/api/emoncms/test", { method: "POST" });
      setTestResult(await response.json());
      refreshStatus();
    } catch {
      setTestResult({ success: false, http_status: 0, response: "Could not reach the device." });
    }

    setBusy(false);
  };

  return (
    <>
      <h2>emoncms</h2>
      <p>
        Posts the home's readings to an emoncms account. Each input that arrives there becomes a
        feed, so only the nine listed below are sent.
      </p>

      {error && <div className="alert alert-danger">{error}</div>}
      {saved && <div className="alert alert-success">Saved.</div>}

      {config && (
        <form onSubmit={save} style={{ maxWidth: "520px" }}>
          <div className="form-check form-switch mb-3">
            <input
              className="form-check-input"
              type="checkbox"
              id="emoncmsEnabled"
              checked={config.enabled}
              onChange={(e) => update({ enabled: e.target.checked })}
            />
            <label className="form-check-label" htmlFor="emoncmsEnabled">Enabled</label>
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="emoncmsUrl">URL</label>
            <input
              className="form-control"
              type="url"
              id="emoncmsUrl"
              required
              value={config.base_url}
              onChange={(e) => update({ base_url: e.target.value })}
            />
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="emoncmsKey">Write API key</label>
            <input
              className="form-control"
              type="password"
              id="emoncmsKey"
              autoComplete="off"
              placeholder={config.key_set ? "unchanged" : ""}
              value={apiKey}
              onChange={(e) => { setSaved(false); setApiKey(e.target.value); }}
            />
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="emoncmsNode">Node name</label>
            <input
              className="form-control"
              type="text"
              id="emoncmsNode"
              required
              maxLength={32}
              value={config.node}
              onChange={(e) => update({ node: e.target.value })}
            />
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="emoncmsInterval">Interval (seconds)</label>
            <input
              className="form-control"
              type="number"
              id="emoncmsInterval"
              required
              min={10}
              max={3600}
              value={config.interval_s}
              onChange={(e) => update({ interval_s: Number(e.target.value) })}
            />
          </div>

          <div className="mb-3">
            <button className="btn btn-primary" type="submit" disabled={busy}>Save</button>{" "}
            <button className="btn btn-outline-secondary" type="button" disabled={busy} onClick={sendTest}>
              Send test
            </button>
          </div>
        </form>
      )}

      {testResult && (
        <div className={`alert ${testResult.success ? "alert-success" : "alert-warning"}`}>
          {testResult.success ? "emoncms accepted the test post. " : "The test post was not accepted. "}
          <code>{describeResult(testResult)}</code>
        </div>
      )}

      {status && (
        <>
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
                    ? <>{formatTime(status.last_error.time)}<br /><code>{describeResult(status.last_error)}</code></>
                    : "None"}
                </td>
              </tr>
              <tr>
                <th>Posts</th>
                <td>
                  {status.posts_ok} OK, {status.posts_failed} failed
                  {status.dropped > 0 && <>, {status.dropped} dropped</>}
                </td>
              </tr>
              <tr>
                <th>Buffered</th>
                <td>{status.buffered} of {status.buffer_capacity}</td>
              </tr>
            </tbody>
          </table>

          <h5>Last payload</h5>
          <p className="text-muted">
            {status.last_payload_time ? `Sent with the time ${formatTime(status.last_payload_time)}.` : "Nothing has been sent yet."}
          </p>

          <table className="table table-bordered table-sm" style={{ maxWidth: "520px" }}>
            <tbody>
              {status.inputs.map((key) => {
                const value = status.last_payload?.[key];

                return (
                  <tr key={key}>
                    <th style={{ width: "200px" }}><code>{key}</code></th>
                    <td>{value === undefined ? <em className="text-muted">missing</em> : value}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </>
      )}
    </>
  );
}

export default EmoncmsSettings;

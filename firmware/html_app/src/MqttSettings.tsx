import { useEffect, useState } from "react";

interface MqttConfig {
  url: string;
  username: string;
  password_set: boolean;
  connected: boolean;
}

function MqttSettings() {
  const [config, setConfig] = useState<MqttConfig | undefined>(undefined);
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | undefined>(undefined);
  const [saved, setSaved] = useState(false);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    const fetchConfig = async () => {
      try {
        const response = await fetch("/api/mqtt/config");

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

  const update = (changes: Partial<MqttConfig>) => {
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
      const response = await fetch("/api/mqtt/config", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        // An empty password tells the device to keep the one it has.
        body: JSON.stringify({ url: config.url, username: config.username, password }),
      });

      if (response.ok) {
        setConfig(await response.json());
        setPassword("");
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
      <h2>MQTT</h2>
      <p>
        The broker that readings and Home Assistant discovery are published to. Leave the URL
        empty to turn MQTT off.
      </p>

      {error && <div className="alert alert-danger">{error}</div>}
      {saved && (
        <div className="alert alert-success">
          Saved. The device is reconnecting; reload the page in a few seconds to see whether it
          got through.
        </div>
      )}

      {config && (
        <form onSubmit={save} style={{ maxWidth: "520px" }}>
          <div className="mb-3">
            <label className="form-label" htmlFor="mqttUrl">URL</label>
            <input
              className="form-control"
              type="text"
              id="mqttUrl"
              placeholder="mqtt://192.168.1.10"
              value={config.url}
              onChange={(e) => update({ url: e.target.value })}
            />
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="mqttUsername">Username</label>
            <input
              className="form-control"
              type="text"
              id="mqttUsername"
              autoComplete="off"
              maxLength={64}
              value={config.username}
              onChange={(e) => update({ username: e.target.value })}
            />
          </div>

          <div className="mb-3">
            <label className="form-label" htmlFor="mqttPassword">Password</label>
            <input
              className="form-control"
              type="password"
              id="mqttPassword"
              autoComplete="off"
              placeholder={config.password_set ? "unchanged" : ""}
              value={password}
              onChange={(e) => { setSaved(false); setPassword(e.target.value); }}
            />
          </div>

          <div className="mb-3">
            <button className="btn btn-primary" type="submit" disabled={busy}>Save</button>{" "}
            <span className="text-muted">
              {config.url === "" ? "Not configured" : config.connected ? "Connected" : "Not connected"}
            </span>
          </div>
        </form>
      )}
    </>
  );
}

export default MqttSettings;

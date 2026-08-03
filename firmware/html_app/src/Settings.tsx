import { useEffect, useState } from "react";
import { QRCodeSVG } from "qrcode.react";

interface DeviceInfo {
  v: number;
  name: string;
  host: string;
  ip: string | null;
  id: string;
  token: string;
}

function Settings() {

  let [info, setInfo] = useState<DeviceInfo | undefined>(undefined);
  let [error, setError] = useState<string | undefined>(undefined);
  let [showToken, setShowToken] = useState<boolean>(false);

  useEffect(() => {
    const fetchInfo = async () => {
      try {
        var response = await fetch("/api/info");

        if (response.ok) {
          setInfo(await response.json());
        } else {
          setError(`The device returned ${response.status}.`);
        }
      } catch {
        setError("Could not reach the device.");
      }
    };

    fetchInfo();
  }, []);

  // The companion app scans this verbatim, so it has to be exactly what /api/info returned --
  // don't reformat it or re-order the keys here.
  const payload = info ? JSON.stringify(info) : "";

  return (
    <>
      <h1>Settings</h1>
      <hr />

      {error && <div className="alert alert-danger">{error}</div>}

      {info && <>
        <h2>Companion App</h2>
        <p>
          Scan this with the Heating Monitor iOS app to pair it with this device. The code
          contains the address of this device and its pairing token, so treat it like a password.
        </p>

        <div style={{ background: '#ffffff', display: 'inline-block', padding: '16px', marginBottom: '20px' }}>
          <QRCodeSVG value={payload} size={256} level="M" />
        </div>

        <table className="table table-bordered" style={{ maxWidth: '520px' }}>
          <tbody>
            <tr>
              <th style={{ width: '140px' }}>Name</th>
              <td>{info.name}</td>
            </tr>
            <tr>
              <th>Hostname</th>
              <td>{info.host}</td>
            </tr>
            <tr>
              <th>IP Address</th>
              <td>{info.ip ?? <em>Not available</em>}</td>
            </tr>
            <tr>
              <th>Device ID</th>
              <td><code>{info.id}</code></td>
            </tr>
            <tr>
              <th>Pairing Token</th>
              <td>
                {showToken
                  ? <code>{info.token}</code>
                  : <span className="text-muted">Hidden</span>}
                <button
                  className="btn btn-sm btn-outline-secondary"
                  style={{ marginLeft: '10px' }}
                  onClick={() => setShowToken(!showToken)}>
                  {showToken ? "Hide" : "Show"}
                </button>
              </td>
            </tr>
          </tbody>
        </table>
      </>}

      {!info && !error && <div className="alert alert-info">Loading...</div>}
    </>
  )
}

export default Settings;

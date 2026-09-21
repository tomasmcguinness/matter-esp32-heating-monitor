import { useEffect, useState } from "react";

interface DeviceInfo {
  name: string;
  host: string;
  url: string;
  ip: string | null;
}

function Settings() {

  let [info, setInfo] = useState<DeviceInfo | undefined>(undefined);
  let [error, setError] = useState<string | undefined>(undefined);

  useEffect(() => {
    const fetchInfo = async () => {
      try {
        var response = await fetch("/api/companion/info");

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

  return (
    <>
      <h1>Settings</h1>
      <hr />

      {error && <div className="alert alert-danger">{error}</div>}

      {info && <>
        <h2>Companion App</h2>
        <p>
          The Matter Controller Companion app adds this controller by address. Enter the URL
          below, or the IP address if the hostname doesn't resolve on your phone. There is no
          pairing step and no code to scan.
        </p>

        <table className="table table-bordered" style={{ maxWidth: '520px' }}>
          <tbody>
            <tr>
              <th style={{ width: '140px' }}>Name</th>
              <td>{info.name}</td>
            </tr>
            <tr>
              <th>URL</th>
              <td><code>{info.url}</code></td>
            </tr>
            <tr>
              <th>Hostname</th>
              <td>{info.host}</td>
            </tr>
            <tr>
              <th>IP Address</th>
              <td>{info.ip ? <code>http://{info.ip}</code> : <em>Not available</em>}</td>
            </tr>
          </tbody>
        </table>
      </>}

      {!info && !error && <div className="alert alert-info">Loading...</div>}
    </>
  )
}

export default Settings;

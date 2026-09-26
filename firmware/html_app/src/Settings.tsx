import { useEffect, useState } from "react";

interface DeviceInfo {
  name: string;
  url: string | null;
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
          The Matter Controller Companion app adds this controller by address. Type the address
          below into the app exactly as it appears &mdash; scheme and IP, no trailing slash. There
          is no pairing step and no code to scan.
        </p>

        <p className="text-muted">
          It has to be the IP address. This device does not answer to{' '}
          <code>heating-monitor.local</code> &mdash; the Matter stack owns the mDNS port, so
          nothing advertises that name, and a phone asked to resolve it will reach some other
          machine on your network instead. The address changes if the DHCP lease does.
        </p>

        <table className="table table-bordered" style={{ maxWidth: '520px' }}>
          <tbody>
            <tr>
              <th style={{ width: '140px' }}>Name</th>
              <td>{info.name}</td>
            </tr>
            <tr>
              <th>Address</th>
              <td>
                {info.url
                  ? <code>{info.url}</code>
                  : <em>No network address yet</em>}
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

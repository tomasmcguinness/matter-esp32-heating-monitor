import { useEffect, useState } from "react";

interface SdStatus {
  state: "absent" | "no_filesystem" | "mounted";
  capacityBytes?: number;
  totalBytes?: number | null;
  freeBytes?: number | null;
  error?: string;
  reason?: string;
}

interface DeviceStatus {
  firmware: string;
  idf: string;
  uptimeSeconds: number;
  freeHeap: number;
  minFreeHeap: number;
  psramFree: number;
  psramTotal: number;
  resetReason: string;
}

interface Status {
  sd: SdStatus;
  device: DeviceStatus;
}

// Card and heap sizes are quoted in the decimal units the card is sold in, so a 32GB card
// reads as 32 GB rather than 29.8.
function formatBytes(bytes: number): string {
  const units = ["B", "KB", "MB", "GB", "TB"];
  let value = bytes;
  let unit = 0;

  while (value >= 1000 && unit < units.length - 1) {
    value /= 1000;
    unit++;
  }

  return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
}

function formatUptime(seconds: number): string {
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);

  if (days > 0) {
    return `${days}d ${hours}h ${minutes}m`;
  }

  if (hours > 0) {
    return `${hours}h ${minutes}m`;
  }

  return `${minutes}m`;
}

function SdCard({ sd }: { sd: SdStatus }) {
  if (sd.state === "absent") {
    return (
      <div className="alert alert-info">
        No SD card is installed, so history is not being recorded. Fit a FAT32-formatted card
        and restart the device.
        {sd.error && <> <span className="text-muted">({sd.error})</span></>}
      </div>
    );
  }

  if (sd.state === "no_filesystem") {
    return (
      <div className="alert alert-warning">
        An SD card
        {sd.capacityBytes ? ` of ${formatBytes(sd.capacityBytes)}` : ""} was detected, but it
        could not be mounted, so history is not being recorded. {sd.reason}
      </div>
    );
  }

  const total = sd.totalBytes ?? null;
  const free = sd.freeBytes ?? null;
  const used = total !== null && free !== null ? total - free : null;
  const percentUsed = total !== null && used !== null && total > 0
    ? Math.round((used / total) * 100)
    : null;

  return (
    <>
      <table className="table table-bordered" style={{ maxWidth: '520px' }}>
        <tbody>
          <tr>
            <th style={{ width: '140px' }}>Capacity</th>
            <td>{sd.capacityBytes ? formatBytes(sd.capacityBytes) : <em>Unknown</em>}</td>
          </tr>
          <tr>
            <th>Used</th>
            <td>{used !== null ? formatBytes(used) : <em>Unknown</em>}</td>
          </tr>
          <tr>
            <th>Free</th>
            <td>{free !== null ? formatBytes(free) : <em>Unknown</em>}</td>
          </tr>
        </tbody>
      </table>

      {percentUsed !== null &&
        <div className="progress" style={{ maxWidth: '520px' }} role="progressbar"
          aria-valuenow={percentUsed} aria-valuemin={0} aria-valuemax={100}>
          <div className="progress-bar" style={{ width: `${percentUsed}%` }}>{percentUsed}%</div>
        </div>}
    </>
  );
}

function Status() {

  const [status, setStatus] = useState<Status | undefined>(undefined);
  const [error, setError] = useState<string | undefined>(undefined);

  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const response = await fetch("/api/status");

        if (response.ok) {
          setStatus(await response.json());
        } else {
          setError(`The device returned ${response.status}.`);
        }
      } catch {
        setError("Could not reach the device.");
      }
    };

    fetchStatus();
  }, []);

  return (
    <>
      <h1>Status</h1>
      <hr />

      {error && <div className="alert alert-danger">{error}</div>}

      {status && <>
        <h2>SD Card</h2>
        <SdCard sd={status.sd} />

        <h2>Device</h2>
        <table className="table table-bordered" style={{ maxWidth: '520px' }}>
          <tbody>
            <tr>
              <th style={{ width: '140px' }}>Firmware</th>
              <td>{status.device.firmware}</td>
            </tr>
            <tr>
              <th>ESP-IDF</th>
              <td>{status.device.idf}</td>
            </tr>
            <tr>
              <th>Uptime</th>
              <td>{formatUptime(status.device.uptimeSeconds)}</td>
            </tr>
            <tr>
              <th>Last Restart</th>
              <td>{status.device.resetReason}</td>
            </tr>
            <tr>
              <th>Free Heap</th>
              <td>
                {formatBytes(status.device.freeHeap)}
                <span className="text-muted"> (low water mark {formatBytes(status.device.minFreeHeap)})</span>
              </td>
            </tr>
            <tr>
              <th>PSRAM</th>
              <td>
                {formatBytes(status.device.psramFree)} free of {formatBytes(status.device.psramTotal)}
              </td>
            </tr>
          </tbody>
        </table>
      </>}

      {!status && !error && <div className="alert alert-info">Loading...</div>}
    </>
  )
}

export default Status;

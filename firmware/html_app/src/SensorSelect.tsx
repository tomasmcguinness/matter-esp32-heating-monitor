import { useEffect, useState } from "react";

// The shape of one entry from GET /api/sensors -- see sensors_get_handler in main/app_main.cpp.
// One entry per endpoint device type, so a multi-device-type endpoint appears more than once.
//
type Sensor = {
    nodeId: number;
    endpointId: number;
    deviceTypeId: number;
    nodeName: string | null;
    endpointName: string | null;
};

// A form needs one of these per sensor it configures -- Edit Home has six -- and they all mount
// together, so each one fetching for itself put six identical requests on the device at once.
// Sharing the in-flight request collapses that to one. It is dropped as soon as it settles, so
// coming back to a form still picks up any devices paired in the meantime.
//
let sensorsRequest: Promise<Sensor[]> | null = null;

const fetchSensors = () => {
    if (!sensorsRequest) {
        sensorsRequest = fetch('/api/sensors')
            .then(response => response.json())
            .finally(() => { sensorsRequest = null; });
    }

    return sensorsRequest;
};

const SensorSelect = ({ title, required, deviceType, selectedSensor, id = undefined, onSelectedSensorChange }: { title: string, required: boolean, deviceType: number, selectedSensor: string | undefined, id:  string | undefined, onSelectedSensorChange: (id:string) => void }) => {

    const [sensors, setSensors] = useState<Sensor[]>([]);

    useEffect(() => {
        let cancelled = false;

        fetchSensors().then(data => {
            if (!cancelled) {
                setSensors(data);
            }
        });

        return () => { cancelled = true; };
    }, []);

    // Present sensors that match the device type
    const sensorOptions = sensors.filter(s => s.deviceTypeId === deviceType).map(s => {
        const key = `${s.nodeId}|${s.endpointId}`;
        return <option key={key} value={key}>{s.nodeName} - {s.endpointName} (0x{s.nodeId.toString(16).toUpperCase()} - 0x{s.endpointId})</option>;
    });

    return (
        <>
            <label htmlFor={id} className="form-label">{title} {required && <span style={{ 'color': 'red' }}>*</span>}</label>
            <select name="sensor" className="form-control" id={id} value={selectedSensor || ''} onChange={(e) => onSelectedSensorChange(e.target.value)} required={required}>
                <option value=''></option>
                {sensorOptions}
            </select>
        </>
    );
}

export default SensorSelect;

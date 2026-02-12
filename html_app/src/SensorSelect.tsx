import { useEffect, useState } from "react";

const SensorSelect = ({ title, required, deviceType, selectedSensor, onSelectedSensorChange }: { title: string, required: boolean, deviceType: number, selectedSensor: string | undefined, onSelectedSensorChange: (id:string) => void }) => {

    const [sensors, setSensors] = useState<[]>([]);

    useEffect(() => {
        fetch('/api/sensors').then(response => response.json()).then(data => {
            // Present sensors that match the device type
            setSensors(data.filter((s: any) => s.deviceTypeId === deviceType));
        });
    }, []);

    let sensorOptions = sensors.map((s: any) => {
        var key = `${s.nodeId}|${s.endpointId}`;
        return <option key={key} value={key}>{s.nodeName} - {s.endpointName} (0x{s.nodeId.toString(16).toUpperCase()} - 0x{s.endpointId})</option>;
    });

    return ([
        <label htmlFor="sensor" className="form-label">{title} {required && <span style={{ 'color': 'red' }}>*</span>}</label>,
        <select name="sensor" className="form-control" id="temperatureSensor" value={selectedSensor || ''} onChange={(e) => onSelectedSensorChange(e.target.value)} required={required}>
            <option value=''></option>
            {sensorOptions}
        </select>]);
}

export default SensorSelect;
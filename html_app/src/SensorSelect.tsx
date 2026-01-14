import { useEffect, useState } from "react";

const SensorSelect = ({ title, selectedSensor, onSelectedSensorChange }: { title: string, selectedSensor: string | undefined, onSelectedSensorChange: (id:string) => void }) => {

    const [sensors, setSensors] = useState<[]>([]);

    useEffect(() => {
        fetch('/api/sensors').then(response => response.json()).then(data => {
            setSensors(data);
        });
    }, []);

    let sensorOptions = sensors.map((s: any) => {
        var key = `${s.nodeId}|${s.endpointId}`;
        return <option key={key} value={key}>{s.nodeName} - {s.endpointName} (0x{s.nodeId} - 0x{s.endpointId})</option>;
    });

    return ([
        <label htmlFor="sensor" className="form-label">{title} <span style={{ 'color': 'red' }}>*</span></label>,
        <select name="sensor" className="form-control" id="temperatureSensor" value={selectedSensor || ''} onChange={(e) => onSelectedSensorChange(e.target.value)} required={true}>
            <option value=''></option>
            {sensorOptions}
        </select>]);
}

export default SensorSelect;
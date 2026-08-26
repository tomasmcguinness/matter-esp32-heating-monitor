type BatteryProps = {
    powerSource: number | undefined,
    percent: number | null | undefined,
    voltage: number | null | undefined
};

const Battery = ({ powerSource, percent, voltage }: BatteryProps) => {

    // BatPercentRemaining and BatVoltage are both optional in Matter, so a device may report
    // either, both or neither.
    const level = (percent === null || percent === undefined) ? null : Math.min(Math.max(percent, 0), 100);
    const volts = (voltage === null || voltage === undefined) ? null : (voltage / 1000).toFixed(2);

    // The firmware only ever fills these in for battery powered devices, so a value is enough to
    // go on. powerSource is only consulted to decide whether a device we have nothing for yet is
    // one we expect a reading from -- it stays 0 when the PowerSource cluster isn't on endpoint 0.
    if (level === null && volts === null) {
        return powerSource === 2 ? (<span className="text-muted">&mdash;</span>) : null;
    }

    let colourClass: string | undefined = undefined;

    if (level !== null) {
        colourClass = level <= 15 ? "text-danger" : level <= 30 ? "text-warning" : "text-success";
    }

    // The battery body runs from x=2 to x=12, so a full charge is 10 units wide.
    const fillWidth = level === null ? 0 : (level / 100) * 10;

    return (
        <span className={colourClass} style={{ whiteSpace: 'nowrap' }}>
            <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16" style={{ marginRight: '5px', verticalAlign: 'text-bottom' }}>
                <path d="M0 6a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v4a2 2 0 0 1-2 2H2a2 2 0 0 1-2-2zm2-1a1 1 0 0 0-1 1v4a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V6a1 1 0 0 0-1-1z" />
                <path d="M16 8a1.5 1.5 0 0 1-1.5 1.5v-3A1.5 1.5 0 0 1 16 8" />
                {fillWidth > 0 && <rect x="2" y="6" width={fillWidth} height="4" />}
            </svg>
            {level !== null && `${level}%`}
            {level !== null && volts !== null && " · "}
            {volts !== null && `${volts} V`}
        </span>
    );
}

export default Battery;

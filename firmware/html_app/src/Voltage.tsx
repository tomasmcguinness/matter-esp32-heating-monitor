import { Children } from 'react';

// ElectricalPowerMeasurement reports voltage in millivolts.
//
function Voltage({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var millivolts = parseInt(firstChild.toString());

    if (isNaN(millivolts)) {
        return <span>-</span>;
    }

    return <span>{(millivolts / 1000.0).toFixed(1)}V</span>;
}

export default Voltage;

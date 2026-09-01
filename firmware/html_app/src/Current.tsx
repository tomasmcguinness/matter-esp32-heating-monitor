import { Children } from 'react';

// ElectricalPowerMeasurement reports current in milliamps.
//
function Current({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var milliamps = parseInt(firstChild.toString());

    if (isNaN(milliamps)) {
        return <span>-</span>;
    }

    return <span>{(milliamps / 1000.0).toFixed(2)}A</span>;
}

export default Current;

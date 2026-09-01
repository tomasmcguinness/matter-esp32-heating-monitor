import { Children } from 'react';

// ElectricalPowerMeasurement reports active power in milliwatts. Unlike Power, a reading of zero is
// meaningful here -- it means nothing is drawing -- so only a missing value renders as a dash.
//
function ElectricalPower({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var milliwatts = parseInt(firstChild.toString());

    if (isNaN(milliwatts)) {
        return <span>-</span>;
    }

    return <span>{Math.round(milliwatts / 1000.0)}W</span>;
}

export default ElectricalPower;

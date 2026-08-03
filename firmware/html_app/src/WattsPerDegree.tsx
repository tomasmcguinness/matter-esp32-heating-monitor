import { Children } from 'react';

function WattsPerDegree({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var value = parseInt(firstChild.toString());

    if (value === null || value === undefined || value === 0) {
        return <span>-</span>;
    }

    return <span>{value}W/°C</span>;
}

export default WattsPerDegree;
import { Children } from 'react';

function Temperature({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var temp = parseInt(firstChild.toString());

    if (temp === null || temp === undefined || temp === 0) {
        return <span>-</span>;
    }

    var formattedTemp = (temp / 100.0).toFixed(1);

    return <span>{formattedTemp}°C</span>;
}

export default Temperature;
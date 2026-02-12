import { Children } from 'react';

function FlowRate({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var temp = parseInt(firstChild.toString());

    if (temp === null || temp === undefined) {
        return <span>-</span>;
    }

    var formattedTemp = (temp / 36.0).toFixed(2);

    return <span>{formattedTemp}L/s</span>;
}

export default FlowRate;
import { Children } from 'react';

function Power({ children }: { children: any }) {

    const firstChild = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    var temp = parseInt(firstChild.toString());

    if (temp === null || temp === undefined || temp === 0) {
        return <span>-</span>;
    }

    return <span>{temp}W</span>;
}

export default Power;
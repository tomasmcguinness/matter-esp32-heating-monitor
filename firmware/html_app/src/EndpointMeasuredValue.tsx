import { Children } from 'react';

function EndpointMeasuredValue({ children }: { children: any }) {

    const firstChild:any = Children.toArray(children)[0];

    if (firstChild === null || firstChild === undefined) {
        return <span>-</span>;
    }

    return <span>{firstChild.deviceTypeId}</span>;
}

export default EndpointMeasuredValue;
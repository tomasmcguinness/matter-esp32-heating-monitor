import { Children } from 'react';

function Temperature({ children } : { children: any }) {

    const firstChild = Children.toArray(children)[0];
    
    var temp = parseInt(firstChild.toString());

    return <span>{temp / 100.0}°C</span>;
}

export default Temperature;
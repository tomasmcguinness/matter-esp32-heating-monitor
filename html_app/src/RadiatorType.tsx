import { Children } from 'react';

function RadiatorType({ children }: { children: any }) {

  const firstChild = Children.toArray(children)[0];

  if (firstChild === null || firstChild === undefined) {
    return <span>-</span>;
  }

  switch(parseInt(firstChild.toString())) {
    case 0:
      return <span>Designer</span>;
    case 1:
      return <span>Towel</span>;
    case 10:
      return <span>Type 10 (P1)</span>;
    case 11:
      return <span>Type 11 (K1)</span>;
    case 20:
      return <span>Type 20</span>;
    case 21:
      return <span>Type 21 (P+)</span>;
    case 22:
      return <span>Type 22 (K2)</span>;
    case 33:
      return <span>Type 33 (K3)</span>;
    case 44:
      return <span>Type 44 (K4)</span>;
    default:
      return <span>Unknown Value: {firstChild}</span>
  }
}

export default RadiatorType

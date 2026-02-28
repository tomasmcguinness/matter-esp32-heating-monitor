import { WebSocketContext } from './WSContext.jsx';
import { useContext, useEffect, useRef } from "react";
import { Network } from "vis-network";
import { DataSet } from "vis-data";

const nodes = new DataSet<any>([]);
const edges = new DataSet<any>([]);

var data = {
  nodes: nodes,
  edges: edges
};
var options = {};

function ThreadNetwork() {

  const visJsRef = useRef(null);

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  const buildNetwork = async () => {
    await fetch("/api/network", { method: "POST" });
  };

  useEffect(() => {

    subscribe("network", (message: any) => {
      console.log({ message });
      edges.update({ from: message.extAddress, to: message.neighborExtAddress, label: message.averageRssi.toString(), arrows: { to: true } });

      var color = 'blue';
      if (message.isChild) {
        color = 'purple';
      }

      nodes.update({ id: message.neighborExtAddress, color, font: { color: '#FFFFFF' } });
    });

    const fetchNodes = async () => {
      var response = await fetch("http://192.168.1.105/api/nodes");

      if (response.ok) {

        let data = await response.json();

        console.log({ data });

        data.forEach((element: any) => {
          nodes.update({ id: element.extAddress, label: `0x${element.nodeId.toString(16).toUpperCase()}` });
        });

        buildNetwork();
      }
    };

    visJsRef.current && new Network(visJsRef.current, data, options);

    fetchNodes();

    return () => {
      unsubscribe("network");
    }
  }, [subscribe]);

  return <div style={{ width: '100%', height: '800px' }} ref={visJsRef} />;
}

export default ThreadNetwork;
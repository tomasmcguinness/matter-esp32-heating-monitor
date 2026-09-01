import { useEffect, createContext, useRef, useCallback, useMemo } from "react"

const WebSocketContext = createContext()

function WebSocketProvider({ children }) {

    var url = new URL('/ws', window.location.href);

    url.protocol = url.protocol.replace('http', 'ws');

    const socketUrl = url.href;

    const ws = useRef(null);
    const channels = useRef({});

    // Stable identities: consumers list these in their useEffect deps, so re-creating them on every
    // render would make every page resubscribe on every render.
    //
    const subscribe = useCallback((channel, callback) => {
        console.log('Subscription received for channel ' + channel);
        channels.current[channel] = callback
    }, []);

    const unsubscribe = useCallback((channel) => {
        console.log('Subscription removed for channel ' + channel);
        delete channels.current[channel]
    }, []);

    useEffect(() => {
        let socket = null;
        let retryTimer = null;
        let disposed = false;

        const connect = () => {
            socket = new WebSocket(socketUrl);
            ws.current = socket;

            socket.onopen = () => {
                console.log('WS open');
            }

            socket.onmessage = (message) => {
                const data = JSON.parse(message.data)

                const callback = channels.current[data.channel];

                if (callback) {
                    callback(data)
                } else {
                    console.log('No subscriber for channel ' + data.channel);
                }
            }

            // The device reboots on an OTA update and the cable gets unplugged, so a closed socket has
            // to come back on its own -- otherwise every page just quietly stops updating until the
            // user reloads. `disposed` keeps the retry from outliving the provider.
            //
            socket.onclose = () => {
                console.log('WS closed');

                if (disposed) {
                    return;
                }

                retryTimer = setTimeout(connect, 2000);
            }

            socket.onerror = () => {
                socket.close();
            }
        }

        connect();

        return () => {
            disposed = true;
            clearTimeout(retryTimer);

            if (socket) {
                socket.onclose = null;
                socket.close();
            }
        }
    }, [socketUrl])

    const value = useMemo(() => ({ subscribe, unsubscribe }), [subscribe, unsubscribe]);

    return (<WebSocketContext.Provider value={value}>
        {children}
    </WebSocketContext.Provider>
    )
}

export { WebSocketContext, WebSocketProvider }

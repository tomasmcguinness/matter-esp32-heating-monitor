import { useEffect, createContext, useRef } from "react"

const WebSocketContext = createContext()

function WebSocketProvider({ children }) {

    var url = new URL('/ws', window.location.href);

    url.protocol = url.protocol.replace('http', 'ws');

    const socketUrl = url.href;

    const ws = useRef(null)
    const channels = useRef({}) // maps each channel to the callback
    /* called from a component that registers a callback for a channel */
    const subscribe = (channel, callback) => {
        channels.current[channel] = callback
    }
    /* remove callback  */
    const unsubscribe = (channel) => {
        delete channels.current[channel]
    }
    useEffect(() => {
        ws.current = new WebSocket(socketUrl)
        ws.current.onopen = () => {
            console.log('WS open');
            ws.current.send('anything');
        }
        ws.current.onclose = () => { console.log('WS close') }
        ws.current.onmessage = (message) => {
            console.log('WS message data received')
            const { type, ...data } = JSON.parse(message.data)
            const channel = `${type}_${data.channel}`

            if (channels.current[channel]) {
                channels.current[channel](data)
            }
        }
        return () => { ws.current.close() }
}, [])

return (<WebSocketContext.Provider value={{ subscribe, unsubscribe }}>
    {children}
</WebSocketContext.Provider>
)
}

export { WebSocketContext, WebSocketProvider }
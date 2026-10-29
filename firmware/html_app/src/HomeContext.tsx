import { createContext, useContext, useEffect, useState, type ReactNode } from "react";
import { WebSocketContext } from "./WSContext.jsx";

// The latest GET /api/home payload, kept up to date from the "home" channel for the whole app.
//
// This is the one subscriber to "home". WSContext holds a single callback per channel, so a page
// subscribing to it as well would replace this one -- and its unsubscribe on leaving would stop
// the page header updating everywhere. Pages read the payload through useHome() instead.
//
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type HomeData = any;

const HomeContext = createContext<HomeData | undefined>(undefined);

function HomeProvider({ children }: { children: ReactNode }) {
  const [home, setHome] = useState<HomeData | undefined>(undefined);

  const { subscribe, unsubscribe } = useContext(WebSocketContext);

  // Fetched on mount as well, so the header has values straight away rather than waiting for
  // whatever sensor reports next. The firmware builds the push from the same function as the GET,
  // so either replaces the payload wholesale.
  //
  useEffect(() => {
    const fetchHome = async () => {
      try {
        const response = await fetch("/api/home");

        if (response.ok) {
          setHome(await response.json());
        }
      } catch {
        // The push will fill it in once the device is reachable.
      }
    };

    fetchHome();
  }, []);

  useEffect(() => {
    subscribe("home", setHome);

    return () => {
      unsubscribe("home");
    };
  }, [subscribe, unsubscribe]);

  return <HomeContext.Provider value={home}>{children}</HomeContext.Provider>;
}

// undefined until the first fetch or push arrives.
function useHome(): HomeData | undefined {
  return useContext(HomeContext);
}

// eslint-disable-next-line react-refresh/only-export-components
export { HomeProvider, useHome };

import { NavLink, Navigate, Routes, Route, useLocation, useParams } from "react-router";
import './App.css'
import Home from './Home.tsx'
import EditHome from './EditHome.tsx'
import Layout from './Layout.tsx'

import Device from './Device.tsx'
import Devices from './Devices.tsx'
import AddDevice from './AddDevice.tsx'
import EditDevice from "./EditDevice.tsx";
import CommissioningWindow from "./CommissioningWindow.tsx";

import Emitter from './Emitter.tsx'
import Emitters from './Emitters.tsx'
import AddEmitter from './AddEmitter.tsx'
import EditEmitter from "./EditEmitter.tsx";

import Room from './Room.tsx'
import Rooms from './Rooms.tsx'
import AddRoom from './AddRoom.tsx'
import EditRoom from "./EditRoom.tsx";
import RoomHistory from "./RoomHistory.tsx";

import { WebSocketProvider } from './WSContext.jsx'
import { HomeProvider } from './HomeContext.tsx'
import History from "./History.tsx";
import Settings from "./Settings.tsx";
import Status from "./Status.tsx";
import Diagnostics from "./Diagnostics.tsx";

// The Radiators tab became Emitters. Old links and bookmarks still land on the same page.
function RadiatorRedirect({ suffix = "" }: { suffix?: string }) {
  const { radiatorId } = useParams();
  return <Navigate replace to={radiatorId ? `/emitters/${radiatorId}${suffix}` : `/emitters${suffix}`} />;
}

function App() {

  // The layout schematic widens with the radiator count, so it gets the whole screen rather than
  // Bootstrap's fixed-width container. Every other page keeps the container.
  const { pathname } = useLocation();
  const containerClass = pathname === "/layout" ? "container-fluid" : "container";

  return (
    <>
      <nav className="navbar navbar-expand-lg">
        <div className="container">
          <a className="navbar-brand" href="#">Heating Monitor</a>
          <button className="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav" aria-controls="navbarNav" aria-expanded="false" aria-label="Toggle navigation">
            <span className="navbar-toggler-icon"></span>
          </button>
          <div className="collapse navbar-collapse" id="navbarNav">
            <ul className="navbar-nav">
              <li className="nav-item">
                <NavLink className="nav-link" to="/">Home</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/layout">Layout</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/rooms">Rooms</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/emitters">Emitters</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/devices">Devices</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/history">History</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/settings">Settings</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/status">Status</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/diagnostics">Diagnostics</NavLink>
              </li>
            </ul>
          </div>
        </div>
      </nav>
      <WebSocketProvider>
        <HomeProvider>
        <div className={containerClass}>
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="/edit" element={<EditHome />} />

            <Route path="/layout" element={<Layout />} />

            <Route path="/rooms" element={<Rooms />} />
            <Route path="/rooms/add" element={<AddRoom />} />
            <Route path="/rooms/:roomId" element={<Room />} />
            <Route path="/rooms/:roomId/edit" element={<EditRoom />} />
            <Route path="/rooms/:roomId/history" element={<RoomHistory />} />

            <Route path="/emitters" element={<Emitters />} />
            <Route path="/emitters/add" element={<AddEmitter />} />
            <Route path="/emitters/:radiatorId" element={<Emitter />} />
            <Route path="/emitters/:radiatorId/edit" element={<EditEmitter />} />

            <Route path="/radiators" element={<RadiatorRedirect />} />
            <Route path="/radiators/add" element={<RadiatorRedirect suffix="/add" />} />
            <Route path="/radiators/:radiatorId" element={<RadiatorRedirect />} />
            <Route path="/radiators/:radiatorId/edit" element={<RadiatorRedirect suffix="/edit" />} />

            <Route path="/devices" element={<Devices />} />
            <Route path="/devices/add" element={<AddDevice />} />
            <Route path="/devices/:nodeId" element={<Device />} />
            <Route path="/devices/:nodeId/edit" element={<EditDevice />} />
            <Route path="/devices/:nodeId/commissioning" element={<CommissioningWindow />} />

            <Route path="/history" element={<History />} />

            <Route path="/settings" element={<Settings />} />

            <Route path="/status" element={<Status />} />

            <Route path="/diagnostics" element={<Diagnostics />} />

          </Routes>
        </div>
        </HomeProvider>
      </WebSocketProvider>
    </>
  )
}

export default App

import { NavLink, Routes, Route } from "react-router";
import './App.css'
import Home from './Home.tsx'
import EditHome from './EditHome.tsx'

import Device from './Device.tsx'
import Devices from './Devices.tsx'
import AddDevice from './AddDevice.tsx'

import Radiator from './Radiator.tsx'
import Radiators from './Radiators.tsx'
import AddRadiator from './AddRadiator.tsx'
import EditRadiator from "./EditRadiator.tsx";

import Room from './Room.tsx'
import Rooms from './Rooms.tsx'
import AddRoom from './AddRoom.tsx'
import EditRoom from "./EditRoom.tsx";

import { WebSocketProvider } from './WSContext.jsx'

function App() {

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
                <NavLink className="nav-link" to="/rooms">Rooms</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/radiators">Radiators</NavLink>
              </li>
              <li className="nav-item">
                <NavLink className="nav-link" to="/devices">Devices</NavLink>
              </li>
            </ul>
          </div>
        </div>
      </nav>
      <WebSocketProvider>
        <div className="container">
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="/edit" element={<EditHome />} />

            <Route path="/rooms" element={<Rooms />} />
            <Route path="/rooms/add" element={<AddRoom />} />
            <Route path="/rooms/:roomId" element={<Room />} />
            <Route path="/rooms/:roomId/edit" element={<EditRoom />} />

            <Route path="/radiators" element={<Radiators />} />
            <Route path="/radiators/add" element={<AddRadiator />} />
            <Route path="/radiators/:radiatorId" element={<Radiator />} />
            <Route path="/radiators/:radiatorId/edit" element={<EditRadiator />} />

            <Route path="/devices" element={<Devices />} />
            <Route path="/devices/add" element={<AddDevice />} />
            <Route path="/devices/:nodeId" element={<Device />} />
          </Routes>
        </div>
      </WebSocketProvider>
    </>
  )
}

export default App

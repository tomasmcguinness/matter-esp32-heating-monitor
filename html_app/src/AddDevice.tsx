function Devices() {

  const startCommissioning = () => {
    fetch('/api/commissioning', { method: 'POST' });
  }

  return (
    <>
      <h1>Add Device</h1>
      <form>

        <div className="mb-3">
          <label htmlFor="exampleFormControlInput1" className="form-label">Manual Setup Code</label>
          <input type="email" className="form-control" id="exampleFormControlInput1" placeholder="1111-111-1111" />
        </div>

        <button type="submit" className="btn btn-primary" onClick={startCommissioning}>Add Device</button>
      </form>
    </>
  )
}

export default Devices

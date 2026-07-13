import { useState, useEffect } from 'react'

function App() {
  const [activeTab, setActiveTab] = useState('home')
  const [ipAddress, setIpAddress] = useState('192.168.4.1')
  const [demoMode, setDemoMode] = useState(true)
  const [isConnected, setIsConnected] = useState(true)

  // System status
  const [status, setStatus] = useState({
    state: 'IDLE',
    weight: 0.0,
    almondsTarget: 20,
    walnutsTarget: 20,
    cashewsTarget: 0,
    soakHours: 4,
    soakTimeRemaining: 0,
    rinse: true,
    drain: true,
    inlet: false,
    drainValve: false,
    uvc: false
  })

  // Local demo simulation variables
  const [demoTimer, setDemoTimer] = useState(null)
  const [simulatedWeight, setSimulatedWeight] = useState(0.0)

  // Fetch status from the physical ESP32 device if not in demo mode
  useEffect(() => {
    if (demoMode) {
      setIsConnected(true)
      return
    }

    const interval = setInterval(() => {
      fetch(`http://${ipAddress}/status`, { mode: 'cors' })
        .then(res => res.json())
        .then(data => {
          setStatus(prev => ({
            ...prev,
            state: data.state,
            weight: data.weight,
            almondsTarget: data.almondsTarget,
            walnutsTarget: data.walnutsTarget,
            cashewsTarget: data.cashewsTarget,
            soakHours: data.soakHours,
            soakTimeRemaining: data.soakTimeRemaining,
            rinse: data.rinse,
            drain: data.drain,
            inlet: data.inlet,
            drainValve: data.drainValve,
            uvc: data.uvc
          }))
          setIsConnected(true)
        })
        .catch(() => {
          setIsConnected(false)
        })
    }, 1000)

    return () => clearInterval(interval)
  }, [demoMode, ipAddress])

  // Local Demo Mode State Machine Simulator
  useEffect(() => {
    if (!demoMode) return

    let timer
    if (status.state === 'CHECK_CUP') {
      // Simulate waiting for cup detection
      timer = setTimeout(() => {
        setStatus(prev => ({ ...prev, state: 'DISPENSING ALMONDS', weight: 0.0 }))
      }, 2500)
    } 
    else if (status.state === 'DISPENSING ALMONDS') {
      if (status.almondsTarget > 0) {
        timer = setTimeout(() => {
          setStatus(prev => ({ 
            ...prev, 
            weight: prev.almondsTarget, 
            state: prev.walnutsTarget > 0 ? 'DISPENSING WALNUTS' : (prev.cashewsTarget > 0 ? 'DISPENSING CASHEWS' : (prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL'))
          }))
        }, 2000)
      } else {
        setStatus(prev => ({ 
          ...prev, 
          state: prev.walnutsTarget > 0 ? 'DISPENSING WALNUTS' : (prev.cashewsTarget > 0 ? 'DISPENSING CASHEWS' : (prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL'))
        }))
      }
    } 
    else if (status.state === 'DISPENSING WALNUTS') {
      if (status.walnutsTarget > 0) {
        timer = setTimeout(() => {
          setStatus(prev => ({ 
            ...prev, 
            weight: prev.weight + prev.walnutsTarget, 
            state: prev.cashewsTarget > 0 ? 'DISPENSING CASHEWS' : (prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL')
          }))
        }, 2000)
      } else {
        setStatus(prev => ({ 
          ...prev, 
          state: prev.cashewsTarget > 0 ? 'DISPENSING CASHEWS' : (prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL')
        }))
      }
    }
    else if (status.state === 'DISPENSING CASHEWS') {
      if (status.cashewsTarget > 0) {
        timer = setTimeout(() => {
          setStatus(prev => ({ 
            ...prev, 
            weight: prev.weight + prev.cashewsTarget, 
            state: prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL'
          }))
        }, 2000)
      } else {
        setStatus(prev => ({ 
          ...prev, 
          state: prev.rinse ? 'RINSE_FILL' : 'SOAK_FILL'
        }))
      }
    }
    else if (status.state === 'RINSE_FILL') {
      setStatus(prev => ({ ...prev, inlet: true, drainValve: false }))
      timer = setTimeout(() => {
        setStatus(prev => ({ ...prev, state: 'RINSE_WAIT', inlet: false, weight: prev.weight + 150 }))
      }, 2000)
    }
    else if (status.state === 'RINSE_WAIT') {
      timer = setTimeout(() => {
        setStatus(prev => ({ ...prev, state: 'RINSE_DRAIN' }))
      }, 2000)
    }
    else if (status.state === 'RINSE_DRAIN') {
      setStatus(prev => ({ ...prev, drainValve: true }))
      timer = setTimeout(() => {
        setStatus(prev => ({ ...prev, state: 'SOAK_FILL', drainValve: false, weight: prev.weight - 150 }))
      }, 2000)
    }
    else if (status.state === 'SOAK_FILL') {
      setStatus(prev => ({ ...prev, inlet: true }))
      timer = setTimeout(() => {
        setStatus(prev => ({ 
          ...prev, 
          state: 'SOAKING', 
          inlet: false, 
          weight: prev.weight + 200,
          soakTimeRemaining: prev.soakHours * 5 // Speedup: 5s per hour in simulation
        }))
      }, 2000)
    }
    else if (status.state === 'SOAKING') {
      if (status.soakTimeRemaining > 0) {
        timer = setTimeout(() => {
          setStatus(prev => {
            const nextRemaining = prev.soakTimeRemaining - 1
            // Flash UV-C for 1s every 5s
            const uvcActive = nextRemaining % 5 === 4 || nextRemaining % 5 === 3
            return {
              ...prev,
              soakTimeRemaining: nextRemaining,
              uvc: uvcActive
            }
          })
        }, 1000)
      } else {
        setStatus(prev => ({ ...prev, state: prev.drain ? 'SOAK_DRAIN' : 'READY', uvc: false }))
      }
    }
    else if (status.state === 'SOAK_DRAIN') {
      setStatus(prev => ({ ...prev, drainValve: true }))
      timer = setTimeout(() => {
        setStatus(prev => ({ ...prev, state: 'READY', drainValve: false, weight: prev.weight - 200 }))
      }, 2500)
    }

    return () => clearTimeout(timer)
  }, [status.state, status.soakTimeRemaining, demoMode])

  // Trigger actions (Start / Abort / Rinse)
  const handleAction = (action) => {
    if (demoMode) {
      if (action === 'start') {
        setStatus(prev => ({ ...prev, state: 'CHECK_CUP', weight: 0.0 }))
      } else if (action === 'stop') {
        setStatus(prev => ({
          ...prev,
          state: 'IDLE',
          weight: 0.0,
          inlet: false,
          drainValve: false,
          uvc: false
        }))
      } else if (action === 'rinse') {
        setStatus(prev => ({ ...prev, state: 'RINSE_FILL', weight: 0.0 }))
      }
      return
    }

    // Physical board request
    let cmd = action === 'start' ? 'start' : (action === 'stop' ? 'stop' : 'rinse')
    fetch(`http://${ipAddress}/action?cmd=${cmd}`, { mode: 'cors' })
      .catch(() => alert('Failed to connect to Smart Soak device API.'))
  }

  // Update target portion configurations
  const handleConfigChange = (key, val) => {
    setStatus(prev => {
      const next = { ...prev, [key]: parseInt(val) }
      if (!demoMode) {
        fetch(`http://${ipAddress}/config?almonds=${next.almondsTarget}&walnuts=${next.walnutsTarget}&cashews=${next.cashewsTarget}&soak=${next.soakHours}&rinse=${next.rinse ? 1 : 0}&drain=${next.drain ? 1 : 0}`, { mode: 'cors' })
      }
      return next
    })
  }

  return (
    <div className="bg-background text-on-surface font-sans min-h-screen pb-32">
      {/* Header */}
      <header className="fixed top-0 left-0 w-full h-16 z-50 bg-background/80 backdrop-blur-xl border-b border-white/5 flex justify-center">
        <div className="max-w-lg w-full px-6 flex justify-between items-center">
          <div className="flex items-center gap-2">
            <span className="material-symbols-outlined text-primary font-bold">settings_remote</span>
            <h1 className="text-xl font-bold text-on-surface">Smart Soak</h1>
          </div>
          
          <div className="flex items-center gap-4">
            {/* Demo Mode Toggle */}
            <div className="flex items-center gap-2">
              <span className="text-xs text-outline">Demo Mode</span>
              <input 
                type="checkbox" 
                checked={demoMode} 
                onChange={(e) => setDemoMode(e.target.checked)}
                className="w-8 h-4 bg-gray-700 checked:bg-primary rounded-full cursor-pointer appearance-none relative before:content-[''] before:absolute before:w-4 before:h-4 before:bg-white before:rounded-full before:transition-all checked:before:translate-x-4" 
              />
            </div>

            <div className={`flex items-center gap-2 px-3 py-1 rounded-full border ${isConnected ? 'bg-secondary/10 border-secondary/20 text-secondary' : 'bg-error/10 border-error/20 text-error'}`}>
              <span className={`w-2 h-2 rounded-full ${isConnected ? 'bg-secondary shadow-[0_0_8px_rgba(78,222,163,0.8)]' : 'bg-error'}`}></span>
              <span className="text-xs uppercase tracking-wider font-semibold">{isConnected ? 'Connected' : 'Offline'}</span>
            </div>
          </div>
        </div>
      </header>

      {/* Main Content */}
      <main className="pt-24 px-6 max-w-lg mx-auto space-y-8">
        
        {activeTab === 'home' && (
          <>
            {/* Status Card */}
            <section className="glass-card rounded-2xl p-6 flex justify-between items-center relative overflow-hidden">
              <div className="absolute -right-4 -top-4 w-24 h-24 bg-primary/10 rounded-full blur-2xl"></div>
              <div>
                <p className="text-xs text-outline uppercase tracking-widest">System Status</p>
                <p className="text-xl font-bold text-on-surface">Cycle: <span className="text-primary">{status.state}</span></p>
              </div>
              <div className="w-10 h-10 rounded-full bg-surface-container flex items-center justify-center">
                <span className="material-symbols-outlined text-outline">power_settings_new</span>
              </div>
            </section>

            {/* Circular Progress Dial */}
            <section className="flex flex-col items-center justify-center py-4">
              <div className="relative w-64 h-64 flex items-center justify-center group">
                <div className="absolute inset-0 bg-primary/5 rounded-full blur-3xl opacity-50"></div>
                <div className="absolute inset-0 rounded-full border-[8px] border-surface-container"></div>
                
                {/* Conic Gradient Progress Indicator */}
                <div 
                  className="absolute inset-0 rounded-full border-[8px] border-transparent progress-ring-gradient opacity-40 rotate-[-90deg]" 
                  style={{
                    maskImage: 'radial-gradient(circle, transparent 64%, black 65%)',
                    WebkitMaskImage: 'radial-gradient(circle, transparent 64%, black 65%)'
                  }}
                ></div>

                {/* Glass Center Piece */}
                <div className="w-[85%] h-[85%] rounded-full glass-card shadow-2xl flex flex-col items-center justify-center text-center p-6 glow-purple z-10">
                  {status.state === 'IDLE' ? (
                    <div 
                      onClick={() => handleAction('start')}
                      className="w-16 h-16 rounded-full bg-primary flex items-center justify-center mb-2 hover:scale-110 active:scale-95 transition-transform duration-300 cursor-pointer"
                    >
                      <span className="material-symbols-outlined text-on-primary text-4xl font-bold">play_arrow</span>
                    </div>
                  ) : (
                    <div 
                      onClick={() => handleAction('stop')}
                      className="w-16 h-16 rounded-full bg-error flex items-center justify-center mb-2 hover:scale-110 active:scale-95 transition-transform duration-300 cursor-pointer"
                    >
                      <span className="material-symbols-outlined text-white text-4xl">stop</span>
                    </div>
                  )}
                  
                  <p className="text-lg font-bold text-on-surface">
                    {status.state === 'SOAKING' 
                      ? `${Math.floor(status.soakTimeRemaining / 5)}h ${status.soakTimeRemaining % 5}m left` 
                      : (status.state === 'IDLE' ? 'Ready to start' : 'Processing...')}
                  </p>
                  <p className="text-[10px] text-outline uppercase tracking-wider mt-1">
                    {status.state === 'IDLE' ? 'Tap play to start' : `Weight: ${status.weight.toFixed(1)}g`}
                  </p>
                </div>
              </div>
            </section>

            {/* Cups / Containers Status Grid */}
            <section className="space-y-4">
              <div className="flex justify-between items-end">
                <h2 className="text-lg font-bold text-on-surface">Containers</h2>
                <button 
                  onClick={() => setActiveTab('recipes')}
                  className="flex items-center gap-1 text-primary text-sm font-semibold hover:underline"
                >
                  <span className="material-symbols-outlined text-sm">edit</span> Edit
                </button>
              </div>
              
              <div className="space-y-3">
                {/* Almonds */}
                <div className="glass-card rounded-xl p-4 border-l-4 border-l-primary flex justify-between items-center">
                  <div className="flex items-center gap-3">
                    <div className="w-10 h-10 rounded-full bg-surface-container-lowest flex items-center justify-center">
                      <span className="material-symbols-outlined text-primary">nutrition</span>
                    </div>
                    <div>
                      <p className="text-sm font-semibold text-on-surface">Almonds</p>
                      <p className="text-xs text-outline">Target: {status.almondsTarget}g</p>
                    </div>
                  </div>
                  <div className="text-right">
                    <p className="text-lg font-bold text-primary">{status.almondsTarget}g</p>
                    <p className="text-[10px] text-secondary uppercase font-bold">Optimal</p>
                  </div>
                </div>

                {/* Walnuts */}
                <div className="glass-card rounded-xl p-4 border-l-4 border-l-primary flex justify-between items-center">
                  <div className="flex items-center gap-3">
                    <div className="w-10 h-10 rounded-full bg-surface-container-lowest flex items-center justify-center">
                      <span className="material-symbols-outlined text-primary">spa</span>
                    </div>
                    <div>
                      <p className="text-sm font-semibold text-on-surface">Walnuts</p>
                      <p className="text-xs text-outline">Target: {status.walnutsTarget}g</p>
                    </div>
                  </div>
                  <div className="text-right">
                    <p className="text-lg font-bold text-primary">{status.walnutsTarget}g</p>
                    <p className="text-[10px] text-secondary uppercase font-bold">Optimal</p>
                  </div>
                </div>

                {/* Cashews */}
                <div className={`glass-card rounded-xl p-4 border-l-4 flex justify-between items-center ${status.cashewsTarget === 0 ? 'border-l-outline-variant/30 opacity-60' : 'border-l-primary'}`}>
                  <div className="flex items-center gap-3">
                    <div className="w-10 h-10 rounded-full bg-surface-container-lowest flex items-center justify-center">
                      <span className="material-symbols-outlined text-primary">eco</span>
                    </div>
                    <div>
                      <p className="text-sm font-semibold text-on-surface">Cashews</p>
                      <p className="text-xs text-outline">Target: {status.cashewsTarget}g</p>
                    </div>
                  </div>
                  <div className="text-right">
                    <p className="text-lg font-bold text-primary">{status.cashewsTarget}g</p>
                    <p className={`text-[10px] uppercase font-bold ${status.cashewsTarget === 0 ? 'text-error' : 'text-secondary'}`}>
                      {status.cashewsTarget === 0 ? 'Empty' : 'Optimal'}
                    </p>
                  </div>
                </div>
              </div>
            </section>

            {/* Actuators / Solenoid Valve States */}
            <section className="glass-card rounded-2xl p-4 flex justify-around items-center text-xs">
              <div className="flex items-center gap-2">
                <span className={`w-3 h-3 rounded-full transition-all ${status.inlet ? 'bg-blue-500 shadow-[0_0_10px_#3b82f6]' : 'bg-gray-600'}`}></span>
                <span className="text-outline">Water Inlet</span>
              </div>
              <div className="flex items-center gap-2">
                <span className={`w-3 h-3 rounded-full transition-all ${status.drainValve ? 'bg-orange-500 shadow-[0_0_10px_#f97316]' : 'bg-gray-600'}`}></span>
                <span className="text-outline">Drain Valve</span>
              </div>
              <div className="flex items-center gap-2">
                <span className={`w-3 h-3 rounded-full transition-all ${status.uvc ? 'bg-purple-500 shadow-[0_0_10px_#a855f7]' : 'bg-gray-600'}`}></span>
                <span className="text-outline font-semibold text-purple-400">UV-C Sterilizer</span>
              </div>
            </section>

            {/* Start Buttons */}
            <section className="flex flex-col gap-3 pt-2">
              <button 
                onClick={() => handleAction('start')}
                disabled={status.state !== 'IDLE'}
                className="w-full h-14 bg-gradient-to-r from-purple-500 to-indigo-600 disabled:opacity-50 text-white rounded-xl font-bold shadow-lg hover:opacity-95 active:scale-[0.99] transition-all flex items-center justify-center gap-2"
              >
                <span className="material-symbols-outlined">water_drop</span>
                Start Soak Cycle
              </button>
              <button 
                onClick={() => handleAction('rinse')}
                disabled={status.state !== 'IDLE'}
                className="w-full h-12 border-2 border-outline-variant disabled:opacity-50 text-on-surface rounded-xl font-bold hover:bg-white/5 active:scale-[0.99] transition-all flex items-center justify-center gap-2"
              >
                <span className="material-symbols-outlined">refresh</span>
                Rinse Cycle
              </button>
            </section>
          </>
        )}

        {activeTab === 'recipes' && (
          <section className="glass-card rounded-2xl p-6 space-y-6">
            <h2 className="text-xl font-bold text-primary flex items-center gap-2">
              <span className="material-symbols-outlined">menu_book</span> Portion & Soak Settings
            </h2>

            {/* Almonds Target */}
            <div className="space-y-2">
              <div className="flex justify-between text-sm">
                <span>Almonds Target (grams)</span>
                <span className="text-primary font-bold">{status.almondsTarget}g</span>
              </div>
              <input 
                type="range" 
                min="0" 
                max="100" 
                step="5"
                value={status.almondsTarget}
                onChange={(e) => handleConfigChange('almondsTarget', e.target.value)}
                className="w-full h-1.5 bg-gray-700 rounded-lg appearance-none cursor-pointer accent-primary"
              />
            </div>

            {/* Walnuts Target */}
            <div className="space-y-2">
              <div className="flex justify-between text-sm">
                <span>Walnuts Target (grams)</span>
                <span className="text-primary font-bold">{status.walnutsTarget}g</span>
              </div>
              <input 
                type="range" 
                min="0" 
                max="100" 
                step="5"
                value={status.walnutsTarget}
                onChange={(e) => handleConfigChange('walnutsTarget', e.target.value)}
                className="w-full h-1.5 bg-gray-700 rounded-lg appearance-none cursor-pointer accent-primary"
              />
            </div>

            {/* Cashews Target */}
            <div className="space-y-2">
              <div className="flex justify-between text-sm">
                <span>Cashews Target (grams)</span>
                <span className="text-primary font-bold">{status.cashewsTarget}g</span>
              </div>
              <input 
                type="range" 
                min="0" 
                max="100" 
                step="5"
                value={status.cashewsTarget}
                onChange={(e) => handleConfigChange('cashewsTarget', e.target.value)}
                className="w-full h-1.5 bg-gray-700 rounded-lg appearance-none cursor-pointer accent-primary"
              />
            </div>

            {/* Soak Time Hours */}
            <div className="space-y-2">
              <div className="flex justify-between text-sm">
                <span>Soak Duration (hours)</span>
                <span className="text-primary font-bold">{status.soakHours} hours</span>
              </div>
              <input 
                type="range" 
                min="1" 
                max="12" 
                step="1"
                value={status.soakHours}
                onChange={(e) => handleConfigChange('soakHours', e.target.value)}
                className="w-full h-1.5 bg-gray-700 rounded-lg appearance-none cursor-pointer accent-primary"
              />
            </div>

            {/* Toggles */}
            <div className="flex justify-between items-center pt-2">
              <span className="text-sm">Rinse before soaking</span>
              <input 
                type="checkbox" 
                checked={status.rinse}
                onChange={(e) => setStatus(prev => ({ ...prev, rinse: e.target.checked }))}
                className="w-8 h-4 bg-gray-700 checked:bg-primary rounded-full cursor-pointer appearance-none relative before:content-[''] before:absolute before:w-4 before:h-4 before:bg-white before:rounded-full before:transition-all checked:before:translate-x-4"
              />
            </div>

            <div className="flex justify-between items-center">
              <span className="text-sm">Auto-drain water after soak</span>
              <input 
                type="checkbox" 
                checked={status.drain}
                onChange={(e) => setStatus(prev => ({ ...prev, drain: e.target.checked }))}
                className="w-8 h-4 bg-gray-700 checked:bg-primary rounded-full cursor-pointer appearance-none relative before:content-[''] before:absolute before:w-4 before:h-4 before:bg-white before:rounded-full before:transition-all checked:before:translate-x-4"
              />
            </div>

            <button 
              onClick={() => setActiveTab('home')}
              className="w-full h-12 bg-primary text-background font-bold rounded-xl mt-4 active:scale-[0.99] transition-all"
            >
              Save & Back
            </button>
          </section>
        )}

        {activeTab === 'wifi' && (
          <section className="glass-card rounded-2xl p-6 space-y-6">
            <h2 className="text-xl font-bold text-primary flex items-center gap-2">
              <span className="material-symbols-outlined">wifi</span> IoT Connection Settings
            </h2>
            
            <div className="space-y-4">
              <div className="space-y-2">
                <label className="text-sm block text-outline">Smart Soak IP Address</label>
                <input 
                  type="text" 
                  value={ipAddress}
                  onChange={(e) => setIpAddress(e.target.value)}
                  className="w-full h-12 bg-surface-container border border-white/10 text-on-surface rounded-xl px-4 focus:border-primary outline-none"
                  placeholder="192.168.4.1"
                />
              </div>

              <div className="p-4 bg-surface-container rounded-xl text-xs space-y-2 text-outline">
                <p className="font-semibold text-on-surface">How to connect to the physical device:</p>
                <p>1. Open your smartphone Wi-Fi settings.</p>
                <p>2. Connect to the network: **"Smart-Soak-AP"**.</p>
                <p>3. Return here, turn **Demo Mode OFF**, and input IP: **192.168.4.1**.</p>
              </div>
            </div>
          </section>
        )}

        {activeTab === 'settings' && (
          <section className="glass-card rounded-2xl p-6 space-y-6 text-sm">
            <h2 className="text-xl font-bold text-primary flex items-center gap-2">
              <span className="material-symbols-outlined">settings</span> Device Information
            </h2>
            <div className="space-y-3">
              <div className="flex justify-between py-2 border-b border-white/5">
                <span className="text-outline">App Version</span>
                <span>v1.0.0</span>
              </div>
              <div className="flex justify-between py-2 border-b border-white/5">
                <span className="text-outline">Hardware Version</span>
                <span>v1.0 (Digital Twin)</span>
              </div>
              <div className="flex justify-between py-2 border-b border-white/5">
                <span className="text-outline">Firmware Type</span>
                <span>FreeRTOS ESP32 FSM</span>
              </div>
              <div className="flex justify-between py-2">
                <span className="text-outline">Sterilization</span>
                <span>Active UV-C (Periodic)</span>
              </div>
            </div>
          </section>
        )}

      </main>

      {/* Bottom Navigation Bar */}
      <nav className="fixed bottom-0 left-1/2 -translate-x-1/2 max-w-lg w-full z-50 flex justify-around items-center px-4 py-3 bg-surface-container-lowest/80 backdrop-blur-2xl border-t border-white/10 shadow-lg">
        {/* Home */}
        <button 
          onClick={() => setActiveTab('home')}
          className={`flex flex-col items-center justify-center p-2 rounded-xl transition-all duration-300 ${activeTab === 'home' ? 'text-primary bg-primary-container/20 scale-110' : 'text-outline hover:text-primary-fixed'}`}
        >
          <span className="material-symbols-outlined" style={{ fontVariationSettings: activeTab === 'home' ? "'FILL' 1" : "'FILL' 0" }}>home</span>
          <span className="text-[10px] mt-1">Home</span>
        </button>

        {/* Recipes */}
        <button 
          onClick={() => setActiveTab('recipes')}
          className={`flex flex-col items-center justify-center p-2 rounded-xl transition-all duration-300 ${activeTab === 'recipes' ? 'text-primary bg-primary-container/20 scale-110' : 'text-outline hover:text-primary-fixed'}`}
        >
          <span className="material-symbols-outlined" style={{ fontVariationSettings: activeTab === 'recipes' ? "'FILL' 1" : "'FILL' 0" }}>menu_book</span>
          <span className="text-[10px] mt-1">Settings</span>
        </button>

        {/* Wi-Fi */}
        <button 
          onClick={() => setActiveTab('wifi')}
          className={`flex flex-col items-center justify-center p-2 rounded-xl transition-all duration-300 ${activeTab === 'wifi' ? 'text-primary bg-primary-container/20 scale-110' : 'text-outline hover:text-primary-fixed'}`}
        >
          <span className="material-symbols-outlined" style={{ fontVariationSettings: activeTab === 'wifi' ? "'FILL' 1" : "'FILL' 0" }}>wifi</span>
          <span className="text-[10px] mt-1">Connection</span>
        </button>

        {/* Settings */}
        <button 
          onClick={() => setActiveTab('settings')}
          className={`flex flex-col items-center justify-center p-2 rounded-xl transition-all duration-300 ${activeTab === 'settings' ? 'text-primary bg-primary-container/20 scale-110' : 'text-outline hover:text-primary-fixed'}`}
        >
          <span className="material-symbols-outlined" style={{ fontVariationSettings: activeTab === 'settings' ? "'FILL' 1" : "'FILL' 0" }}>settings</span>
          <span className="text-[10px] mt-1">Info</span>
        </button>
      </nav>
    </div>
  )
}

export default App

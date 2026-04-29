const char* login_html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Login</title>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; display: flex; justify-content: center; align-items: center; height: 100vh; background-color: #e9ecef; margin: 0; }
    .login-box { background: white; padding: 30px; border-radius: 12px; box-shadow: 0 8px 16px rgba(0,0,0,0.1); text-align: center; width: 300px; }
    .login-box h2 { margin-top: 0; color: #333; }
    input { display: block; margin: 15px auto; padding: 12px; width: 90%; border: 1px solid #ccc; border-radius: 6px; box-sizing: border-box; }
    button { padding: 12px 20px; background: #007bff; color: white; border: none; border-radius: 6px; cursor: pointer; width: 90%; font-size: 16px; margin-top: 10px; }
    button:hover { background: #0056b3; }
    #msg { color: #dc3545; margin-top: 15px; min-height: 20px; font-weight: bold; }
  </style>
</head>
<body>
  <div class="login-box">
    <h2>Ball & Beam Control</h2>
    <input type="text" id="user" placeholder="Username">
    <input type="password" id="pass" placeholder="Password">
    <button onclick="login()">Login</button>
    <p id="msg"></p>
  </div>
  <script>
    function login(){
      let u = document.getElementById('user').value;
      let p = document.getElementById('pass').value;
      fetch('/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'username='+encodeURIComponent(u)+'&password='+encodeURIComponent(p)
      }).then(r => r.text()).then(res => {
        if(res === 'OK') window.location.href = '/dashboard';
        else document.getElementById('msg').innerText = 'Invalid credentials!';
      }).catch(err => {
        document.getElementById('msg').innerText = 'Error connecting to ESP32';
      });
    }
  </script>
</body>
</html>
)=====";

const char* index_html = R"=====(
<!DOCTYPE html>
<html>
<head>
  <title>Ball & Beam Dashboard</title>
  <!-- Chart.js loaded from CDN for the graph -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    body { font-family: 'Segoe UI', Arial, sans-serif; margin: 20px; background-color: #e9ecef; color: #333; }
    h2 { border-bottom: 2px solid #ccc; padding-bottom: 10px; }
    .container { display: flex; flex-wrap: wrap; gap: 20px; }
    .card { background: white; padding: 25px; border-radius: 12px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); flex: 1; min-width: 300px; }
    .status-on { color: #28a745; font-weight: bold; font-size: 1.2em; }
    .status-off { color: #dc3545; font-weight: bold; font-size: 1.2em; }
    .input-group { margin-bottom: 15px; display: flex; align-items: center; }
    .input-group label { width: 90px; font-weight: bold; }
    .input-group input { flex: 1; padding: 8px; border: 1px solid #ccc; border-radius: 4px; margin-right: 10px; }
    button { padding: 8px 15px; background: #28a745; color: white; border: none; border-radius: 6px; cursor: pointer; font-weight: bold; }
    button:hover { background: #218838; }
    .btn-logout { background: #dc3545; margin-top: 15px; width: 100%; border-radius: 6px; padding: 12px; }
    .btn-logout:hover { background: #c82333; }
    canvas { width: 100%; height: 350px; }
  </style>
</head>
<body>
  <h2>Ball & Beam Control Dashboard</h2>
  <div class="container">
    <div class="card">
      <h3>System Status</h3>
      <p>UI Connection: <span id="conn-status" class="status-off">OFFLINE</span></p>
      <p>Data Uptime: <span id="uptime">0</span> seconds</p>
      <p style="font-size: 0.9em; color:#666;">(If connection is OFFLINE, live changes will not be sent to ESP32)</p>
      <button class="btn-logout" onclick="logout()">Secure Logout</button>
    </div>
    
    <div class="card">
      <h3>Live Tuning (PID & Setpoint)</h3>
      <div class="input-group">
        <label>Setpoint:</label>
        <input type="number" id="sp" step="1">
        <button onclick="updateParams()">Update</button>
      </div>
      <div class="input-group">
        <label>Kp (Prop):</label>
        <input type="number" id="kp" step="0.1">
        <button onclick="updateParams()">Update</button>
      </div>
      <div class="input-group">
        <label>Ki (Integ):</label>
        <input type="number" id="ki" step="0.01">
        <button onclick="updateParams()">Update</button>
      </div>
      <div class="input-group">
        <label>Kd (Deriv):</label>
        <input type="number" id="kd" step="0.1">
        <button onclick="updateParams()">Update</button>
      </div>
    </div>
  </div>

  <div class="card" style="margin-top: 20px;">
    <h3>Live Tracking: Distance vs Setpoint</h3>
    <canvas id="myChart"></canvas>
  </div>

  <script>
    let ws;
    let uptimeInterval;
    let uptimeSec = 0;
    
    // Setup Chart.js
    const ctx = document.getElementById('myChart').getContext('2d');
    const chart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'Current Distance (mm)', borderColor: '#007bff', backgroundColor: 'rgba(0,123,255,0.1)', data: [], fill: true, tension: 0.2 },
          { label: 'Target Setpoint (mm)', borderColor: '#dc3545', data: [], fill: false, borderDash: [5, 5], tension: 0 }
        ]
      },
      options: { 
        responsive: true, 
        maintainAspectRatio: false,
        animation: false, // Turn off animation for better live performance
        scales: { 
          y: { suggestedMin: 0, suggestedMax: 300, title:{display:true, text:"Millimeters"} },
          x: { title:{display:true, text:"Time"} }
        } 
      }
    });

    function initWebSocket() {
      // Ensure we use 'wss://' if accessed via HTTPS (like Ngrok), otherwise 'ws://'
      const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      ws = new WebSocket(wsProtocol + '//' + window.location.host + '/ws');
      
      ws.onopen = () => {
        document.getElementById('conn-status').innerText = 'ONLINE';
        document.getElementById('conn-status').className = 'status-on';
        uptimeSec = 0;
        clearInterval(uptimeInterval);
        uptimeInterval = setInterval(() => { 
          uptimeSec++; 
          document.getElementById('uptime').innerText = uptimeSec; 
        }, 1000);
      };
      
      ws.onclose = () => {
        document.getElementById('conn-status').innerText = 'OFFLINE';
        document.getElementById('conn-status').className = 'status-off';
        clearInterval(uptimeInterval);
        // Attempt to reconnect every 2 seconds
        setTimeout(initWebSocket, 2000);
      };
      
      ws.onmessage = (evt) => {
        const data = JSON.parse(evt.data);
        
        // Initial setup from ESP32
        if(data.type === 'init') {
          document.getElementById('sp').value = data.sp;
          document.getElementById('kp').value = data.kp;
          document.getElementById('ki').value = data.ki;
          document.getElementById('kd').value = data.kd;
        } 
        // Real-time graph updates
        else if(data.type === 'update') {
          const timeLabel = new Date().toLocaleTimeString();
          chart.data.labels.push(timeLabel);
          chart.data.datasets[0].data.push(data.dist);
          chart.data.datasets[1].data.push(data.sp);
          
          // Keep only the last 50 data points to prevent lag
          if(chart.data.labels.length > 50) {
            chart.data.labels.shift();
            chart.data.datasets[0].data.shift();
            chart.data.datasets[1].data.shift();
          }
          chart.update();
        }
      };
    }

    function updateParams() {
      // Send adjusted parameters back to the ESP32 via WebSocket
      const sp = document.getElementById('sp').value;
      const kp = document.getElementById('kp').value;
      const ki = document.getElementById('ki').value;
      const kd = document.getElementById('kd').value;
      
      const payload = {sp: parseFloat(sp), kp: parseFloat(kp), ki: parseFloat(ki), kd: parseFloat(kd)};
      ws.send(JSON.stringify(payload));
    }
    
    function logout() {
      // Clear session cookie and return to login
      document.cookie = "session=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;";
      window.location.href = "/";
    }

    // Start everything when the page loads
    window.onload = initWebSocket;
  </script>
</body>
</html>
)=====";

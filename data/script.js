const DEBUG = true;
var ws;

window.addEventListener("load", connectWebSocket);

window.addEventListener("error", function(event) {
  logMsg(`JavaScript: ERROR -> ${event.message} (line: ${event.lineno})`)
});

// Handle wake from sleep
document.addEventListener("visibilitychange", function() {
  if (!document.hidden && ws.readyState !== WebSocket.OPEN) {
    logMsg("Wakeup: Reconnecting WebSocket");
    connectWebSocket();
  }
});

function logMsg() {
  if (DEBUG) {
    const msg = Array.from(arguments).join(" ");
    console.log(msg);
  }
}

function connectWebSocket() {
  logMsg("Page Loaded: Connecting WebSocket");
  const url = `ws://${window.location.hostname}/ws`;
  ws = new WebSocket(url);
  // Event handlers must be re-established with each new WebSocket
  ws.onopen = () => {
    logMsg("WebSocket: CONNECTED ->", url);
  };

  ws.onclose = (event) => {
    const codeDesc = getSocketCloseDesc(event.code);
    logMsg('WebSocket: CLOSED ->', event.code, codeDesc);
  };

  ws.onerror = () => {
    logMsg("WebSocket: ERROR");
    if (ws.readyState !== WebSocket.OPEN) {
      setTimeout(connectWebSocket, 2000); // Reconnect
    }
  }

  ws.onmessage = (event) => {
    logMsg("Receive:", event.data);
    const msg = JSON.parse(event.data);
    for (const key in msg) {
      const descriptor = Object.getOwnPropertyDescriptor(settings, key);
      if (typeof descriptor?.set === "function") {
      settings[key] = msg[key];
      }
    }
  };
}

function getSocketCloseDesc(code) {
  switch (code) {
    case 1000:
      return "Normal Closure";
    case 1001:
      return "Going Away";
    case 1002:
      return "Protocol Error";
    case 1003:
      return "Unsupported Data";
    case 1005:
      return "No Status Recvd";
    case 1006:
      return "Abnormal Closure";
    case 1007:
      return "Invalid UTF-8";
    case 1008:
      return "Policy Violation";
    case 1009:
      return "Message Too Big";
    case 1010:
      return "Extension Req";
    case 1011:
      return "Internal Error";
    case 1015:
      return "TLS Handshake";
    default:
      return "Unknown";
  }
};

function getDateTime() {
  const date = new Date();
  const nowTime = date.toLocaleString("en-US", { timeStyle: "medium" });
  const nowDate = date.toLocaleString("en-US", { dateStyle: "medium" });
  return `${nowTime} &nbsp &nbsp ${nowDate}`;
};

const iface = {
  plus: document.getElementById("plus"),
  minus: document.getElementById("minus"),
  adjustTemp: document.getElementById("adjust-temp"),
  submitButton: document.getElementById("submit-button"),
  adjustLight: document.getElementById("adjust-light")
}

const settings = {
  _temp: 0.0,
  get temp() {
    return this._temp;
  },
  set temp(value) {
    if (value % 1 === 0) {
      value = `${value}.0`;
    }
    document.getElementById("temp").textContent = value;
    document.getElementById("refresh-time").innerHTML = getDateTime();
    this._temp = parseFloat(value);
  },

  _setTemp: 0,
  get setTemp() {
    return this._setTemp;
  },
  set setTemp(value) {
    if (value !== this._setTemp) {
      document.getElementById("set-temp").textContent = value;
      document.getElementById("set-temp2").textContent = value;
      iface.adjustTemp.value = value;
      this._setTemp = parseInt(value);
    }
  },

  _colors: [],
  get colors() {
    return this._colors;
  },
  set colors(color_list) {
    iface.adjustLight.innerHTML = "";
    for (let i = 0; i < color_list.length; i++) {
      const newOption = new Option(color_list[i], i);
      iface.adjustLight.add(newOption);
    }
    document.getElementById("adjust-light").value = this._light;
    this._colors = color_list;
  },

  _light: "",
  get light() {
    return this._light;
  },
  set light(value) {
    if (value !== this._light) {
      document.getElementById("adjust-light").value = value;
      if (value === 0) {
        document.getElementById("light").textContent = "OFF";
      } else {
        document.getElementById("light").textContent = iface.adjustLight.options[value].text;
      }
      this._light = value;
    }
  },

  _heat: 0,
  get heat() {
    return this._heat;
  },
  set heat(value) {
    let heat_state;
    if (value === 0) {
      heat_state = "OFF";
    } else {
      heat_state = "ON";
    }
    document.getElementById("heat").textContent = heat_state;
    this._heat = heat_state;
  },

  _min: 40,
  get min() {
    return this._min;
  },
  set min(value) {
    document.getElementById("adjust-temp").min = value;
    this._min = value;
  },

  _max: 106,
  get max() {
    return this._max;
  },
  set max(value) {
    document.getElementById("adjust-temp").max = value;
    this._max = value;
  },
};

iface.submitButton.addEventListener("click", function() {
  const newTemp = parseInt(iface.adjustTemp.value);
  if (newTemp !== settings.setTemp) {
    const msg = JSON.stringify({ setTemp: newTemp });
    logMsg("Sending:", msg);
    ws.send(msg);
  } 
});

iface.plus.addEventListener("click", function() {
  const curTemp = parseInt(iface.adjustTemp.value);
  const newTemp = curTemp + 1;
  if (newTemp <= settings.max) {
    iface.adjustTemp.value = newTemp;
  }
});

iface.minus.addEventListener("click", function() {
  const curTemp = parseInt(iface.adjustTemp.value);
  const newTemp = curTemp - 1;
  if (newTemp >= settings.min) {
    iface.adjustTemp.value = newTemp;
  }
});

// Validate New Temperature
iface.adjustTemp.addEventListener("blur", function() {
  const newTemp = parseInt(iface.adjustTemp.value);
  if (newTemp < settings.min || newTemp > settings.max) {
    iface.adjustTemp.value = settings.setTemp;
  }
});

iface.adjustLight.addEventListener("change", function() {
  const newColor = parseInt(iface.adjustLight.value);
  const msg = JSON.stringify({ light: newColor });
  logMsg("Sending:", msg);
  ws.send(msg);
});


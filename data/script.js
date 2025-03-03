const url = `ws://${window.location.hostname}/ws`;
const minus = document.querySelector(".minus");
const num = document.querySelector(".num");
const plus = document.querySelector(".plus");
const adjustTemp = document.getElementById('adjust-temp');
const submitButton = document.getElementById('submit-button');
let ws;
let maxTemp;
let minTemp;


window.addEventListener('load', (event) => {
  console.log("Page Loaded");
  initWebSocket();
});

function initWebSocket() {
  // Using dot event properties to overwrite
  ws = new WebSocket(url);
  ws.onopen = onOpen;
  ws.onclose = onClose;
  ws.onmessage = onMessage;
  ws.onerror = onError;
}


// Connection Opened
function onOpen(event) {
  console.log("WebSocket: CONNECTED ->", url);
  // Redundant: server already sends defaults onopen
  requestDefaults();
}


// Connection Closed
function onClose(event) {
  console.log("WebSocket: CLOSED");
  // New WebSocket if client went to sleep
  setTimeout(initWebSocket, 2000);
}


// Error
function onError(event) {
  console.log("WebSocket error: ", event);
}


// Message
function onMessage(event) {
  console.log("Receive: ", event.data);
  const msg = JSON.parse(event.data);

  switch (msg.type) {
    case "data":
      document.getElementById('temp').innerHTML = msg.temp;
      document.getElementById('set-temp').innerHTML = msg.setTemp;
      document.getElementById('set-temp2').innerHTML = msg.setTemp;
      document.getElementById('pump').innerHTML = msg.pump;
      document.getElementById('heat').innerHTML = msg.heat;
      document.getElementById("refresh-time").innerHTML = getDateTime();
      break;
    case "defaults":
      adjustTemp.max = msg.max;
      adjustTemp.min = msg.min;
      adjustTemp.value = msg.setTemp;
      maxTemp = msg.max;
      minTemp = msg.min;
      break;
  }
}


const requestDefaults = () => {
  const msg = JSON.stringify({refresh: 1});
  console.log("Sending:", msg);
  ws.send(msg);
}


submitButton.addEventListener('click', () => {
  const newTemp = parseInt(adjustTemp.value);
  const currTemp = document.getElementById('set-temp').innerHTML
  if (newTemp >= minTemp && newTemp <= maxTemp && newTemp != currTemp) {
    const msg = JSON.stringify({set_temp: newTemp});
    console.log("Sending:", msg)
    ws.send(msg);
    requestDefaults(); // Sets all client entry fields to new value.
  } else {
    adjustTemp.value = document.getElementById('set-temp').innerHTML;
    console.log("Adjust Temp value out of range:", newTemp);
  }
});


const getDateTime = () => {
  const date = new Date();
  const nowTime = date.toLocaleString("en-US", { timeStyle: "medium" });
  const nowDate = date.toLocaleString("en-US", { dateStyle: "medium" });
  return `${nowTime} &nbsp &nbsp ${nowDate}`;
}


// // probably just combine with actual function.
// submitButton.addEventListener('click', sendSetTemp);


plus.addEventListener("click", () => {
  const curTemp = parseInt(adjustTemp.value);
  const newTemp = curTemp + 1;
  if (newTemp <= maxTemp) {
    adjustTemp.value = newTemp;
  }
});

minus.addEventListener("click", () => {
  const curTemp = parseInt(adjustTemp.value);
  const newTemp = curTemp - 1;
  if (newTemp >= minTemp) {
    adjustTemp.value = newTemp;
  }
});


// Validate New Temperature
adjustTemp.addEventListener('blur', () => {
  const newTemp = parseInt(adjustTemp.value);
  if (newTemp < minTemp || newTemp > maxTemp) {
    adjustTemp.value = document.getElementById('set-temp').innerHTML;
  } else {
    console.log("Adjust Temp value out of range:", newTemp);
  }
})


// num.addEventListener("click", () => {
//   const elem = document.getElementById("num");
//   console.log("You clicked number");
//   this.type = 'number';
// });
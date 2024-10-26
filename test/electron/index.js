const { app, BrowserWindow, systemPreferences  } = require('electron');

app.disableHardwareAcceleration();
app.commandLine.appendSwitch("no-sandbox");
app.commandLine.appendSwitch("disable-gpu");
app.commandLine.appendSwitch("disable-gpu-compositing");
app.commandLine.appendSwitch("disable-software-rasterizer");

const createWindow = () => {
  const win = new BrowserWindow({
    width: 1024,
    height: 600,
    webPreferences : {
      nodeIntegration: true,
      contextIsolation: false,
      webSecurity: false,
    }

  })

  win.loadFile('main.html')
}

app.whenReady().then(() => {
  createWindow()
})

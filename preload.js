const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('shellAPI', {
  info: () => ipcRenderer.invoke('shell:info'),
  listApps: () => ipcRenderer.invoke('shell:list-apps'),
  launch: (path) => ipcRenderer.invoke('shell:launch', { path }),
  power: (action) => ipcRenderer.invoke('shell:power', action),
  devtools: () => ipcRenderer.invoke('shell:devtools'),
});

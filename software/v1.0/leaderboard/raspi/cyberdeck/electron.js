// Run with: electron electron.js

const { app, BrowserWindow } = require('electron');

var nodeConsole = require('console');
var myConsole = new nodeConsole.Console(process.stdout, process.stderr);
myConsole.log('Hack The Planet!');

function createWindow() {
    // Resolution is set for Pimoroni Hyperpixel 4.0 Square display
    const mainWindow = new BrowserWindow({
        width: 720,
        height: 720,
        frame: false, // Remove title bar
        fullscreen: false // Run in full-screen mode
    });

    mainWindow.loadFile('public/index.html');

    // mainWindow.openDevTools();

    mainWindow.webContents.on('dom-ready', (event)=> {
        let css = '* { cursor: none !important; }';
        mainWindow.webContents.insertCSS(css);
    });

    mainWindow.on('closed', function () {
        app.quit();
    });
}

app.whenReady().then(createWindow);

app.on('window-all-closed', function () {
    if (process.platform !== 'darwin') app.quit();
});

app.on('activate', function () {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
});



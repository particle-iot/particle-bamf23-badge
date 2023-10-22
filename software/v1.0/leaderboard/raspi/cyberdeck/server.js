// Run with: node server.js

const express = require('express');
const app = express();

const http = require('http');
const server = http.createServer(app);
const io = require('socket.io')(server);
const axios = require('axios');


const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const serialPort = new SerialPort({ path: '/dev/ttyACM0', baudRate: 115200 }, function (err) {
    if (err) {
        return console.log('Error: ', err.message)
    }
})
const parser = serialPort.pipe(new ReadlineParser({ delimiter: '\r\n' }));

let nodeServer = 0;
let resetScreenTimer;
const hostname = 'http://192.168.1.150:3001'; // high score server has a static IP on the local network
let isIdle = true; // Prevent false re-entry from other badges while one user is in progress at the terminal

app.use(express.static('public'));

server.listen(3000, () => {
    console.log('Server is running on port 3000');
});


function isValidJSON(data) {
    if (typeof data !== "string") {
        return false;
    }
    try {
        JSON.parse(data);
        return true;
    } catch (error) {
        console.log(error);
        return false;
    }
}

function resetScreen() {
    if (nodeServer) {
        nodeServer.emit('text1', '');
        nodeServer.emit('text2', '');
        nodeServer.emit('text3', '<<< THROW BALLOON!');
        nodeServer.emit('text4', '');
        nodeServer.emit('text5', '');
        console.log('resetScreen');
        isIdle = true;
    }
}

io.on('connection', (socket) => {
    console.log('A user connected');

    nodeServer = socket;

    nodeServer.on('name-entered', async (data) => {
        console.log("nameEntered", JSON.parse(data));
        if (!isValidJSON(data)) {
            return;
        }
        let jsonData = JSON.parse(data);

        if (jsonData.cyberdeck_game_name !== undefined || jsonData.cyberdeck_game_name != "") {
            if (resetScreenTimer) {
                clearTimeout(resetScreenTimer);
                console.log('clearTimeout 1');
            }

            // CHECK IF NAME IS UNIQUE
            let nameUnique = false;
            const mydata1 = {
                id: jsonData.cyberdeck_device_id,
                nick: jsonData.cyberdeck_game_name,
            };
            await axios.post(hostname+'/user/unique', mydata1)
                .then((res) => {
                    console.log('unique response:',JSON.parse(res.data));
                    console.log('unique status:', res.status);
                    if (res.status == 200 && isValidJSON(res.data)) {
                        let result = JSON.parse(res.data);
                        if (result.res == 200) {
                            console.log('User name is not unique!');
                        } else if (result.res == 404) {
                            console.log('User name is unique!');
                            nameUnique = true;
                        }
                    }
                }).catch((err) => {
                    console.error(err);
                });

            if (!nameUnique) {
                nodeServer.emit('text4', 'NAME TAKEN, TRY AGAIN');
                if (resetScreenTimer) {
                    clearTimeout(resetScreenTimer);
                    console.log('clearTimeout 2');
                }
                resetScreenTimer = setTimeout(resetScreen, 20000);
                console.log('setTimeout 2');
                return;
            }

            console.log('Name entered:', jsonData.cyberdeck_game_name);

            nodeServer.emit('text1', 'WELCOME,');
            nodeServer.emit('text2', jsonData.cyberdeck_game_name);
            nodeServer.emit('text3', '');
            nodeServer.emit('text4', 'YOUR SCORE IS:');
            nodeServer.emit('text5', jsonData.cyberdeck_game_score);

            const mydata2 = {
                id: jsonData.cyberdeck_device_id,
                nick: jsonData.cyberdeck_game_name,
                game: "Splash",
                score: jsonData.cyberdeck_game_score,
                crc: jsonData.cyberdeck_game_crc
            };
            await axios.post(hostname+'/user/put', mydata2)
                .then((res) => {
                    if (res.status == 200 && isValidJSON(res.data)) {
                        if (res.data.res == 200) {
                            console.log('High score record created/updated!');
                        } else {
                            console.log('High score record not created/updated!');
                        }
                    }
                }).catch((err) => {
                    console.error(err);
                });

            resetScreenTimer = setTimeout(resetScreen, 10000);
            // console.log('setTimeout 4');
        }
    });
});

parser.on('data', async (data) => {

    console.log(data);

    if (!isValidJSON(data) ||  !isIdle) {
        return;
    }
    isIdle = false;

    let jsonData = JSON.parse(data);
    // jsonData.rename_player = true;
    let updateName = false;
    let jsonNameLookup;

    // {"cyberdeck_game_name":"Splash","cyberdeck_game_score":"11","cyberdeck_game_crc":"12345678","cyberdeck_device_id":"4A02C58C","rename_user:false"}
    console.log(jsonData.cyberdeck_game_name);
    console.log(jsonData.cyberdeck_game_score);
    console.log(jsonData.cyberdeck_game_crc);
    console.log(jsonData.cyberdeck_device_id);
    console.log(jsonData.rename_player);
    if (nodeServer) {
        nodeServer.emit('json-data', JSON.stringify(jsonData));

        nodeServer.on('esc-key', async (data) => {
            // console.log("escape key hit!");
            resetScreen();
        });
    }

    if (jsonData.rename_player == true) {
        const mydata4 = {
            id: jsonData.cyberdeck_device_id,
        };
        await axios.post(hostname+'/user/delete', mydata4)
            .then((res) => {
                console.log('delete response:',JSON.parse(res.data));
                console.log('delete status:', res.status);
                if (res.status == 200 && isValidJSON(res.data)) {
                    let result = JSON.parse(res.data);
                    if (result.res == 200) {
                        console.log('User deleted!');
                    } else if (result.res == 500) {
                        console.log('User not deleted!');
                    }
                }
            }).catch((err) => {
                console.error(err);
            });
    }

    const mydata = {
        id: jsonData.cyberdeck_device_id,
        score: jsonData.cyberdeck_game_score
    };
    await axios.post(hostname+'/user/get', mydata)
        .then((res) => {
            if (res.status == 200 && isValidJSON(res.data)) {
                console.log(res.data);
                jsonNameLookup = JSON.parse(res.data);
                console.log('Body:',jsonNameLookup);
                if (jsonNameLookup.nick == 'none') {
                    updateName = true;
                    console.log('No name found!');
                } else {
                    console.log('Name found: ' + jsonNameLookup.nick);
                }
            }
        }).catch((err) => {
            console.error(err);
            return;
        });

    // await new Promise(resolve => setTimeout(resetScreen, 10000));
    if (resetScreenTimer) {
        clearTimeout(resetScreenTimer);
        // console.log('clearTimeout 3');
    }
    resetScreenTimer = setTimeout(resetScreen, 15000);
    // console.log('setTimeout 3');

    if (nodeServer) {
        if (updateName) {
            nodeServer.emit('text1', 'ENTER YOUR NICKNAME:');
            nodeServer.emit('text2', '');
            nodeServer.emit('text3', '');
            nodeServer.emit('text4', '');
            nodeServer.emit('text5', '');
            // we'll wait 15s for a response
        } else {
            nodeServer.emit('text1', 'WELCOME BACK,');
            nodeServer.emit('text2', jsonNameLookup.nick);
            nodeServer.emit('text3', '');
            nodeServer.emit('text4', 'YOUR SCORE IS:');
            nodeServer.emit('text5', jsonData.cyberdeck_game_score);

            const mydata3 = {
                id: jsonData.cyberdeck_device_id,
                nick: jsonData.cyberdeck_game_name,
                game: "Splash",
                score: jsonData.cyberdeck_game_score,
                crc: jsonData.cyberdeck_game_crc
            };
            await axios.post(hostname+'/user/put', mydata3)
                .then((res) => {
                    if (res.status == 200 && isValidJSON(res.data)) {
                        if (res.data.res == 200) {
                            console.log('High score record created/updated!');
                        } else {
                            console.log('High score record not created/updated!');
                        }
                    }
                }).catch((err) => {
                    console.error(err);
                    return;
                });
        }
    }

});

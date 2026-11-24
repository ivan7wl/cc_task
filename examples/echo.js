#!/usr/bin/env node
"use strict";

// JavaScript reference for echo.c: one async function per connection.

const net = require("node:net");
const { once } = require("node:events");
const { performance } = require("node:perf_hooks");

function makeWrite(socket, data) {
    return new Promise((resolve, reject) => {
        socket.write(data, error => error ? reject(error) : resolve());
    });
}

async function makeRead(iterator) {
    const { value, done } = await iterator.next();
    return done ? Buffer.alloc(0) : value;
}

// TCP reads are not message aligned. Keep unused bytes for the next round.
function makeReader(socket) {
    const iterator = socket.iterator({ destroyOnReturn: false });
    let saved = Buffer.alloc(0);

    return async function readExactly(size) {
        while (saved.length < size) {
            const chunk = await makeRead(iterator);
            if (chunk.length === 0) {
                throw new Error("unexpected EOF");
            }
            saved = saved.length === 0 ? chunk : Buffer.concat([saved, chunk]);
        }
        const result = saved.subarray(0, size);
        saved = saved.subarray(size);
        return result;
    };
}

function fillBuf(buffer, round) {
    for (let i = 0; i < buffer.length; ++i) {
        buffer[i] = (i + round) & 0xff;
    }
}

function checkBuf(buffer, round) {
    for (let i = 0; i < buffer.length; ++i) {
        if (buffer[i] !== ((i + round) & 0xff)) {
            return false;
        }
    }
    return true;
}

async function serveConnection(socket) {
    socket.setNoDelay(true);
    const iterator = socket.iterator({ destroyOnReturn: false });
    try {
        for (;;) {
            const chunk = await makeRead(iterator);
            if (chunk.length === 0) {
                break;
            }
            await makeWrite(socket, chunk);
        }
    } catch (error) {
        if (error.code !== "ECONNRESET" && error.code !== "EPIPE") {
            console.error("connection:", error.message);
        }
    } finally {
        socket.destroy();
    }
}

async function makeListener(port) {
    const server = net.createServer(socket => {
        void serveConnection(socket);
    });
    const listening = once(server, "listening");
    server.listen(port, "0.0.0.0");
    await listening;
    return server;
}

async function runServer(port) {
    await makeListener(port);
    console.log(`listening 0.0.0.0:${port}`);
}

async function makeConnect(host, port) {
    const socket = net.createConnection({ host, port });
    socket.setNoDelay(true);
    await once(socket, "connect");
    return socket;
}

async function runConnection(host, port, messageSize, rounds) {
    const socket = await makeConnect(host, port);
    const readExactly = makeReader(socket);
    const payload = Buffer.allocUnsafe(messageSize);

    for (let round = 0; round < rounds; ++round) {
        fillBuf(payload, round);
        await makeWrite(socket, payload);
        const echoed = await readExactly(messageSize);
        if (!checkBuf(echoed, round)) {
            throw new Error("echo data mismatch");
        }
    }

    const closed = once(socket, "close");
    socket.end();
    await closed;
    return messageSize * rounds;
}

async function runClient(host, port, connections, messageSize, rounds) {
    const started = performance.now();
    // Every function runs to its first await immediately: this is Promise.all in C.
    const jobs = Array.from({ length: connections }, () =>
        runConnection(host, port, messageSize, rounds));
    const counts = await Promise.all(jobs);
    const seconds = (performance.now() - started) / 1000;
    const bytes = counts.reduce((sum, count) => sum + count, 0);
    const mib = bytes / seconds / (1024 * 1024);
    console.log(`connections=${connections} failed=0 echoed=${bytes} bytes ` +
                `time=${seconds.toFixed(3)}s rate=${mib.toFixed(1)} MiB/s`);
}

function parseNumber(text, min, max) {
    const value = Number(text);
    if (!Number.isInteger(value) || value < min || value > max) {
        throw new Error(`invalid number: ${text}`);
    }
    return value;
}

function usage() {
    console.error(`usage:
  node examples/echo.js server <port>
  node examples/echo.js client <host> <port> <connections> <messageSize> <rounds>`);
}

async function main() {
    const args = process.argv.slice(2);
    if (args.length === 2 && args[0] === "server") {
        await runServer(parseNumber(args[1], 1, 65535));
        return;
    }
    if (args.length === 6 && args[0] === "client") {
        await runClient(args[1], parseNumber(args[2], 1, 65535),
                        parseNumber(args[3], 1, 1000000),
                        parseNumber(args[4], 1, 16384),
                        parseNumber(args[5], 1, 1000000));
        return;
    }
    usage();
    process.exitCode = 2;
}

main().catch(error => {
    console.error(`${error.code || "error"}: ${error.message}`);
    process.exitCode = 1;
});

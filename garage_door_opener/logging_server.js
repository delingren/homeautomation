const {createServer} = require('http');
const {appendFile, existsSync, renameSync} = require('fs');
const {exec} = require('child_process');

const hostname = 'localhost';
const port = 3000;
const basePath = '/var/lib/httplog/';
const lineLimit = 100;

const server = createServer((request, response) => {
  var error = null;
  var statusCode = 200;

  if (request.method == 'POST') {
    request.on('data', function(data) {
      const entry = JSON.parse(data);
      const ip = request.socket.remoteAddress;

      const time = entry.time;
      const channel = entry.channel;
      const message = entry.message;

      if (!channel || !isValidChannelName(channel)) {
        error = 'Invalid or empty channel name.';
        statusCode = 400;
        return;
      }

      if (!message) {
        error = 'Empty message.';
        statusCode = 400;
        return;
      }

      if (!time || !isValidTime(time)) {
        error = 'Invalid ISO 8601 or empty time.'
        statusCode = 400;
        return;
      }

      const fileName = basePath + ip + '.' + channel + '.log';
      appendFile(fileName, time + ',' + message + '\n', 'utf8', err => {
        if (err) {
          console.log(
              'Failed to append entry to ' + fileName + '. Error: ' + err);
        } else {
          exec('/usr/bin/wc -l < ' + fileName, function(error, stdout, stderr) {
            const lines = stdout;
            if (lines >= lineLimit) {
              console.log('Line limit reached. Rolling over.');
              rollOver(fileName);
            }
          });
          console.log('Successfully appended entry to ' + fileName);
        }
      });
    });

    request.on('end', function() {
      response.statusCode = statusCode;
      response.setHeader('Content-Type', 'text/plain');
      if (error) {
        response.end(error + '\n');
      } else {
        response.end('Message queued.\n');
      }
    });
  } else {
    response.statusCode = 200;
    response.setHeader('Content-Type', 'text/plain');
    response.end('Not implemented yet.\n');
  }
});

server.listen(port, hostname, () => {
  console.log(`Server running at http://${hostname}:${port}/`);
});

function isValidChannelName(str) {
  return str.length > 0 && str.match(/^[\w-\.]+$/);
};

function isValidTime(str) {
  // Validate against ISO 8601.
  const parsed = new Date(Date.parse(str));
  try {
    parsed.toISOString();
    return true;
  } catch (e) {
    return false;
  }
}

function rollOver(fileName) {
  const baseName = fileName.substring(0, fileName.length - 4);
  console.log('basename: ' + baseName + ' ...');
  var n = 0;
  var newFileName;
  do {
    newFileName = baseName + '.' + n + '.log';
    if (!existsSync(newFileName)) {
      break;
    }
    n = n + 1;
  } while (true);
  renameSync(fileName, newFileName);
}
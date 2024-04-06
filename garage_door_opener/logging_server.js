const {createServer} = require('node:http');
const {appendFile} = require('node:fs');

const hostname = 'localhost';
const port = 3000;

const server = createServer((request, response) => {
  var error = null;
  var statusCode = 200;

  if (request.method == 'POST') {
    request.on('data', function(data) {
      const entry = JSON.parse(data);
      const remoteIp = request.socket.remoteAddress;
      const channelName = entry.channel;


      console.log(entry.channel);
      console.log(entry.message);
      console.log(request.socket.remoteAddress);


      if (!isValidChannelName(channelName)) {
        error = 'Invalid channel name.';
        statusCode = 400;
        return;
      }

      const fileName = remoteIp + '.' + channelName + '.log';

      appendFile(fileName, entry.message + '\n', err => {
        if (err) {
          console.error(err);
          error = err;
          statusCode = 500;
        } else {
          console.log('Log appened to test.log');
        }
      });
    });

    request.on('end', function() {
      response.statusCode = statusCode;
      response.setHeader('Content-Type', 'text/plain');
      if (error) {
        response.end(error + '\n');
      } else {
        response.end('Message logged.\n');
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
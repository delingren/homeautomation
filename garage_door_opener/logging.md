Client:
Post over https with the following info in json format
1. channel name (alphanumerical, _, -, .)
1. time stamp (ISO 8601)
1. message (escaped for json)

Escape:

Backspace to be replaced with \b.
Form feed to be replaced with \f.
Newline to be replaced with \n.
Carriage return to be replaced with \r.
Tab to be replaced with \t.
Double quote to be replaced with \"
Backslash to be replaced with \\

Server:

Save in $ip.$channel.log with:
1. time stamp
1. message. it does not check the validity of the message.

GET /$ip/$channel?entry=
POST:
curl -X POST -H "Content-Type: application/json" -d '{"channel":"esp32","message":"hello world"}' http://localhost:3000/

Startup:
use pm2 for node.js
https://pm2.keymetrics.io/docs/usage/quick-start/

create a user for this purpose and create startup script
https://pm2.keymetrics.io/docs/usage/startup/
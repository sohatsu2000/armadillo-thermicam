const http = require('http');
const url = require('url');
const fs = require('fs');

const PORT = 12130;

function file_append(fn,data){
  fs.appendFile(fn, data + '\n', (err) => {
    if (err) {
      console.error('Failed to write to file:', err);
    } else {
      console.log('Event data appended to events_log.json');
    }
  });
}

function rand(min,max) {
    return  min + Math.floor(Math.random() * (max-min))
}

function newarr(n,callback){
    return Array.from({ length: n }, (_,i)=>callback(i+1)).filter(Boolean);
}

function rand_class(i) {
    const n = rand(0,10)
    const s = rand(60,120)
    if (n==0) return null;
    return {
              "classNr": `${i}`,
              "gapTime": null,
              "gapTimeSq": null,
              "numVeh": `${n}`,
              "speed": `${s}`,
              "speedSq": null
            };
}

function rand_zone(i) {
    cls = newarr(3,rand_class);
    const o = rand(1,10) * 10
    if (cls.length==0) return null;
    return {
          "class": cls,
          "confidence": null,
          "density": null,
          "headWay": null,
          "headWaySq": null,
          "length": null,
          "occupancy": `${o}`,
          "zoneId": `${i}`
        };
}

const server = http.createServer((req, res) => {
  const parsedUrl = url.parse(req.url, true);
  const { begintime, endtime } = parsedUrl.query;
  
  
  console.log('Begin time:', begintime);
  console.log('End time:', endtime);

  // Optionally, parse to Date objects
  const beginDate = new Date(begintime);
  const endDate = new Date(endtime);

  // Check for the route and method
  if (parsedUrl.pathname === '/api/data') {
  zones = newarr(2,rand_zone);
    const data = {
  "data": [
    {
      "dataNumber": null,
      "intervalTime": null,
      "messageType": "Data",
      "time": "2015-06-04T11:57:00.040+02:00", /* time compris entre les deux parametres de l'url */
      "type": "IntegratedData",
      "zone": zones
    }
  ],
  "nextDataUrl": "/api/data?begintime=2015-06-04T12%3A18%3A00.024%2B02%3A00" /*pas utile mais genere le quand meme on verra plus tard*/
};

    const json = JSON.stringify(data)
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(json);
    file_append("data.log",(new Date).toISOString()+":"+json);
  } else
  if (parsedUrl.pathname === '/api/events/open') {
    const events = [
{
"eventNumber":"2",
"messageType":"Event",
"state":"Begin",
"time":(new Date).toISOString(),
"type":"Presence",
"zoneId":"2"
}
];
    
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify([]));
  }
   else {
    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not Found' }));
  }
});

server.on('clientError', (err, socket) => {
  console.warn('Malformed request received:', err.message);
  socket.end('bado requesto \r\n\r\n');
});

server.listen(PORT, () => {
  console.log(`Server running at http://localhost:${PORT}`);
});

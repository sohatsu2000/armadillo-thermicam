const http = require('http');
const url = require('url');
const fs = require('fs');
const seed = require('seedrandom');
let generator = seed(0);

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

function log_line(data){
  const D = data.data[0]
  const time = D.time
  const z1=D.zone.filter(x=>x.zoneId==1).pop()
  const z2=D.zone.filter(x=>x.zoneId==2).pop()
  const c11 = z1?.['class'].filter(x=>x.classNr==1).pop()
  const c12 = z1?.['class'].filter(x=>x.classNr==2).pop()
  const c21 = z2?.['class'].filter(x=>x.classNr==1).pop()
  const c22 = z2?.['class'].filter(x=>x.classNr==2).pop()
  const up={
    ns:c11?.numVeh,
    ss:c11?.speed,
    nl:c12?.numVeh,
    sl:c12?.speed,
    oc:z1?.occupancy,
  }
  const dw={
    ns:c21?.numVeh,
    ss:c21?.speed,
    nl:c22?.numVeh,
    sl:c22?.speed,
    oc:z2?.occupancy,
  }
  return `${time}`+
  `,${up.ns},${up.ss},${up.nl},${up.sl},${up.oc}`+
  `,${dw.ns},${dw.ss},${dw.nl},${dw.sl},${dw.oc}`+
  '';
}

function rand(min,max) {
    return  min + Math.floor(generator() * (max-min))
}

function newarr(n,callback){
    return Array.from({ length: n }, (_,i)=>callback(i+1)).filter(Boolean);
}

function rand_class(i) {
    const n = rand(0,10)
    const s = rand(60,120)
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
  console.log('Actual date:', beginDate);
  console.log('Sneed:', beginDate.getTime());
  generator=seed(beginDate?.getTime() ?? 0);

  // Check for the route and method
  if (parsedUrl.pathname === '/api/data') {
  zones = newarr(2,rand_zone);
    const data = {
  "data": [
    {
      "dataNumber": null,
      "intervalTime": "60",
      "messageType": "Data",
      "time": (new Date).toISOString(), /* time compris entre les deux parametres de l'url */
      "type": "IntegratedData",
      "zone": zones
    }
  ],
  //"nextDataUrl": "/api/data?begintime=2015-06-04T12%3A18%3A00.024%2B02%3A00" /*pas utile mais genere le quand meme on verra plus tard*/
};

    const json = JSON.stringify(data)
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(json);
    file_append("data.log",log_line(data));
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

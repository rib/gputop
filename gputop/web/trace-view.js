console.log("Hello Console");

ww = new Worker("gputop-web-worker.js")
ww.onmessage = function(event) {
    console.log("Worker message from gputop-web-worker.js");
}

$.plot($("#placeholder"), [ [[0, 0], [1, 1]] ], { yaxis: { max: 1 } });



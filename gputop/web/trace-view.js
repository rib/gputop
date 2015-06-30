console.log("Hello Console");

ww = new Worker("gputop-web-worker.js")
ww.onmessage = function(e) {
    console.log(e.data);
    var json = JSON.parse(e.data);

    var queries = json.queries;

    for (var query of queries) {
	var h3 = document.createElement("h3");
	h3.innerHTML = query.name;
	$("#sidebar").append(h3);
	var div = document.createElement("div");
	$("#sidebar").append(div);

	for (var counter of query.counters) {
	    var counter_div = document.createElement("div");
	    counter_div.innerHTML = counter.name;
	    $(counter_div).draggable({ containment: "window", scroll: false, revert: true, helper: "clone" });

	    div.appendChild(counter_div);
	}
    }
    $("#sidebar").accordion();
}

//var div = document.createElement("div");
//document.body.append(div);


$.plot($("#placeholder"), [ [[0, 0], [1, 1]] ], { yaxis: { max: 1 } });



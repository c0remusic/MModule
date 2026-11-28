// Lockstep Push: the Push companion's [js] (ticket 22 of .scratch/elektron-drums-v2), in a Max for Live MIDI effect in
// front of Lockstep on its track. It says hello to Lockstep by notes 124-127 that give the UDP port it listens on,
// learns Lockstep's port from the answer, then reads the pattern playing and the Track shown (/lockstep/view, the step
// playing in /lockstep/play) and writes trigs (/lockstep/trig), OSC on 127.0.0.1 through udpsend and udpreceive; the
// protocol is edDrumsPushLink.h's. Armed, it takes Push's pad matrix (Button_Matrix) and shows the Track's first 16
// steps on its top two rows; a pad on an empty step sets a trig at the pad's velocity, a short press on a trig removes
// it. It plays no note: Lockstep's sequencer does. One companion arms Push at a time, the last one armed.
//
// Inlets: "start" (live.thisdevice), "arm 0|1" (the Push key), OSC from udpreceive, "otherarmed id" (the global send).
// Outlets: 0 udpsend, 1 udpreceive, 2 MIDI bytes, 3 the global send, 4 the Push key, 5 the status line.
autowatch = 0;
inlets = 1;
outlets = 6;

var HelloMark = 76, FirstHelloNote = 124;
var ShortPress = 400;			// ms: a trig pressed and released sooner is removed
var PadSteps = 16;
// Push's default palette (Push 2's MIDI interface document): its fixed greys and colours
var Black = 0, White = 122, DarkGrey = 124, Blue = 125, Green = 126;

var me = Math.floor(Math.random() * 1000000000);
var started = false;
var myPort = 0, lockstepPort = 0, unanswered = 0;
var helloTask = null;
var view = null;				// [pattern, track, length, v1..v16]
var playStep = -1;
var armed = false;
var surface = null, matrix = null, matrixId = 0, matrixObserver = null, layoutObserver = null;
var shown = [];				// the colour each pad shows, by row * 8 + column
var presses = {};			// a step held: {at, added}
var repaintTask = null, repaints = 0;

function noop() {}

function status(text)
{
	outlet(5, "set", text);
}

function start()
{
	if(started)
		return;
	started = true;
	newPort();
	helloTask = new Task(hello, this);
	helloTask.interval = 1000;
	helloTask.repeat();
}

// A port of our own for udpreceive; another one when Lockstep has not answered for a while (the port was taken)
function newPort()
{
	myPort = 50000 + Math.floor(Math.random() * 10000);
	unanswered = 0;
	outlet(1, "port", myPort);
}

// The hello, every second until Lockstep answers, then every two: a Lockstep reloaded answers from another port
function hello()
{
	if(++unanswered > 5)
	{
		lockstepPort = 0;
		status("waiting for Lockstep");
		newPort();
	}
	helloTask.interval = lockstepPort ? 2000 : 1000;
	var v = [HelloMark, 1 + (myPort >> 12 & 63), 1 + (myPort >> 6 & 63), 1 + (myPort & 63)];
	for(var i = 0; i < 4; ++i)
	{
		outlet(2, [0x90, FirstHelloNote + i, v[i]]);
		outlet(2, [0x80, FirstHelloNote + i, 0]);
	}
}

// OSC from Lockstep, its address the message's name
function anything()
{
	var a = arrayfromargs(arguments);
	if(messagename === "/lockstep/hello" && a.length === 1)
	{
		unanswered = 0;
		if(a[0] !== lockstepPort)
		{
			lockstepPort = a[0];
			outlet(0, "port", lockstepPort);
		}
		status("Lockstep on port " + lockstepPort);
	}
	else if(messagename === "/lockstep/view" && a.length === 3 + PadSteps)
	{
		view = a;
		repaint(false);
	}
	else if(messagename === "/lockstep/play" && a.length === 1)
	{
		playStep = a[0];
		repaint(false);
	}
}

function arm(on)
{
	on = Number(on) !== 0;
	if(on === armed)
		return;
	armed = on;
	if(armed)
	{
		outlet(3, "otherarmed", me);
		grab();
	}
	else
		release();
}

// Another companion armed Push: this one gives it up, its key off
function otherarmed(id)
{
	if(Number(id) === me || !armed)
		return;
	armed = false;
	release();
	outlet(4, "set", 0);
}

function idOf(result)
{
	// LiveAPI.call returns ["id", n] for an object
	if(result instanceof Array)
		return Number(result[result.length - 1]);
	return Number(result);
}

// Push's surface: of those listing Button_Matrix, the second (two do, and the first does nothing: md-drums-seq issue 33)
function findSurface()
{
	var found = [];
	for(var i = 0; i < 20; ++i)
	{
		var cs = new LiveAPI(noop, "control_surfaces " + i);
		if(!cs || Number(cs.id) === 0)
			continue;
		var names = cs.call("get_control_names");
		if(names && names.toString().indexOf("Button_Matrix") >= 0)
			found.push(i);
	}
	return found.length ? found[found.length > 1 ? 1 : 0] : -1;
}

function grab()
{
	release();
	var index = findSurface();
	// No Push, or not up yet: the key goes off, which arming again retries
	if(index < 0)
	{
		armed = false;
		outlet(4, "set", 0);
		status("no Push");
		return;
	}
	surface = new LiveAPI(noop, "control_surfaces " + index);
	matrixId = idOf(surface.call("get_control", "Button_Matrix"));
	if(!matrixId)
	{
		surface = null;
		armed = false;
		outlet(4, "set", 0);
		status("no pad matrix");
		return;
	}
	surface.call("release_control", "id", matrixId);
	surface.call("grab_control", "id", matrixId);
	matrix = new LiveAPI(noop, "id " + matrixId);
	matrixObserver = new LiveAPI(pad, "id " + matrixId);
	matrixObserver.property = "value";
	// Live may paint the pads again when Push's layout changes: paint ours over it
	layoutObserver = new LiveAPI(function(args)
	{
		if(args && args[0] === "pad_layout")
			repaintSoon();
	}, "control_surfaces " + index);
	layoutObserver.property = "pad_layout";
	status("Push armed" + (lockstepPort ? "" : ", waiting for Lockstep"));
	repaintSoon();
}

function release()
{
	if(repaintTask)
		repaintTask.cancel();
	if(matrixObserver)
		matrixObserver.property = "";
	if(layoutObserver)
		layoutObserver.property = "";
	matrixObserver = layoutObserver = null;
	if(surface && matrixId)
		surface.call("release_control", "id", matrixId);
	surface = matrix = null;
	matrixId = 0;
	shown = [];
	presses = {};
	if(started)
		status(lockstepPort ? "Lockstep on port " + lockstepPort : "waiting for Lockstep");
}

function notifydeleted()
{
	release();
	if(helloTask)
		helloTask.cancel();
}

// A colour sent just after the grab may be lost: the whole matrix six times, 150 ms apart
function repaintSoon()
{
	if(repaintTask)
		repaintTask.cancel();
	repaints = 0;
	repaintTask = new Task(function()
	{
		repaint(true);
		if(++repaints >= 6)
			arguments.callee.task.cancel();
	}, this);
	repaintTask.interval = 150;
	repaintTask.repeat();
}

function stepColour(s)
{
	if(!view || s >= view[2])
		return Black;
	if(s === playStep)
		return White;
	var v = view[3 + s];
	if(!v)
		return DarkGrey;
	return v >= 64 ? Green : Blue;
}

function repaint(force)
{
	if(!matrix)
		return;
	for(var row = 0; row < 8; ++row)
		for(var col = 0; col < 8; ++col)
		{
			var c = row < 2 ? stepColour(row * 8 + col) : Black;
			var k = row * 8 + col;
			if(!force && shown[k] === c)
				continue;
			shown[k] = c;
			matrix.call("send_value", col, row, c);
		}
}

// The matrix's value: velocity, column, row (row 0 at the top); velocity 0 when the pad comes up
function pad(args)
{
	if(!args || args[0] !== "value" || args.length < 4 || !view || !lockstepPort)
		return;
	var velocity = Number(args[1]), col = Number(args[2]), row = Number(args[3]);
	if(row > 1)
		return;
	var s = row * 8 + col;
	if(s >= view[2])
		return;
	var pattern = view[0], track = view[1];
	if(velocity > 0)
	{
		var empty = !view[3 + s];
		presses[s] = {at: Date.now(), added: empty, pattern: pattern, track: track};
		if(empty)
			setTrig(pattern, track, s, velocity);
		return;
	}
	var p = presses[s];
	delete presses[s];
	if(p && !p.added && Date.now() - p.at < ShortPress)
		setTrig(p.pattern, p.track, s, 0);
}

// Shown at once, then as Lockstep tells it
function setTrig(pattern, track, s, velocity)
{
	outlet(0, "/lockstep/trig", pattern, track, s, velocity);
	if(view && view[0] === pattern && view[1] === track)
	{
		view[3 + s] = velocity;
		repaint(false);
	}
}

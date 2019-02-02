#define DEBUG(X, Y ...) do { if (debug) werror(X, Y); } while (0)
#define DEFAULT_PORT 3333

import .Protocol;
array(mixed) global_bt = backtrace();
bool debug = true;
bool im_a_server = true;
Stdio.Buffer obuf = Stdio.Buffer();

int frameId;

void write_event(ProtocolMessage msg, bool|void violently) {
    string msg_str = encode_message(msg);

    DEBUG("\nSending event: \n%s\n", msg_str);

    if (im_a_server) if (violently) breakpoint_client->write(msg_str); else obuf->add(msg_str);
    else obuf->write(msg_str);
}

void write_response(ProtocolMessage msg) {
    string msg_str = encode_message(msg);

    DEBUG("\nSending response: \n%s\n", msg_str);

    if (im_a_server) breakpoint_client->write(msg_str);
    else obuf->write(msg_str);
}

void handle_initialize_request(mixed msg) {
    InitializeRequest req = InitializeRequest(msg);
    InitializeResponse res = InitializeResponse();

    res->body->supports_configuration_done_request = 1;
    res->request_seq = req->seq;
    res->success = 1;

    write_response(res);

    InitializedEvent evt = InitializedEvent();

    write_event(evt);
}

void handle_attach_request(mixed msg) {
    AttachRequest req = AttachRequest(msg);
    AttachResponse res = AttachResponse();

    res->request_seq = req->seq;
    res->success = 1;

    write_response(res);
}

void handle_continue_request(mixed msg) {
    object key;
    handle_request_generic(msg);
    DEBUG("Resuming.\n");
	set_mode(CONTINUE);

    if(breakpoint_cond) {
        key = bp_lock->lock();
        breakpoint_cond->signal();
        key = 0;
        breakpoint_cond = 0;
    }
    if(wait_cond) {
        key = wait_lock->lock();
        wait_cond->signal();
        key = 0;
        wait_cond = 0;
    }
}


void handle_launch_request(mixed msg) {
    LaunchRequest req = LaunchRequest(msg);
    LaunchResponse res = LaunchResponse();

    res->request_seq = req->seq;
    res->success = 1;

    write_response(res);
}

void handle_evaluate_request(mixed msg) {
    EvaluateRequest req = EvaluateRequest(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->body = ([
        "result" : "EvaluateResponse echo: " + req?->arguments?->expression,
        "variablesReference" : 0
    ]);

    write_response(res);
}

void handle_threads_request(mixed msg) {
    Request req = Request(msg);
    ThreadsResponse res = ThreadsResponse();

    DAPThread dt = DAPThread();
    dt->id = 1;
    dt->name = "dummy";

    res->body = (["threads" : ({ dt })]);

    res->request_seq = req->seq;
    res->success = 1;

    write_response(res);
}

void handle_configuration_done_request(mixed msg) {
    Request req = Request(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->command = req->command;


    write_response(res);
    StoppedEvent evt = StoppedEvent();
    evt->body = (["reason" : "entry", "threadId" : 1]);
    write_event(evt);
}

void handle_stack_trace_request(mixed msg) {
    Request req = Request(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->command = req->command;

    array(mapping(string:mixed)) frames = ({});
    foreach(global_bt, mixed frame) {
        frames += ({
            ([
                "id" : frameId++,
                "line": frame[1],
                "column": 0,
                "name": sprintf("%O", frame[2]),
                "source": ([
                    "path": frame[0],
                    "name": (frame[0] / "/")[-1]
                    ])
            ])
        });
    }
    frames = reverse(frames);
    res->body = ([
        "stackFrames" : frames
    ]);

    write_response(res);
}

void handle_scopes_request(mixed msg) {
    Request req = Request(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->command = req->command;
    res->body = ([
        "scopes" : ({
            ([
                "name" : "scope name",
                "variableReference" : "variable reference",
                "expensive" : false
            ])
        })
    ]);
    write_response(res);
}

void handle_action_request(mixed msg) {
    handle_request_generic(msg);

    StoppedEvent evt = StoppedEvent();
    evt->body = (["reason" : "pause", "threadId" : 1]);
    sleep(0.5);
    write_event(evt);
}

void handle_breakpoints_request(mixed msg) {
  string path = msg->arguments->source->path;
  array(int) lines = msg->arguments->lines;
  foreach(lines, int line) {
    object bp = Debug.Debugger.add_breakpoint(path, line);
    bp->enable();
  }

    Request req = Request(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->command = req->command;

    write_response(res);
}

void handle_request_generic(mixed msg) {
    Request req = Request(msg);
    Response res = Response();

    res->request_seq = req->seq;
    res->success = 1;
    res->command = req->command;

    write_response(res);
}

// breakpointing support
Stdio.Port breakpoint_port;
int breakpoint_port_no = 3333;
Stdio.File breakpoint_client;

constant SINGLE_STEP = 1;
constant CONTINUE = 0;

object breakpoint_cond;
object evaluate_cond;
object wait_cond;
object bp_lock = Thread.Mutex();
object wait_lock = Thread.Mutex();
object breakpoint_hilfe;
object bpbe;
object bpbet;

int mode = .BreakpointHilfe.CONTINUE; 

public void set_mode(int m) {
	mode = m;
}

// pause up to wait_seconds for a debugger to connect before continuing
public void load_breakpoint(int wait_seconds)
{
    bpbe = Pike.Backend();
    bpbet = Thread.Thread(lambda(){ do { catch(bpbe(1000.0)); } while (1); });
//    bpbet->set_thread_name("Breakpoint Thread");

    werror("Starting Debug Server on port %d.\n", breakpoint_port_no);
    breakpoint_port = Stdio.Port(breakpoint_port_no, handle_breakpoint_client);
    breakpoint_port->set_backend(bpbe);
	Debug.Debugger.set_debugger(this);
	
	// wait of -1 is infinite
	if(wait_seconds > 0) 
  	  bpbe->call_out(wait_timeout, wait_seconds);
	  
	if(wait_seconds != 0) {
		werror("waiting up to " + wait_seconds + " for debugger to connect\n");
	    object key = wait_lock->lock();
	    wait_cond = Thread.Condition();
	    wait_cond->wait(key);
	    key = 0;
	}
}

void wait_timeout() {
	werror("debugger connect wait timed out.\n");
    object key = wait_lock->lock();
    wait_cond->signal();
    key = 0;
}

public int do_breakpoint(string file, int line, string opcode, object current_object, array bt)
{
  if(!breakpoint_client) return 0;

  global_bt = bt;
  StoppedEvent evt = StoppedEvent();
  evt->body->reason = "breakpoint";
  evt->body->threadId = 1;
  write_event(evt, true);
  // TODO: what do we do if a breakpoint is hit and we already have a running hilfe session,
  // such as if a second thread encounters a bp?
  object key = bp_lock->lock();
  breakpoint_cond = Thread.Condition();
  breakpoint_cond->wait(key);
  key = 0;
  // now, we must wait for the hilfe session to end.
  return mode;
}

private void handle_breakpoint_client(int id)
{
  if(breakpoint_client) {
    breakpoint_port->accept()->close();
  }
  else breakpoint_client = breakpoint_port->accept();

  // if we are holding pike execution until a debugger connects, remove the timeout.  
  if(wait_cond) {
    bpbe->remove_call_out(wait_timeout);
  }

  breakpoint_client->set_backend(bpbe);
  breakpoint_client->set_nonblocking(breakpoint_read, breakpoint_write, breakpoint_close);
}

private void breakpoint_close(mixed data)
{
    DEBUG("\nClose callback. Shutting down.\n");
    exit(0);
}


private void breakpoint_write(mixed id)
{
    if (im_a_server) {
        string m = obuf->read();
        if (sizeof(m)) breakpoint_client->write(m);
    }
}

private void breakpoint_read(mixed id, string data)
{
    mixed msg = Standards.JSON.decode(parse_request(data));
    DEBUG("\nReceived request: \n%s\n", data);
    switch (msg[?"command"]) {
        case "initialize":
            handle_initialize_request(msg);
            break;
        case "attach":
            handle_attach_request(msg);
            break;
        case "launch":
            handle_launch_request(msg);
            break;
        case "evaluate":
            handle_evaluate_request(msg);
            break;
        case "threads":
            handle_threads_request(msg);
            break;
        case "configurationDone":
            handle_configuration_done_request(msg);
            break;
        case "stackTrace":
            handle_stack_trace_request(msg);
            break;
        case "scopes":
            handle_scopes_request(msg);
            break;
        case "continue":
            handle_continue_request(msg);
            break;
        case "next":
        case "stepIn":
        case "stepOut":
            handle_action_request(msg);
            break;
        case "setBreakpoints":
            handle_breakpoints_request(msg);
            break;
        default:
            handle_request_generic(msg);
    }
}

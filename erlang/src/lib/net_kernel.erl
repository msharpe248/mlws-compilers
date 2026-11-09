-module(net_kernel).
-export([start/1, start/2, stop/0, connect_node/1, monitor_nodes/1, monitor_nodes/2,
         get_net_ticktime/0, set_net_ticktime/1, set_net_ticktime/2]).

%% Distribution, on MLWSVM's std.node: this VM becomes a node named
%% name@host, registered with this machine's `mlwsvm names` (which must be
%% running), with a node number made from its name. A program that uses it
%% must be compiled with --modules, and every node must run the same erlrt
%% (the same mlws-vm-erlc): terms cross as erlrt's structs, and funs as its
%% closures. net_kernel is a registered process that keeps the cookie and
%% connects to other nodes for the program's processes.

start([Name]) -> start(Name, #{});
start([Name, Type | _]) -> start(Name, #{name_domain => Type}).

start(Name, Opts) when is_atom(Name), is_map(Opts) ->
    case whereis(net_kernel) of
        undefined ->
            Full = full_name(atom_to_list(Name), maps:get(name_domain, Opts, longnames)),
            Cookie = erts:cookie(),
            case '$rt':node_start(Full, Cookie) of
                ok ->
                    P = spawn(fun() -> loop(Cookie) end),
                    register(net_kernel, P),
                    {ok, P};
                {error, Why} -> {error, Why}
            end;
        K -> {error, {already_started, K}}
    end.

full_name(Name, Type) ->
    case lists:member($@, Name) of
        true -> list_to_atom(Name);
        false ->
            Host = string:trim('$rt':hostname()),
            H = case Type of
                    shortnames -> hd(string:split(Host, "."));
                    _ -> Host
                end,
            list_to_atom(Name ++ "@" ++ H)
    end.

stop() -> {error, not_allowed}.

connect_node(Node) when is_atom(Node) -> erts:connect(Node).

monitor_nodes(Flag) -> '$rt':node_monitor_all(Flag).
monitor_nodes(Flag, _Opts) -> monitor_nodes(Flag).

%% How long, in seconds, a connection may hear nothing before its node is
%% taken as down (std.node's ticktime: an idle connection ticks every quarter
%% of it). For connections made from now on.
get_net_ticktime() ->
    case is_alive() of
        true -> '$rt':node_ticktime();
        false -> ignored
    end.

set_net_ticktime(T) when is_integer(T), T > 0 ->
    case get_net_ticktime() of
        ignored -> ignored;
        T -> unchanged;
        _ -> '$rt':node_set_ticktime(T), change_initiated
    end.
set_net_ticktime(T, _TransitionPeriod) -> set_net_ticktime(T).

loop(Cookie) ->
    receive
        {connect, Node, From} ->
            From ! {net_kernel, connected, Node, '$rt':node_connect(Node, Cookie)},
            loop(Cookie);
        {get_cookie, From} ->
            From ! {net_kernel, cookie, Cookie},
            loop(Cookie);
        {set_cookie, C} -> loop(C)
    end.

-module(a).
-export([main/1]).

%% Node a, with b running: messages, rpc, spawn, monitors and kills across
%% two MLWSVM nodes, found through `mlwsvm names`.
main(_) ->
    erlang:set_cookie(dist_test),
    io:format("~p ~p~n", [node(), is_alive()]),
    {ok, _} = net_kernel:start(a@localhost, #{}),
    B = b@localhost,
    io:format("~p ~p~n", [node(), wait(B, 100)]),
    io:format("nodes: ~p~n", [nodes()]),
    {echo, B} ! {self(), hello},
    receive {echoed, N, M} -> io:format("echoed ~p from ~p~n", [M, N]) end,
    io:format("rpc: ~p~n", [rpc:call(B, b, square, [7])]),
    {badrpc, {'EXIT', {Undef, _}}} = rpc:call(B, b, nope, []),
    io:format("rpc of nothing: ~p~n", [Undef]),
    Me = self(),
    X = 10,
    P = spawn(B, fun() -> Me ! {from, node(), X * 2} end),
    receive {from, N2, V} -> io:format("a fun on ~p: ~p (spawned there: ~p)~n", [N2, V, node(P) =:= B]) end,
    io:format("is_process_alive of b's: ~p~n", [catch is_process_alive(P)]),
    W = spawn(B, fun() -> receive _ -> ok end end),
    MRef = monitor(process, W),
    exit(W, kill),
    receive {'DOWN', MRef, process, W, Why} -> io:format("killed there: ~p~n", [Why]) end,
    monitor_node(B, true),
    {b_main, B} ! stop,
    receive {nodedown, B} -> io:format("nodedown ~p~n", [B]) end,
    io:format("nodes: ~p~n", [nodes()]).

%% b may not be up yet
wait(B, 0) -> net_adm:ping(B);
wait(B, K) ->
    case net_adm:ping(B) of
        pong -> pong;
        pang -> timer:sleep(50), wait(B, K - 1)
    end.

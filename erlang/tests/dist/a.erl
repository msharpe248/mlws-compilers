-module(a).
-export([main/1]).

%% Node a, with b running: messages, rpc, spawn, monitors and kills across
%% two MLWSVM nodes, found through `mlwsvm names`.
main(Args) ->
    erlang:set_cookie(dist_test),
    io:format("~p ~p~n", [node(), is_alive()]),
    {ok, _} = net_kernel:start(a@localhost, #{}),
    B = b@localhost,
    io:format("~p ~p~n", [node(), wait(B, 100)]),
    io:format("nodes: ~p~n", [nodes()]),
    {echo, B} ! {self(), hello},
    receive {echoed, N, M} -> io:format("echoed ~p from ~p~n", [M, N]) end,
    big(B, case Args of [Sz] -> list_to_integer(Sz); _ -> 1000000 end),
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

%% Terms of any size and depth, there and back, sharing kept: a list of N
%% (a million, but less when run.sh runs under MLWSVM's GC stress or
%% verify), tuples and lists nested N/10 deep, a map of N/10, 10N bytes.
big(B, N) ->
    S = lists:seq(1, 1000),
    D = N div 10,
    Terms = [{"a long list", lists:seq(1, N)},
             {"a long string", lists:duplicate(N div 5, $a)},
             {"{S, S, S}", {S, S, S}},
             {"nested tuples", lists:foldl(fun(_, A) -> {A} end, x, lists:seq(1, D))},
             {"a deep list", lists:foldl(fun(I, A) -> [I, A] end, [], lists:seq(1, D))},
             {"a big map", maps:from_list([{I, integer_to_list(I)} || I <- lists:seq(1, D)])},
             {"a big binary", list_to_binary(lists:duplicate(N, "0123456789"))},
             {"bigints", [1 bsl 200, -(1 bsl 100), 1.5e300]}],
    [begin
         {echo, B} ! {self(), V},
         receive {echoed, _, V2} -> io:format("~s: ~p~n", [Name, V2 =:= V]) end
     end || {Name, V} <- Terms],
    io:format("rpc length: ~p~n", [rpc:call(B, erlang, length, [lists:seq(1, N)]) =:= N]).

%% b may not be up yet
wait(B, 0) -> net_adm:ping(B);
wait(B, K) ->
    case net_adm:ping(B) of
        pong -> pong;
        pang -> timer:sleep(50), wait(B, K - 1)
    end.

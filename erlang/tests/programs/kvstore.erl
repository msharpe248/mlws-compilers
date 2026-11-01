%% A key-value server process with a client API.
-module(kvstore).
-export([main/0]).

start() -> spawn(fun() -> loop(#{}) end).

call(Pid, Req) ->
    Ref = make_ref(),
    Pid ! {call, self(), Ref, Req},
    receive {Ref, Reply} -> Reply after 1000 -> timeout end.

loop(State) ->
    receive
        {call, From, Ref, {put, K, V}} ->
            From ! {Ref, ok},
            loop(State#{K => V});
        {call, From, Ref, {get, K}} ->
            From ! {Ref, maps:find(K, State)},
            loop(State);
        {call, From, Ref, keys} ->
            From ! {Ref, lists:sort(maps:keys(State))},
            loop(State);
        {call, From, Ref, stop} ->
            From ! {Ref, stopped}
    end.

main() ->
    S = start(),
    [ok = call(S, {put, K, K * K}) || K <- lists:seq(1, 50)],
    io:format("~p~n", [call(S, {get, 7})]),
    io:format("~p~n", [call(S, {get, 99})]),
    io:format("~p~n", [length(call(S, keys))]),
    io:format("~p~n", [call(S, stop)]),
    io:format("~p~n", [call(S, keys)]).

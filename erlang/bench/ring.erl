%% A ring of processes passing a token around.
-module(ring).
-export([main/0]).

main() ->
    N = 1000, M = 1000,
    Self = self(),
    First = lists:foldl(fun(_, Next) -> spawn(fun() -> relay(Next) end) end, Self, lists:seq(1, N - 1)),
    First ! M,
    io:format("~p~n", [loop(First)]).

loop(First) ->
    receive
        0 -> done;
        K -> First ! K - 1, loop(First)
    end.

relay(Next) ->
    receive
        K -> Next ! K, relay(Next)
    end.

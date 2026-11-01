-module(multi).
-export([main/0]).

main() ->
    S0 = stack:new(),
    S1 = stack:push(1, stack:push(2, S0)),
    {Top, S2} = stack:pop(S1),
    io:format("~p ~p~n", [Top, stack:size(S2)]),
    io:format("~p~n", [geometry:area({circle, 1.0})]),
    ok.

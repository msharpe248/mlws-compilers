%% Sorting, maps and list functions.
-module(sorting).
-export([main/0]).

main() ->
    L = [(I * 7919) rem 100003 || I <- lists:seq(1, 200000)],
    S = lists:sort(L),
    io:format("~p ~p~n", [hd(S), lists:last(S)]),
    M = lists:foldl(fun(X, Acc) -> maps:update_with(X rem 1000, fun(C) -> C + 1 end, 1, Acc) end, #{}, L),
    io:format("~p~n", [maps:size(M)]),
    io:format("~p~n", [lists:sum(lists:map(fun(X) -> X * 2 end, L))]).

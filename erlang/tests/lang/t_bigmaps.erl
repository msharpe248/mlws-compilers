%% Maps of more than 32 keys (a tree in the runtime): values replaced in
%% place, keys of every kind, and 1 and 1.0 as two keys.
-module(t_bigmaps).
-export([main/0]).

main() ->
    Ks = [(I * 37) rem 101 - 50 || I <- lists:seq(1, 101)],
    M0 = maps:from_list([{K, 0} || K <- Ks]),
    M1 = lists:foldl(fun(K, M) -> maps:update_with(K, fun(C) -> C + 1 end, 1, M) end, M0, Ks ++ Ks),
    io:format("~p ~p~n", [maps:size(M1), lists:sum(maps:values(M1))]),
    io:format("~p~n", [lists:sublist(lists:sort(maps:to_list(M1)), 5)]),
    M2 = lists:foldl(fun(K, M) -> maps:update_with(K, fun(C) -> C * 10 end, 7, M) end, M1, [-50, 0, 50, 51, 1.0, 1]),
    io:format("~p ~p ~p ~p ~p~n", [maps:size(M2), maps:get(0, M2), maps:get(51, M2), maps:get(1.0, M2), maps:get(1, M2)]),
    Mixed = [a, b, 1.5, -2.5, 1 bsl 70, -(1 bsl 70), {t, 1}, [1, 2], <<"bin">>, "str", 1, 1.0, 0, 0.0],
    M3 = lists:foldl(fun(K, M) -> M#{K => 1} end, M2, Mixed),
    M4 = lists:foldl(fun(K, M) -> M#{K := 2} end, M3, Mixed),
    M5 = lists:foldl(fun(K, M) -> maps:update_with(K, fun(V) -> V + 1 end, 0, M) end, M4, Mixed),
    io:format("~p~n", [[{K, maps:get(K, M5)} || K <- Mixed]]),
    io:format("~p ~p~n", [maps:size(M5), length(lists:usort(maps:keys(M5)))]),
    io:format("~p ~p ~p~n", [maps:find(1.0, M5), maps:find(2.0, M5), maps:is_key(1 bsl 70, M5)]),
    M6 = maps:update_with(a, fun(V) -> V end, M5),
    io:format("~p ~p~n", [M6 =:= M5, maps:update_with(zz, fun(V) -> V end, 9, M6) =:= M6]),
    io:format("~p~n", [try maps:update_with(zz, fun(V) -> V end, M6) catch error:E -> E end]),
    M7 = maps:remove(0, maps:remove(1, M5)),
    io:format("~p ~p ~p~n", [maps:size(M7), maps:is_key(1, M7), maps:get(1.0, M7)]).

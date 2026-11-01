-module(t_maps).
-export([main/0]).

main() ->
    M = #{a => 1, b => 2},
    io:format("~p ~p~n", [maps:get(a, M), maps:get(c, M, none)]),
    M2 = M#{c => 3},
    M3 = M2#{a := 10},
    io:format("~p~n", [lists:sort(maps:to_list(M3))]),
    io:format("~p ~p~n", [maps:size(M3), maps:is_key(b, M3)]),
    #{a := A, c := C} = M3,
    io:format("~p ~p~n", [A, C]),
    io:format("~p~n", [maps:find(z, M3)]),
    io:format("~p~n", [lists:sort(maps:keys(maps:remove(b, M3)))]),
    Counts = lists:foldl(fun(W, Acc) -> maps:update_with(W, fun(N) -> N + 1 end, 1, Acc) end,
                         #{}, [x, y, x, z, x, y]),
    io:format("~p~n", [lists:sort(maps:to_list(Counts))]),
    io:format("~p~n", [maps:get(x, maps:map(fun(_, V) -> V * 100 end, Counts))]),
    io:format("~p~n", [maps:fold(fun(_, V, S) -> V + S end, 0, Counts)]),
    io:format("~p~n", [#{1 => one} =:= #{1 => one}]),
    io:format("~p~n", [maps:merge(#{a => 1}, #{a => 2})]),
    try M#{zz := 1} catch error:{badkey, zz} -> io:format("badkey~n") end,
    Big = maps:from_list([{I, I * I} || I <- lists:seq(1, 100)]),
    io:format("~p ~p~n", [maps:size(Big), maps:get(50, Big)]),
    io:format("~p~n", [#{k => [1, 2]}]),
    case M of
        #{b := 2} -> io:format("matched b~n");
        _ -> io:format("no~n")
    end,
    F = fun(#{a := V}) -> V; (_) -> no_a end,
    io:format("~p ~p~n", [F(#{a => x}), F(#{})]),
    ok.

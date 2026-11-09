-module(appenv).
-export([main/0]).

%% application:get_env / set_env / unset_env / get_all_env: every kind of
%% term set and read back, defaults, a config list, and a process that sees
%% what another sets.
main() ->
    io:format("~p~n", [application:get_env(myapp, port)]),
    io:format("~p~n", [application:get_env(myapp, port, 80)]),
    Terms = [{int, 8080}, {neg, -3}, {big, 1 bsl 100}, {float, 2.5},
             {atom, prod}, {tuple, {a, 1, "s"}}, {empty, {}}, {nil, []},
             {list, [1, two, {3}]}, {string, "hello"}, {bin, <<"bytes">>},
             {map, #{a => 1, "b" => [2]}}, {nested, [{x, #{y => {z}}}]},
             {bool, true}],
    [ok = application:set_env(myapp, K, V) || {K, V} <- Terms],
    [io:format("~p: ~p~n", [K, application:get_env(myapp, K) =:= {ok, V}]) || {K, V} <- Terms],
    io:format("~p~n", [application:get_env(myapp, tuple)]),
    ok = application:set_env([{other, [{a, 1}, {b, {2}}]}]),
    io:format("~p~n", [application:get_all_env(other)]),
    ok = application:unset_env(other, a),
    io:format("~p~n", [application:get_all_env(other)]),
    io:format("~p~n", [application:get_env(other, a, gone)]),
    Me = self(),
    spawn(fun() -> application:set_env(myapp, from_child, yes), Me ! done end),
    receive done -> ok end,
    io:format("~p~n", [application:get_env(myapp, from_child)]),
    io:format("~p~n", [application:get_all_env(none)]).

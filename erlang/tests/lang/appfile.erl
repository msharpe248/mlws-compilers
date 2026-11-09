-module(appfile).
-export([main/0]).

%% Values from appfile.env (run with --env appfile.env; the run of the
%% reference Erlang has none, so appfile.out is made by hand): a string is
%% an atom, an array a list, an object a map with atom keys; ${VAR}, an
%% OS environment variable, is nothing when there is none.
main() ->
    {ok, Port} = application:get_env(shop, port),
    io:format("~p ~p~n", [Port, Port + 1]),
    io:format("~p~n", [application:get_env(shop, rate)]),
    io:format("~p~n", [application:get_env(shop, mode) =:= {ok, maintenance}]),
    io:format("~p~n", [application:get_env(shop, servers)]),
    {ok, Db} = application:get_env(shop, db),
    io:format("~p ~p~n", [maps:get(host, Db), maps:get(port, Db)]),
    io:format("~p~n", [Db]),
    io:format("~p~n", [application:get_env(shop, huge)]),
    io:format("~p~n", [application:get_env(shop, greeting)]),
    io:format("~p~n", [[application:get_env(shop, K) || K <- [open, closed, none, key]]]),
    io:format("~p~n", [[K || {K, _} <- application:get_all_env(shop)]]),
    application:set_env(shop, port, 9090),
    io:format("~p~n", [application:get_env(shop, port)]),
    io:format("~p~n", [application:get_all_env(other)]).

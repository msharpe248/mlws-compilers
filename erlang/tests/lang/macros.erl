-module(macros).
-export([main/0]).
-include("macros.hrl").
-define(SQUARE(X), ((X) * (X))).
-define(GREETING, "hi").
-define(TWICE(F, X), F(F(X))).
-ifdef(DEBUG).
-define(LOG(X), io:format("debug ~p~n", [X])).
-else.
-define(LOG(X), ok).
-endif.
-ifndef(LIMIT).
-define(LIMIT, 10).
-endif.

inc(X) -> X + 1.

main() ->
    io:format("~p ~p~n", [?SQUARE(3), ?SQUARE(1 + 2)]),
    io:format("~s ~p~n", [?GREETING, ?MODULE]),
    io:format("~p~n", [?TWICE(inc, 5)]),
    ?LOG(hidden),
    io:format("~p ~p~n", [?LIMIT, ?SHARED]),
    io:format("~p ~p~n", [?FUNCTION_NAME, ?FUNCTION_ARITY]),
    io:format("~p~n", [?MODULE_STRING]),
    io:format("~p~n", [#point{x = 1}]),
    ok.

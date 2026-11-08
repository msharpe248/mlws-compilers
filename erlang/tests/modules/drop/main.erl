-module(main).
-export([main/1]).

%% A new version may drop a function that has been called: calls to it are
%% then undef, and the others go to the new code (MLWSVM 0.19.0's handles).
main([Dir]) ->
    io:format("~p ~p~n", [m:f(), m:g()]),
    {ok, B} = file:read_file(Dir ++ "/v2/m.masm"),
    io:format("~p~n", [code:load_binary(m, "m.masm", B)]),
    io:format("~p~n", [catch m:g()]),
    io:format("~p~n", [catch m:f()]).
